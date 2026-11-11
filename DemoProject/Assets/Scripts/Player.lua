-- Third-person thief: movement, over-the-shoulder camera, footstep noise, arrows and HUD.
--
-- Controls: WASD move, Shift run, C crouch, Space jump, mouse look,
--           1 = water arrow, 2 = fire arrow, left mouse = shoot, Esc = release the mouse.
-- Noise probe (when the noise_probe property is on): - / = pick a loudness, G makes that noise
-- where you aim. The HUD shows the floor under you and how loud your last step was.

local LightGem = require("Scripts/LightGem")
local Noises = require("Scripts/AI/Noises")

local Player = {
    Properties = {
        walk_speed = 2.4,
        run_speed = 5.0,
        crouch_speed = 1.2,
        stand_height = 1.8,
        crouch_height = 1.1,
        mouse_sensitivity = 0.12,
        camera_distance = 3.2,
        arrow_speed = 32.0,
        noise_probe = true,
    }
}

local ARROWS = {
    { name = "Water", prefab = "Prefabs/WaterArrow.pprefab", color = { 0.4, 0.7, 1.0, 1.0 } },
    { name = "Fire", prefab = "Prefabs/FireArrow.pprefab", color = { 1.0, 0.55, 0.2, 1.0 } },
}

local STANDING = {
    vec3(0, 1.70, 0), vec3(-0.22, 1.45, 0), vec3(0.22, 1.45, 0), vec3(0, 1.15, 0),
    vec3(-0.2, 0.9, 0), vec3(0.2, 0.9, 0), vec3(0, 0.45, 0),
}

local function scaled_silhouette(scale)
    local points = {}
    for i, p in ipairs(STANDING) do
        points[i] = vec3(p.x, p.y * scale, p.z)
    end
    return points
end

function Player:on_create()
    -- Yaw from the facing direction (Euler angles are ambiguous for some rotations).
    local facing = self.entity:forward()
    self.yaw = math.deg(math.atan(-facing.x, -facing.z))
    self.pitch = -10
    self.arrow = 1
    self.step_timer = 0
    self.cooldown = 0
    self.crouched = false
    self.gem = LightGem.new()
    self.visual_height = self.stand_height
    self.shoot_timer = 0
    for _, child in ipairs(self.entity:children()) do
        if child:get_name() == "Model" then self.model = child end
    end
    self.pending_arrow = nil
    self.airborne = false
    self.fall_speed = 0
    self.last_step = nil -- { loudness, material } of the last footstep, for the HUD
    self.probe_loudness = 5
    self.camera = scene.find("Camera")
    input.set_cursor_locked(true)
end

-- Crouching shrinks the physics capsule (the engine refuses to stand up under a low ceiling),
-- switches to the crouch clips and lowers the silhouette guards see. Everything follows the height the character
-- actually has, so a blocked stand-up keeps the thief crouched.
function Player:update_stance(dt, want_crouch)
    -- Read the height physics settled on last step, then request the new one.
    local height = self.entity:get("CharacterController").Height
    self.entity:set("CharacterController", { Height = want_crouch and self.crouch_height or self.stand_height })
    self.crouched = height < (self.stand_height + self.crouch_height) * 0.5

    -- The camera and the silhouette ease towards the capsule height.
    local previous = self.visual_height
    self.visual_height = previous + (height - previous) * math.min(dt * 12, 1)
    local scale = self.visual_height / self.stand_height
    if math.abs(self.visual_height - previous) > 0.001 then
        -- Fewer, lower silhouette points: harder to spot against bright backgrounds.
        self.entity:set("PerceptionTarget", { SilhouettePoints = scaled_silhouette(scale) })
    end
end

function Player:update_look()
    local dx, dy = input.mouse_delta()
    self.yaw = self.yaw - dx * self.mouse_sensitivity
    self.pitch = math.max(-70, math.min(45, self.pitch - dy * self.mouse_sensitivity))
end

function Player:update_movement(dt)
    local yaw = math.rad(self.yaw)
    local forward = vec3(-math.sin(yaw), 0, -math.cos(yaw))
    local right = vec3(math.cos(yaw), 0, -math.sin(yaw))
    local move = vec3(0, 0, 0)
    if input.key("W") then move = move + forward end
    if input.key("S") then move = move - forward end
    if input.key("D") then move = move + right end
    if input.key("A") then move = move - right end

    self:update_stance(dt, input.key("C"))
    local running = input.key("LeftShift") and not self.crouched
    local speed = self.crouched and self.crouch_speed or (running and self.run_speed or self.walk_speed)

    local moving = move:length() > 0.01
    if moving then
        move = move:normalized()
        self.entity:look_at(self.entity:get_position() + move, true)
    end
    character.move(self.entity, move * speed)
    if input.key_pressed("Space") and character.is_grounded(self.entity) then
        character.jump(self.entity, 4.5)
    end

    self:update_animation(dt, running)

    -- Footsteps make noise guards can hear: running is loud, sneaking is nearly silent, and the
    -- floor matters (carpet and moss muffle, metal rings). See Scripts/AI/Noises.lua.
    local grounded = character.is_grounded(self.entity)
    if moving and grounded then
        self.step_timer = self.step_timer - dt
        if self.step_timer <= 0 then
            local gait = self.crouched and "crouch" or (running and "run" or "walk")
            self.step_timer = Noises.footsteps[gait].interval
            local loudness, material = Noises.footstep(self.entity, gait)
            self.last_step = { loudness = loudness, material = material }
        end
    end

    -- Landing from a jump or a fall is loud in proportion to the falling speed.
    local velocity = character.get_velocity(self.entity)
    if not grounded then
        self.airborne = true
        self.fall_speed = math.max(self.fall_speed, -velocity.y)
    elseif self.airborne then
        local loudness = Noises.landing(self.entity, self.fall_speed)
        if loudness > 0 then self.last_step = { loudness = loudness, material = "landing" } end
        self.airborne = false
        self.fall_speed = 0
    end
