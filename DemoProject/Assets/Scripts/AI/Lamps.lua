-- Lamps put out at a switch (Scripts/World/LightSwitch.lua) do not stay dark for ever: a calm NPC
-- who sees one (close enough, in front of him, nothing solid between) says so, walks to its switch
-- and turns it back on. Only one NPC goes per switch. Anything more pressing (a noise, a glimpse
-- of the thief) comes first: the NPC script drops the chore with Lamps.release.
--
-- Use from an NPC script (the state lives in the script instance, like Actor.lua):
--   if Lamps.chore(self, dt) then return end   -- while calm: busy relighting
--   Lamps.release(self)                         -- when something else takes over

local Actor = require("Scripts/AI/Actor")

local Lamps = {
    sight_distance = 14.0, -- metres
    field_of_view = 0.3,   -- cosine of the half angle he notices things in (about 145 degrees wide)
    check_interval = 0.5,  -- seconds between looks around
    claim_timeout = 40.0,  -- a claim older than this is free again (he never got there)
}

local NOTICE = { "Who put the lights out?", "Why is it dark in here?", "Hm. The lamps are out." }
local DONE = { "That's better.", "There we are." }

-- Every light switch in the scene (their script instances).
function Lamps.switches()
    local list = {}
    for _, entity in ipairs(scene.entities_with("Script")) do
        local instance = entity:script()
        if instance and instance.is_light_switch then table.insert(list, instance) end
    end
    return list
end

-- Can the NPC see the point: close enough, in front of him, nothing solid in between.
function Lamps.can_see(npc, point)
    local eye = npc:get_position() + vec3(0, 1.6, 0)
    local to_point = point - eye
    local distance = to_point:length()
    if distance > Lamps.sight_distance then return false end
    if distance < 0.5 then return true end
    local direction = to_point / distance
    local flat = vec3(direction.x, 0, direction.z)
    if flat:length() > 0.1 and npc:forward():dot(flat:normalized()) < Lamps.field_of_view then return false end
    local hit = physics.raycast(eye, direction, distance, npc)
    return not hit or hit.distance > distance - 0.3
end

local function claim_free(switch)
    local claim = switch.claimed_by
    if not claim then return true end
    if not claim.entity:valid() or ai.vital_state(claim.entity) ~= "Awake" then return true end
    return time.elapsed() - claim.time > Lamps.claim_timeout
end

-- A dark switch whose lamp he can see and nobody else is seeing to, or nil.
function Lamps.noticed(npc)
    for _, switch in ipairs(Lamps.switches()) do
        if not switch.lit and claim_free(switch) then
            for _, lamp in ipairs(switch.lamp_entities) do
                if lamp:valid() and Lamps.can_see(npc, lamp:get_position()) then return switch end
            end
        end
    end
    return nil
end

-- Drives the relighting chore. Returns true while it keeps the NPC busy.
function Lamps.chore(self, dt, speed)
    local job = self.lamp_job
    if not job then
        self.lamp_look = (self.lamp_look or 0) - dt
        if self.lamp_look > 0 then return false end
        self.lamp_look = Lamps.check_interval
        local switch = Lamps.noticed(self.entity)
        if not switch then return false end
        switch.claimed_by = { entity = self.entity, time = time.elapsed() }
        job = { switch = switch, phase = "go" }
        self.lamp_job = job
        self.goal = nil
        Actor.bark(self, NOTICE)
    end

    local switch = job.switch
    if switch.lit or not switch.entity:valid() then -- someone beat him to it
        Lamps.release(self)
        return false
    end
    if job.phase == "go" then
        if Actor.go(self, switch:standing_point(), speed or 1.4) then
            job.phase, job.timer = "flip", 0.6
            self.entity:look_at(switch.entity:get_position(), true)
        end
    else
        Actor.stand(self, "Use_Item")
        job.timer = job.timer - dt
        if job.timer <= 0 then
            switch:set(true, self.entity)
            Actor.bark(self, DONE)
            Lamps.release(self)
        end
    end
    return true
end

-- Gives up the chore (and the switch, for someone else to see to).
function Lamps.release(self)
    local job = self.lamp_job
    if job and job.switch.claimed_by and job.switch.claimed_by.entity == self.entity then job.switch.claimed_by = nil end
    self.lamp_job = nil
end

return Lamps
