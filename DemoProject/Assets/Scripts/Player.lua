-- Third-person thief: movement, over-the-shoulder camera, footstep noise, arrows and HUD.
--
-- Controls: WASD move, Shift run, C crouch, Space jump, mouse look,
--           1 = water arrow, 2 = fire arrow, left mouse = shoot, Esc = release the mouse.

local Player = {
    Properties = {
        walk_speed = 2.4,
        run_speed = 5.0,
        crouch_speed = 1.2,
        mouse_sensitivity = 0.12,
        camera_distance = 3.2,
        arrow_speed = 32.0,
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
    self.pending_arrow = nil
    self.camera = scene.find("Camera")
    input.set_cursor_locked(true)
end

function Player:set_crouched(crouched)
    if crouched == self.crouched then return end
    self.crouched = crouched
    -- A crouching thief has a smaller silhouette: fewer points against bright backgrounds.
    self.entity:set("PerceptionTarget", { SilhouettePoints = scaled_silhouette(crouched and 0.6 or 1.0) })
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

    self:set_crouched(input.key("C"))
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

    -- Footsteps make noise guards can hear: running is loud, sneaking is nearly silent.
    if moving and character.is_grounded(self.entity) then
        self.step_timer = self.step_timer - dt
        if self.step_timer <= 0 then
            self.step_timer = running and 0.3 or 0.5
            local radius = self.crouched and 1.0 or (running and 12.0 or 4.0)
            local feet = self.entity:get_position()
            ai.emit_noise(feet, radius, self.entity)
            audio.play_at("Audio/Footstep.wav", feet, self.crouched and 0.15 or (running and 0.7 or 0.35))
        end
    end
end

function Player:update_camera()
    if not self.camera then return end
    local yaw, pitch = math.rad(self.yaw), math.rad(self.pitch)
    local look = vec3(-math.sin(yaw) * math.cos(pitch), math.sin(pitch), -math.cos(yaw) * math.cos(pitch))
    local right = vec3(math.cos(yaw), 0, -math.sin(yaw))
    local pivot = self.entity:get_position() + vec3(0, self.crouched and 1.1 or 1.6, 0) + right * 0.45

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
        audio.play_at("Audio/Bow.wav", origin, 0.6)
        ai.emit_noise(origin, 3.0, self.entity)
        self.cooldown = 0.6
    end
end

function Player:draw_hud()
    local target = self.entity:get("PerceptionTarget")
    local exposure = math.min(target.Exposure or 0, 3.0) / 3.0
    local light = math.min((target.LightLevel or 0) * 4.0, 1.0)

    -- Visibility gem: how exposed the silhouette is to the most alert guard.
    local h = ui.height()
    ui.rect(20, h - 70, 204, 24, { 0, 0, 0, 0.6 })
    ui.rect(22, h - 68, 200 * exposure, 20, { 0.3 + exposure * 0.7, 0.8 - exposure * 0.6, 0.2, 0.9 })
    ui.text(26, h - 66, "SILHOUETTE", 2, { 1, 1, 1, 0.9 })
    ui.rect(20, h - 40, 204, 10, { 0, 0, 0, 0.6 })
    ui.rect(22, h - 38, 200 * light, 6, { 1.0, 0.9, 0.6, 0.9 })

    local arrow = ARROWS[self.arrow]
    ui.text(240, h - 66, arrow.name .. " arrow  [1/2]", 2, arrow.color)
    if self.crouched then ui.text(240, h - 40, "crouching", 2, { 0.7, 0.7, 0.7, 1 }) end
end

function Player:on_update(dt)
    if input.key_pressed("Escape") then input.set_cursor_locked(false) end
    if input.mouse_pressed("Left") then input.set_cursor_locked(true) end
    self:update_look()
    self:update_movement(dt)
    self:update_camera()
    self:update_arrows(dt)
    self:draw_hud()
end

return Player
