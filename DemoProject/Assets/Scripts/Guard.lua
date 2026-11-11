-- Guard behaviour driven by the engine's perception state:
--   Unaware    -> patrol the waypoints
--   Suspicious -> stop and turn towards the stimulus
--   Searching  -> walk to where something was seen or heard
--   Alarmed    -> run at the intruder; catching him restarts the level
-- The "Model" child plays a clip for each behaviour (walk, look around, run).

local Guard = {
    Properties = {
        waypoints = "",      -- comma separated entity names
        walk_speed = 1.4,
        run_speed = 3.8,
        wait_time = 2.0,
        catch_distance = 1.2,
    }
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
    self.points = parse_waypoints(self.waypoints)
    self.index = 1
    self.wait = 0
    self.player = scene.find("Player")
    self.caught_timer = nil
    for _, child in ipairs(self.entity:children()) do
        if child:get_name() == "Model" then self.model = child end
    end
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

function Guard:walk_towards(target, speed)
    local position = self.entity:get_position()
    local offset = vec3(target.x - position.x, 0, target.z - position.z)
    local distance = offset:length()
    if distance < 0.3 then
        character.move(self.entity, vec3(0, 0, 0))
        return true
    end
    local direction = offset / distance
    character.move(self.entity, direction * speed)
    self.entity:look_at(position + direction, true)
    return false
end

function Guard:patrol(dt)
    if #self.points == 0 then
        character.move(self.entity, vec3(0, 0, 0))
        self:animate("Idle")
        return
    end
    if self.wait > 0 then
        self.wait = self.wait - dt
        character.move(self.entity, vec3(0, 0, 0))
        self:animate("Idle")
        return
    end
    self:animate("Walking_B", 1.0)
    if self:walk_towards(self.points[self.index], self.walk_speed) then
        self.index = self.index % #self.points + 1
        self.wait = self.wait_time
    end
end

function Guard:on_update(dt)
    if self.caught_timer then
        self.caught_timer = self.caught_timer - dt
        ui.text(ui.width() / 2 - 150, ui.height() / 2 - 20, "YOU HAVE BEEN CAUGHT", 4, { 1, 0.2, 0.15, 1 })
        if self.caught_timer <= 0 then scene.load("Scenes/Stealth.pscene") end
        return
    end

    local state = ai.state(self.entity)
    local stimulus = ai.last_stimulus(self.entity)

    if state == "Unaware" then
        self:patrol(dt)
    elseif state == "Suspicious" then
        character.move(self.entity, vec3(0, 0, 0))
        if stimulus then self.entity:look_at(stimulus, true) end
        self:animate("Look_Around")
    elseif state == "Searching" then
        local arrived = not stimulus or self:walk_towards(stimulus, self.walk_speed)
        if arrived then self:animate("Look_Around") else self:animate("Walking_B", 1.0) end
    else -- Alarmed
        self:animate("Running_B", 2.4)
        local target = self.player and self.player:valid() and self.player:get_position() or stimulus
        if target then
            self:walk_towards(target, self.run_speed)
            if self.player and self.entity:get_position():distance(self.player:get_position()) < self.catch_distance then
                self.caught_timer = 3.0
                self:animate("Cheer")
                character.move(self.entity, vec3(0, 0, 0))
                character.move(self.player, vec3(0, 0, 0))
                audio.play_at("Audio/Alarm.wav", self.entity:get_position(), 1.0)
            end
        end
    end

    if state ~= "Unaware" and state ~= self.last_state then
        audio.play_at("Audio/Alert.wav", self.entity:get_position(), 0.5)
    end
    self.last_state = state
end

return Guard
