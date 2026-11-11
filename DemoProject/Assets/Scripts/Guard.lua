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
        catch_scene = "Scenes/Stealth.pscene", -- loaded when the player is caught
    }
}

local RANK = { Unaware = 0, Suspicious = 1, Investigating = 2, Searching = 3, Alarmed = 4 }

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
    self.points = parse_waypoints(self.waypoints)
    self.index = 1
    self.wait = 0
    self.player = scene.find("Player")
    self.caught_timer = nil
    self.state = "Unaware"
    self:reset_activity()
    for _, child in ipairs(self.entity:children()) do
        if child:get_name() == "Model" then self.model = child end
    end
end

function Guard:reset_activity()
    self.goal = nil
    self.phase = "go"
    self.timer = 0
    self.spot = nil
    self.visited = {}
    self.stimulus = nil
end

-- Plays a clip; moving clips run at a rate matching the guard's ground speed.
function Guard:animate(clip, reference)
    if not self.model then return end
    anim.play(self.model, clip, 0.25, true)
    local rate = 1.0
    if reference then
        local velocity = character.get_velocity(self.entity)
        rate = math.max(0.6, math.min(vec3(velocity.x, 0, velocity.z):length() / reference, 1.8))
    end
    anim.set_speed(self.model, rate)
end

-- Walks to the target along the navmesh. A new path is only requested when the goal moves,
-- so calling this every frame is cheap. Returns true once there (or when it cannot get closer).
function Guard:go(target, speed, clip, reference)
    if not self.goal or self.goal:distance(target) > 0.5 or nav.status(self.entity) == "Idle" then
        self.goal = target
        nav.move_to(self.entity, target, speed)
    end
    local status = nav.status(self.entity)
    if status == "Moving" then
        self:animate(clip or "Walking_B", reference or 1.0)
        return false
    end
    return true -- Arrived, or Failed / Stuck: either way, carry on with the next step
end

function Guard:stand(clip)
    if nav.status(self.entity) == "Moving" then nav.stop(self.entity) end
    self.goal = nil
    self:animate(clip or "Idle")
end

function Guard:face(position)
    if position then self.entity:look_at(position, true) end
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
    if self.phase == "go" then
        if self:go(center, speed, speed > self.walk_speed and "Running_B" or "Walking_B", speed > self.walk_speed and 2.4 or 1.0) then
            self.phase, self.timer = "look", self.look_time
        end
    elseif self.phase == "look" then
        self:stand("Look_Around")
        self.timer = self.timer - dt
        if self.timer <= 0 then
            self.spot = self:pick_spot(center, radius, prefer_dark)
            self.phase = self.spot and "spot" or "look"
            self.timer = self.look_time
        end
    elseif self.phase == "spot" then
        if self:go(self.spot, speed) then self.phase, self.timer = "look", self.look_time end
    end
end

function Guard:patrol(dt)
    if #self.points == 0 then
        self:stand("Idle")
        return
    end
    if self.wait > 0 then
        self.wait = self.wait - dt
        self:stand("Idle")
        return
    end
    if self:go(self.points[self.index], self.walk_speed) then
        self.index = self.index % #self.points + 1
        self.wait = self.wait_time
        self.goal = nil
    end
end

function Guard:catch()
    self.caught_timer = 3.0
    nav.stop(self.entity)
    self:animate("Cheer")
    character.move(self.player, vec3(0, 0, 0))
    audio.play_at("Audio/Alarm.wav", self.entity:get_position(), 1.0)
end

function Guard:on_update(dt)
    if self.caught_timer then
        self.caught_timer = self.caught_timer - dt
        ui.text(ui.width() / 2 - 150, ui.height() / 2 - 20, "YOU HAVE BEEN CAUGHT", 4, { 1, 0.2, 0.15, 1 })
        if self.caught_timer <= 0 then scene.load(self.catch_scene) end
        return
    end

    local reaction = ai.reaction(self.entity)
    local state = reaction.state
    if state ~= self.state then
        if RANK[state] > RANK[self.state] then
            audio.play_at("Audio/Alert.wav", self.entity:get_position(), 0.5)
        end
        -- A calmer state keeps the place being checked; a new or stronger one starts over.
        if RANK[state] > RANK[self.state] or state == "Unaware" then self:reset_activity() end
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
        self:patrol(dt)
    elseif state == "Suspicious" then
        self.glance = self.glance + dt
        if self.glance < self.glance_patience or not reaction.position then
            self:stand("Look_Around")
            self:face(reaction.position)
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
            self:go(reaction.position, self.run_speed, "Running_B", 2.4)
            if self.entity:get_position():distance(self.player:get_position()) < self.catch_distance then
                self:catch()
            end
        else
            self:check_area(dt, center, self.run_speed, self.search_radius, true)
        end
    end
end

return Guard
