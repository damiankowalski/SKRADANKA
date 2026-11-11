-- Perception lab guard: stands still and slowly scans left and right, turns towards anything
-- suspicious, and resets a few seconds after raising the alarm so testing can continue.

local Sentry = {
    Properties = {
        scan_angle = 50.0,  -- degrees either side of the starting direction
        scan_period = 8.0,  -- seconds for a full left-right-left sweep
        reset_after = 3.0,  -- seconds the alarm stays up before calming down
    }
}

function Sentry:on_create()
    local facing = self.entity:forward()
    self.base_yaw = math.deg(math.atan(-facing.x, -facing.z))
    self.time = 0
    self.alarm_timer = nil
end

function Sentry:face(yaw_degrees)
    local yaw = math.rad(yaw_degrees)
    local position = self.entity:get_position()
    self.entity:look_at(position + vec3(-math.sin(yaw), 0, -math.cos(yaw)), true)
end

function Sentry:on_update(dt)
    local state = ai.state(self.entity)
    if state == "Alarmed" then
        self.alarm_timer = self.alarm_timer or self.reset_after
        self.alarm_timer = self.alarm_timer - dt
        if self.alarm_timer <= 0 then
            ai.set_suspicion(self.entity, 0)
            self.alarm_timer = nil
        end
    end

    local stimulus = ai.last_stimulus(self.entity)
    if state ~= "Unaware" and stimulus then
        self.entity:look_at(stimulus, true)
        return
    end
    self.time = self.time + dt
    local sweep = math.sin(self.time * 2 * math.pi / self.scan_period) * self.scan_angle
    self:face(self.base_yaw + sweep)
end

return Sentry
