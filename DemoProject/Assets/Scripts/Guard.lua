-- Guard behaviour on top of the engine's reaction model (docs/Perception.md). The engine decides
-- how strongly the guard reacts and for how long (ai.reaction): a stimulus of strength s keeps
-- him busy 90 s * (s/10)^2 and the state steps down as it fades. This script decides what he
-- does meanwhile; every activity ends by itself and he always goes back to his route.
--
--   Unaware       patrol the waypoints along the navmesh
--   Suspicious    stop, face the stimulus and look around; if it keeps bothering him, walk over
--   Investigating walk to the stimulus, look around, check a few spots nearby
--   Searching     hurry there, then search the area, darkest spots first
--   Alarmed       run at the intruder; catching him restarts the level. Lost him: search.
--
-- Bumping into the player (or being bumped into) alarms him whatever the light (cause "Touch").
--
-- He talks: a line for every change of mind (subtitles above his head), and when he starts a
-- search or raises the alarm he shouts, which sends guards within earshot to the same place
-- (ai.shout). Knocked out (blackjack, gas) he falls and stays down: a body others may find.
--
-- Calm, he notices lamps put out at a switch and goes to light them again (Scripts/AI/Lamps.lua).

local Actor = require("Scripts/AI/Actor")
local Lamps = require("Scripts/AI/Lamps")
local Mission = require("Scripts/Mission/Mission")

local Guard = {
    Properties = {
        waypoints = "",          -- comma separated entity names
        walk_speed = 1.4,
        search_speed = 2.2,
        run_speed = 3.8,
        wait_time = 2.0,         -- pause at each waypoint
        look_time = 2.5,         -- looking around at each spot he checks
        glance_patience = 4.0,   -- seconds of staring before he walks over to have a look
        investigate_radius = 3.0,
        search_radius = 7.0,
        catch_distance = 1.2,
        shout_loudness = 8.0,    -- how far his shouts carry (0-10 loudness scale)
        catch_scene = "Scenes/Stealth.pscene", -- loaded when the player is caught
    }
}

local RANK = { Unaware = 0, Suspicious = 1, Investigating = 2, Searching = 3, Alarmed = 4 }

-- What he says when his state changes (the voice is a placeholder sound; the words are subtitles).
local LINES = {
    Suspicious = { "Hm?", "What was that?", "Who's there?" },
    Investigating = { "I'd better take a look.", "Something over there...", "Let's have a look." },
    Searching = { "Someone's here!", "Show yourself!", "I know you're here!" },
    Alarmed = { "Intruder!", "Stop right there!", "There you are!" },
    Calm = { "Must have been rats.", "Nothing there.", "Hm. Imagining things." },
    Body = { "A body! Sound the alarm!", "Someone's been attacked!" },
    Shout = { "What's going on?", "Coming!", "On my way!" },
    Touch = { "Who's that?!", "Got you!", "What the-" },
}

local function parse_waypoints(list)
    local points = {}
    for name in string.gmatch(list, "[^,%s]+") do
        local waypoint = scene.find(name)
        if waypoint then
            table.insert(points, waypoint:get_position())
        else
            log.warn("Guard: waypoint '" .. name .. "' not found")
        end
    end
    return points
end

function Guard:on_create()
    Actor.init(self)
    self.points = parse_waypoints(self.waypoints)
    self.index = 1
    self.wait = 0
    self.player = scene.find("Player")
    self.caught_timer = nil
    self.state = "Unaware"
    self:reset_activity()
end

function Guard:reset_activity()
    self.goal = nil
    self.phase = "go"
    self.timer = 0
    self.spot = nil
    self.visited = {}
    self.stimulus = nil
end

-- Said from outside (e.g. WorldEvents: "Just the storm.").
function Guard:bark(lines, sound)
    Actor.bark(self, lines, sound)
end

-- A spot worth checking near the centre: reachable, preferably dark, not checked yet.
function Guard:pick_spot(center, radius, prefer_dark)
    local best, best_score = nil, -math.huge
    for _ = 1, 6 do
        local point = nav.random_point(center, radius)
        if point then
            local score = 0
            if prefer_dark then score = -math.log(ai.luminance(point + vec3(0, 1, 0)) + 0.001) end
            for _, seen in ipairs(self.visited) do
                if seen:distance(point) < 2.0 then score = score - 10 end
            end
            if score > best_score then best, best_score = point, score end
        end
    end
    if best then table.insert(self.visited, best) end
    return best
end

