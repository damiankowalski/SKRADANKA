-- A servant: goes about his chores between a few spots. He is no fighter: anything that worries
-- him beyond a glance (a noise worth checking, a glimpse of the intruder, a body) sends him
-- running to the alarm bell, shouting. Ringing it tells every guard where the trouble is
-- (a strong report, as if each of them had heard it), or in a big level every guard within
-- `bell_reach` of the bell. Knock him out before he gets there.
-- Calm, he lights lamps the thief put out (Scripts/AI/Lamps.lua).

local Actor = require("Scripts/AI/Actor")
local Lamps = require("Scripts/AI/Lamps")
local Noises = require("Scripts/AI/Noises")

local Servant = {
    Properties = {
        waypoints = "",       -- comma separated entity names he walks between
        walk_speed = 1.2,
        run_speed = 3.6,
        wait_time = 4.0,
        bell = "Bell",        -- entity he runs to when alarmed
        bell_strength = 9.0,  -- strength of the report every guard gets when he rings
        bell_reach = 0.0,     -- metres from the bell within which guards get it (0: every guard in the level)
    }
}

local LINES = {
    Alarm = { "Guards! Guards!", "Help! Intruder!", "Somebody help!" },
    Calm = { "Phew...", "Must have been nothing." },
}

function Servant:on_create()
    Actor.init(self)
    self.points = {}
    for name in string.gmatch(self.waypoints, "[^,%s]+") do
        local waypoint = scene.find(name)
        if waypoint then table.insert(self.points, waypoint:get_position()) else log.warn("Servant: waypoint '" .. name .. "' not found") end
    end
    self.index = 1
    self.wait = 0
    self.bell_entity = scene.find(self.bell)
    self.fleeing = nil -- position of the trouble while running to the bell
    self.rang = 0
end

function Servant:ring()
    local bell = self.bell_entity:get_position()
    Noises.emit("Bell", bell + vec3(0, 2, 0), self.entity)
    for _, npc in ipairs(scene.entities_with("Perception")) do
        if npc ~= self.entity and ai.vital_state(npc) == "Awake"
            and (self.bell_reach <= 0 or npc:get_position():distance(bell) <= self.bell_reach) then
            ai.notify(npc, self.fleeing, self.bell_strength, "Bell")
        end
    end
    self.rang = self.rang + 1
end

function Servant:chores(dt)
    if #self.points == 0 then
        Actor.stand(self, "Idle")
        return
    end
    if self.wait > 0 then
        self.wait = self.wait - dt
        Actor.stand(self, "Use_Item")
        return
    end
    if Actor.go(self, self.points[self.index], self.walk_speed) then
        self.index = self.index % #self.points + 1
        self.wait = self.wait_time
        self.goal = nil
    end
end

function Servant:on_update(dt)
    if Actor.down(self, dt) then return end
    local reaction = ai.reaction(self.entity)

    if not self.fleeing and (reaction.state == "Investigating" or reaction.state == "Searching" or reaction.state == "Alarmed")
        and reaction.cause ~= "Bell" and self.bell_entity and reaction.position then
        self.fleeing = reaction.position
        Actor.bark(self, LINES.Alarm, "Audio/Alert.wav")
        ai.shout(self.entity, 7)
    end

    if self.fleeing or reaction.state ~= "Unaware" then Lamps.release(self) end
    if self.fleeing then
        if Actor.go(self, self.bell_entity:get_position(), self.run_speed, "Running_A", 3.0) then
            self:ring()
            self.fleeing = nil
            ai.calm(self.entity)
            self.wait = 10 -- catches his breath by the bell
            Actor.bark(self, LINES.Calm)
        end
    elseif reaction.state == "Suspicious" and reaction.position then
        Actor.stand(self, "Look_Around")
        self.entity:look_at(reaction.position, true)
    elseif reaction.state ~= "Unaware" or not Lamps.chore(self, dt, self.walk_speed) then
        self:chores(dt)
    end
    Actor.draw_bark(self, dt)
end

return Servant