end

-- Debug tool for tuning guards: make a noise of a chosen loudness where the camera aims.
function Player:update_noise_probe()
    if not self.noise_probe then return end
    if input.key_pressed("Minus") then self.probe_loudness = math.max(self.probe_loudness - 1, 1) end
    if input.key_pressed("Equal") then self.probe_loudness = math.min(self.probe_loudness + 1, 10) end
    if input.key_pressed("G") and self.aim_origin then
        local hit = physics.raycast(self.aim_origin, self.aim_direction, 40, self.entity)
        local point = hit and hit.point or (self.aim_origin + self.aim_direction * 10)
        ai.emit_noise(point, self.probe_loudness, self.entity, "Probe")
        audio.play_at("Audio/Clang.wav", point, self.probe_loudness / 10)
    end
end

-- Picks the clip from stance and movement; the playback rate follows the ground speed (the
-- clips' stride is short for the model's proportions, so it is clamped to keep motion natural).
function Player:update_animation(dt, running)
    if not self.model then return end
    self.shoot_timer = math.max(self.shoot_timer - dt, 0)
    if self.shoot_timer > 0 then return end -- let the shot play out
    local velocity = character.get_velocity(self.entity)
    local speed = vec3(velocity.x, 0, velocity.z):length()
    local moving = speed > 0.2
    local clip, reference = "Idle", nil
    if not character.is_grounded(self.entity) then
        clip = "Jump_Idle"
    elseif self.crouched then
        if moving then clip, reference = "Crouch_Walk", 0.8 else clip = "Crouch_Idle" end
    elseif moving then
        if running then clip, reference = "Running_A", 3.0 else clip, reference = "Walking_A", 1.6 end
    end
    anim.play(self.model, clip, 0.2, true)
    anim.set_speed(self.model, reference and math.max(0.5, math.min(speed / reference, 2.0)) or 1.0)
end

function Player:update_camera()
    if not self.camera then return end
    local yaw, pitch = math.rad(self.yaw), math.rad(self.pitch)
    local look = vec3(-math.sin(yaw) * math.cos(pitch), math.sin(pitch), -math.cos(yaw) * math.cos(pitch))
    local right = vec3(math.cos(yaw), 0, -math.sin(yaw))
    local pivot = self.entity:get_position() + vec3(0, self.visual_height + 0.15, 0) + right * 0.5

    -- Pull the camera in when a wall is behind the player.
    local distance = self.camera_distance
    local hit = physics.raycast(pivot, -look, distance, self.entity)
    if hit then distance = math.max(hit.distance - 0.2, 0.3) end

    self.camera:set_position(pivot - look * distance)
    self.camera:look_at(pivot + look * 10)
    self.aim_origin = pivot
    self.aim_direction = look
end

function Player:update_arrows(dt)
    -- Bodies of freshly spawned arrows exist one frame later: launch them then.
    if self.pending_arrow then
        if self.pending_arrow.entity:valid() then
            physics.set_velocity(self.pending_arrow.entity, self.pending_arrow.velocity)
        end
        self.pending_arrow = nil
    end

    if input.key_pressed("1") then self.arrow = 1 end
    if input.key_pressed("2") then self.arrow = 2 end
    self.cooldown = math.max(self.cooldown - dt, 0)
    if input.mouse_pressed("Left") and self.cooldown <= 0 and self.aim_origin then
        local origin = self.aim_origin + self.aim_direction * 0.8
        local arrow = scene.instantiate(ARROWS[self.arrow].prefab, origin)
        arrow:look_at(origin + self.aim_direction)
        self.pending_arrow = { entity = arrow, velocity = self.aim_direction * self.arrow_speed }
        if self.model then
            anim.play(self.model, "1H_Ranged_Shoot", 0.05, false)
            anim.set_speed(self.model, 1.5)
            self.shoot_timer = 0.45
        end
        Noises.emit("Bow", origin, self.entity)
        self.cooldown = 0.6
    end
end

function Player:draw_hud(dt)
    local target = self.entity:get("PerceptionTarget")
    local w, h = ui.width(), ui.height()
    LightGem.draw(LightGem.update(self.gem, target.LightGem or 0, dt), w / 2, h - 50)

    local arrow = ARROWS[self.arrow]
    ui.text(20, h - 40, arrow.name .. " arrow  [1/2]", 2, arrow.color)
    if self.crouched then ui.text(w / 2 + 70, h - 58, "crouching", 2, { 0.7, 0.7, 0.7, 1 }) end
    if self.noise_probe then
        local step = self.last_step
        local floor = Noises.surface_under(self.entity)
        ui.text(20, h - 70, string.format("floor %s   last step %s", floor,
            step and string.format("%.1f/10 (%s)", step.loudness, step.material) or "-"), 1.5, { 0.7, 0.7, 0.7, 1 })
        ui.text(20, h - 92, string.format("noise probe %d/10  [- =] G", self.probe_loudness), 1.5, { 0.7, 0.7, 0.7, 1 })
    end
end

function Player:on_update(dt)
    if input.key_pressed("Escape") then input.set_cursor_locked(false) end
    if input.mouse_pressed("Left") then input.set_cursor_locked(true) end
    self:update_look()
    self:update_movement(dt)
    self:update_camera()
    self:update_arrows(dt)
    self:update_noise_probe()
    self:draw_hud(dt)
end

return Player