-- Go to the stimulus, look around, then check spots nearby until the reaction is over.
function Guard:check_area(dt, center, speed, radius, prefer_dark)
    local hurry = speed > self.walk_speed
    if self.phase == "go" then
        if Actor.go(self, center, speed, hurry and "Running_B" or "Walking_B", hurry and 2.4 or 1.0) then
            self.phase, self.timer = "look", self.look_time
        end
    elseif self.phase == "look" then
        Actor.stand(self, "Look_Around")
        self.timer = self.timer - dt
        if self.timer <= 0 then
            self.spot = self:pick_spot(center, radius, prefer_dark)
            self.phase = self.spot and "spot" or "look"
            self.timer = self.look_time
        end
    elseif self.phase == "spot" then
        if Actor.go(self, self.spot, speed) then self.phase, self.timer = "look", self.look_time end
    end
end

function Guard:patrol(dt)
    if #self.points == 0 then
        Actor.stand(self, "Idle")
        return
    end
    if self.wait > 0 then
        self.wait = self.wait - dt
        Actor.stand(self, "Idle")
        return
    end
    if Actor.go(self, self.points[self.index], self.walk_speed) then
        self.index = self.index % #self.points + 1
        self.wait = self.wait_time
        self.goal = nil
    end
end

function Guard:catch()
    self.caught_timer = 3.0
    nav.stop(self.entity)
    Actor.animate(self, "Cheer")
    character.move(self.player, vec3(0, 0, 0))
    audio.play_at("Audio/Alarm.wav", self.entity:get_position(), 1.0)
end

-- A new state: say something, and shout when it is serious (unless he is only passing on what
-- someone else shouted, so the news spreads without echoing back and forth).
function Guard:on_state_change(previous, state, reaction)
    local rising = RANK[state] > RANK[previous]
    if rising then
        if reaction.cause == "Body" then
            Actor.bark(self, LINES.Body, "Audio/Alert.wav")
        elseif reaction.cause == "Shout" or reaction.cause == "Bell" then
            Actor.bark(self, LINES.Shout, "Audio/Alert.wav")
        elseif reaction.cause == "Touch" then
            Actor.bark(self, LINES.Touch, "Audio/Alert.wav")
        else
            Actor.bark(self, LINES[state], RANK[state] >= 3 and "Audio/Alert.wav" or "Audio/Mumble.wav")
        end
        if RANK[state] >= 3 and reaction.kind ~= "Reported" then
            local alerted = ai.shout(self.entity, self.shout_loudness)
            if #alerted > 0 then log.trace(string.format("Guard shout reached %d guard(s)", #alerted)) end
        end
    elseif state == "Unaware" and RANK[previous] >= 2 then
        Actor.bark(self, LINES.Calm)
    end
end

function Guard:on_update(dt)
    if Actor.down(self, dt) then return end
    if self.caught_timer then
        self.caught_timer = self.caught_timer - dt
        ui.text(ui.width() / 2 - 150, ui.height() / 2 - 20, "YOU HAVE BEEN CAUGHT", 4, { 1, 0.2, 0.15, 1 })
        if self.caught_timer <= 0 then scene.load(self.catch_scene) end
        return
    end

    local reaction = ai.reaction(self.entity)
    local state = reaction.state
    if state ~= self.state then
        self:on_state_change(self.state, state, reaction)
        -- A calmer state keeps the place being checked; a new or stronger one starts over.
        if RANK[state] > RANK[self.state] or state == "Unaware" then self:reset_activity() end
        if state ~= "Unaware" then Lamps.release(self) end
        self.state = state
        self.glance = 0
    end
    -- Something new happened somewhere else: start checking from there.
    if reaction.position and self.stimulus and reaction.position:distance(self.stimulus) > 1.5 and state ~= "Alarmed" then
        self:reset_activity()
    end
    self.stimulus = reaction.position

    local center = reaction.position or self.entity:get_position()
    if state == "Unaware" then
        if not Lamps.chore(self, dt, self.walk_speed) then self:patrol(dt) end
    elseif state == "Suspicious" then
        self.glance = self.glance + dt
        if self.glance < self.glance_patience or not reaction.position then
            Actor.stand(self, "Look_Around")
            if reaction.position then self.entity:look_at(reaction.position, true) end
        else
            self:check_area(dt, center, self.walk_speed, self.investigate_radius, false)
        end
    elseif state == "Investigating" then
        self:check_area(dt, center, self.walk_speed, self.investigate_radius, false)
    elseif state == "Searching" then
        self:check_area(dt, center, self.search_speed, self.search_radius, true)
    else -- Alarmed
        -- The engine keeps the stimulus on the player while he is in sight.
        local seeing = reaction.kind == "Seen" and self.player and self.player:valid() and
            self.player:get_position():distance(center) < 1.0
        if seeing then
            Actor.go(self, center, self.run_speed, "Running_B", 2.4)
            if self.entity:get_position():distance(self.player:get_position()) < self.catch_distance and not Mission.over() then
                self:catch()
            end
        else
            self:check_area(dt, center, self.run_speed, self.search_radius, true)
        end
    end
    Actor.draw_bark(self, dt)
end

return Guard
