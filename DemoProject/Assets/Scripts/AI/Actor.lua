-- Shared behaviour helpers for NPC scripts (Guard.lua, Servant.lua): walking along the navmesh,
-- animation, falling when knocked out, and spoken lines ("barks") shown above the head.
-- Use: Actor.init(self) in on_create, then Actor.go(self, ...), Actor.bark(self, ...) etc.
-- Every call takes the script instance; the state lives in it.

local Actor = {}

function Actor.init(self)
    self.goal = nil
    self.bark_text = nil
    self.bark_timer = 0
    self.fallen = nil
    for _, child in ipairs(self.entity:children()) do
        if child:get_name() == "Model" then self.model = child end
    end
end

-- Plays a looping clip; moving clips run at a rate matching the ground speed.
function Actor.animate(self, clip, reference)
    if not self.model then return end
    anim.play(self.model, clip, 0.25, true)
    local rate = 1.0
    if reference then
        local velocity = character.get_velocity(self.entity)
        rate = math.max(0.6, math.min(vec3(velocity.x, 0, velocity.z):length() / reference, 1.8))
    end
    anim.set_speed(self.model, rate)
end

-- Walks to the target along the navmesh; a new path is only requested when the goal moves, so
-- calling this every frame is cheap. Returns true once there (or when it cannot get closer).
function Actor.go(self, target, speed, clip, reference)
    if not self.goal or self.goal:distance(target) > 0.5 or nav.status(self.entity) == "Idle" then
        self.goal = target
        nav.move_to(self.entity, target, speed)
    end
    if nav.status(self.entity) == "Moving" then
        Actor.animate(self, clip or "Walking_B", reference or 1.0)
        return false
    end
    return true -- Arrived, or Failed / Stuck: either way, carry on with the next step
end

function Actor.stand(self, clip)
    if nav.status(self.entity) == "Moving" then nav.stop(self.entity) end
    self.goal = nil
    Actor.animate(self, clip or "Idle")
end

-- True while the NPC is down (knocked out or dead); plays the fall once, then holds the pose.
function Actor.down(self, dt)
    if ai.vital_state(self.entity) == "Awake" then return false end
    if not self.fallen then
        self.fallen = 0
        self.bark_text = nil
        if self.model then
            anim.play(self.model, "Death_A", 0.1, false)
            anim.set_speed(self.model, 1.0)
        end
    end
    self.fallen = self.fallen + dt
    if self.model and self.fallen > 1.5 and anim.is_playing(self.model, "Death_A") == false then
        anim.play(self.model, "Death_A_Pose", 0.0, true)
    end
    return true
end

-- Says a line: one picked at random from a list (or the string itself), shown above the head for
-- a few seconds. Voices are placeholders: a short sound plus the subtitle.
function Actor.bark(self, lines, sound)
    local line = type(lines) == "table" and lines[math.random(#lines)] or lines
    self.bark_text = line
    self.bark_timer = 2.5
    audio.play_at(sound or "Audio/Mumble.wav", self.entity:get_position() + vec3(0, 1.7, 0), 0.6)
end

function Actor.draw_bark(self, dt)
    if not self.bark_text then return end
    self.bark_timer = self.bark_timer - dt
    if self.bark_timer <= 0 then
        self.bark_text = nil
        return
    end
    local x, y = ui.world_to_screen(self.entity:get_position() + vec3(0, 2.25, 0))
    if x then
        local alpha = math.min(self.bark_timer / 0.5, 1.0)
        ui.text(x - #self.bark_text * 5.4, y, self.bark_text, 1.8, { 1.0, 0.95, 0.8, alpha })
    end
end

return Actor
