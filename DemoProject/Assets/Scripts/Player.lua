-- Third-person thief: movement, over-the-shoulder camera, footstep noise, arrows and HUD.
--
-- Controls (CONTROLS below; F1 shows them in the game):
--           WASD move, Shift run, C crouch (press to crouch, again to stand: crouch_toggle), Space jump, mouse look,
--           1 = water bolt, 2 = fire bolt, 3 = gas bolt, 4 = moss bolt,
--           right mouse (held) = aim the crossbow: a dot marks the aim, left mouse shoots,
--           Q / E (held) = lean left / right round a corner, standing or crouching still (a movement key ends it),
--           F = use (take loot, pick a pocket from behind, pick up / drop a body, or whatever the
--               object offers: on_use / use_prompt), Tab = objectives,
--           V = blackjack (knocks out an NPC from behind who is not on alert), Esc = release the mouse.
-- Left mouse means one thing while aiming (aim_action: shoot) and another without (plain_action:
-- nothing yet), so other tools can claim either.
-- Noise probe (when the noise_probe property is on): - / = pick a loudness, G makes that noise
-- where you aim. The HUD shows the floor under you and how loud your last step was.
--
-- Every key and mouse read goes through self.controls: the keyboard (the input table itself) or, when the game
-- value playtest names a route (--set playtest=...), the agent playtester's virtual keyboard
-- (Scripts/Playtest/Bot.lua, docs/Playtest.md). Either way the thief moves, sounds and shows himself through the
-- same code.

local Compass = require("Scripts/Compass")
local LightGem = require("Scripts/LightGem")
local Mission = require("Scripts/Mission/Mission")
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
        north_yaw = 0.0,   -- the compass: yaw (degrees) of the direction this level calls north
        help_seconds = 15, -- how long the controls stay up when the level starts (F1 toggles them)
        crouch_toggle = true, -- C crouches and C again stands up (running stands up too); false: crouch while C is held
        crosshair = true,     -- a small dot at the screen centre, where F (use) and the crossbow aim
    }
}

local ARROWS = {
    { name = "Water", prefab = "Prefabs/WaterArrow.pprefab", color = { 0.4, 0.7, 1.0, 1.0 } },
    { name = "Fire", prefab = "Prefabs/FireArrow.pprefab", color = { 1.0, 0.55, 0.2, 1.0 } },
    { name = "Gas", prefab = "Prefabs/GasArrow.pprefab", color = { 0.55, 0.9, 0.4, 1.0 } },
    { name = "Moss", prefab = "Prefabs/MossArrow.pprefab", color = { 0.45, 0.65, 0.3, 1.0 } },
}

-- The controls panel (F1).
local CONTROLS = {
    { "W A S D", "move" }, { "Shift", "run" }, { "C", "crouch / stand up" }, { "Space", "jump" },
    { "Right mouse", "aim the crossbow" }, { "Left mouse", "shoot (while aiming)" }, { "1 - 4", "water / fire / gas / moss bolt" },
    { "Q / E", "lean left / right (standing still)" },
    { "F", "use, take, open, pick up a body" }, { "V", "blackjack (from behind)" }, { "Tab", "objectives" },
    { "Esc", "release the mouse" }, { "F1", "show / hide this" },
}

local CAMERA_RADIUS = 0.2  -- the camera's clearance from walls: wider than its near plane
local SHOULDER = 0.5       -- the over-the-shoulder offset to the right

local REACH = 2.2          -- metres: how far use (F) and the blackjack (V) reach

local LEAN = 0.45          -- metres the head leans out standing (less crouched)
local LEAN_RADIUS = 0.15   -- the head's clearance from walls while leaning
local LEAN_SPEED = 10      -- how fast the lean eases in and out (per second)
local CARRY_SLOWDOWN = 0.6 -- speed multiplier with a body on the shoulders
local CARRY_CENTER = 0.8   -- metres from a fallen body's feet to its middle

local STANDING = {
    vec3(0, 1.70, 0), vec3(-0.22, 1.45, 0), vec3(0.22, 1.45, 0), vec3(0, 1.15, 0),
    vec3(-0.2, 0.9, 0), vec3(0.2, 0.9, 0), vec3(0, 0.45, 0),
}

function Player:on_create()
    -- Yaw from the facing direction (Euler angles are ambiguous for some rotations).
    local facing = self.entity:forward()
    self.yaw = math.deg(math.atan(-facing.x, -facing.z))
    self.pitch = -10
    self.arrow = 1
    self.step_timer = 0
    self.cooldown = 0
    self.crouched = false
    self.want_crouch = false -- what the player asked for (the capsule follows when there is room)
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
    self.carrying = nil     -- body on the shoulders: { entity, points }
    self.swing_cooldown = 0
    self.camera = scene.find("Camera")
    self.aiming = false
    self.boom = self.camera_distance -- current camera distance (eases back out after a wall)
    self.lean = 0           -- metres the head leans right of the view (negative: left)
    self.leaning = false
    -- The height and lean the silhouette points were last built for: the scene's own points stand until either
    -- changes.
    self.silhouette_key = string.format("%.3f %.3f", self.stand_height, 0)
    -- The model's own yaw under the player (it faces -Z like him): leaning rolls it on top of that. Read from its
    -- forward, not its Euler angles, which may come back in another but equal form (180, 0, 180).
    if self.model then
        local f = self.model:forward()
        self.model_yaw = math.deg(math.atan(-f.x, -f.z)) - self.yaw
    end
    self.help_time = self.help_seconds
    self.controls = input
    local route = game.get("playtest", nil)
    if route then self.controls = require("Scripts/Playtest/Bot").attach(self, route) end
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
end

-- What guards see of him: the silhouette points follow his height (crouched: fewer, lower points, harder
-- to spot against bright backgrounds) and his lean (the head and shoulders out past the corner, the feet
-- where they stand). Rebuilt only when either changes.
function Player:update_silhouette()
    local key = string.format("%.3f %.3f", self.visual_height, self.lean)
    if key == self.silhouette_key then return end
    self.silhouette_key = key
    local scale = self.visual_height / self.stand_height
    local top = STANDING[1].y
    local points = {}
    for i, p in ipairs(STANDING) do
        points[i] = vec3(p.x + self.lean * p.y / top, p.y * scale, p.z)
    end
    self.entity:set("PerceptionTarget", { SilhouettePoints = points })
end

-- Leaning (Q left, E right, held) puts his head out past a corner: the eye he sees with (the fog of war),
-- the camera, the silhouette guards see and the model all move sideways, square to where the camera looks.
-- Only standing or crouching still, on the ground, hands free; a movement key ends it. A wall at the side
-- stops the head at once; easing back out is gradual.
function Player:update_lean(dt, wants_to_move)
    local want = 0
    if not wants_to_move and not self.busy and not self.carrying and character.is_grounded(self.entity) then
        if self.controls.key("Q") then want = want - 1 end
        if self.controls.key("E") then want = want + 1 end
    end
    local target = want * LEAN * self.visual_height / self.stand_height
    if target ~= 0 then
        local yaw = math.rad(self.yaw)
        local side = vec3(math.cos(yaw), 0, -math.sin(yaw)) * (target > 0 and 1 or -1)
        local eye = self.entity:get_position() + vec3(0, self.visual_height - 0.15, 0)
        local hit = physics.spherecast(eye, side, LEAN_RADIUS, math.abs(target), self.entity)
        if hit then target = (target > 0 and 1 or -1) * hit.distance end
    end
    if math.abs(target) < math.abs(self.lean) and target * self.lean >= 0 and want ~= 0 then
        self.lean = target -- a wall: in at once
    else
        self.lean = self.lean + (target - self.lean) * math.min(dt * LEAN_SPEED, 1)
    end
    if math.abs(self.lean) < 0.005 then self.lean = 0 end
    self.leaning = self.lean ~= 0

    if self.entity:has("FogOfWar") then self.entity:set("FogOfWar", { Lean = self.lean }) end
    if self.model and self.model_yaw then
        -- The body tilts from the feet so the head ends up self.lean to the side.
        local tilt = math.deg(math.asin(math.max(-0.5, math.min(0.5, self.lean / math.max(self.visual_height, 0.5)))))
        self.model:set_rotation(vec3(0, self.model_yaw, -tilt))
    end
end

function Player:update_look()
    local dx, dy = self.controls.mouse_delta()
    self.yaw = self.yaw - dx * self.mouse_sensitivity
    self.pitch = math.max(-70, math.min(45, self.pitch - dy * self.mouse_sensitivity))
end

function Player:update_movement(dt)
    local yaw = math.rad(self.yaw)
    local forward = vec3(-math.sin(yaw), 0, -math.cos(yaw))
    local right = vec3(math.cos(yaw), 0, -math.sin(yaw))
    local move = vec3(0, 0, 0)
    if not self.busy then -- busy hands (picking a lock): W A S D mean something else
        if self.controls.key("W") then move = move + forward end
        if self.controls.key("S") then move = move - forward end
        if self.controls.key("D") then move = move + right end
        if self.controls.key("A") then move = move - right end
    end

    -- Crouching: a toggle by default (C down, C again up; setting off at a run stands him up), or held.
    if self.crouch_toggle then
        if self.controls.key_pressed("C") then self.want_crouch = not self.want_crouch end
        if self.controls.key("LeftShift") and move:length() > 0.01 and not self.carrying then self.want_crouch = false end
    else
        self.want_crouch = self.controls.key("C")
    end
    self:update_stance(dt, self.want_crouch)
    self:update_lean(dt, move:length() > 0.01)
    self:update_silhouette()
    local running = self.controls.key("LeftShift") and not self.crouched and not self.carrying
    local speed = self.crouched and self.crouch_speed or (running and self.run_speed or self.walk_speed)
    if self.carrying then speed = speed * CARRY_SLOWDOWN end

    local moving = move:length() > 0.01
    if moving then move = move:normalized() end
    if self.aiming or self.leaning then -- facing the aim (side-stepping) or the way he leans out
        self.entity:look_at(self.entity:get_position() + forward, true)
    elseif moving then
        self.entity:look_at(self.entity:get_position() + move, true)
    end
    character.move(self.entity, move * speed)
    if self.controls.key_pressed("Space") and character.is_grounded(self.entity) then
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
    if self.controls.key_pressed("Minus") then self.probe_loudness = math.max(self.probe_loudness - 1, 1) end
    if self.controls.key_pressed("Equal") then self.probe_loudness = math.min(self.probe_loudness + 1, 10) end
    if self.controls.key_pressed("G") and self.aim_origin then
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

-- The over-the-shoulder camera never sees through walls: a sphere wider than the camera's near
-- plane is swept from the head out to the shoulder (a wall on the right pulls the shoulder in),
-- then back along the boom (a wall behind pulls the camera in). Pulling in is instant, easing
-- back out is gradual, so the view does not pump along a rough wall.
function Player:update_camera(dt)
    if not self.camera then return end
    local yaw, pitch = math.rad(self.yaw), math.rad(self.pitch)
    local look = vec3(-math.sin(yaw) * math.cos(pitch), math.sin(pitch), -math.cos(yaw) * math.cos(pitch))
    local right = vec3(math.cos(yaw), 0, -math.sin(yaw))
    local head = self.entity:get_position() + vec3(0, self.visual_height + 0.15, 0) + right * self.lean

    local shoulder = SHOULDER
    local side = physics.spherecast(head, right, CAMERA_RADIUS, SHOULDER, self.entity)
    if side then shoulder = side.distance end
    local pivot = head + right * shoulder

    local wanted = self.camera_distance
    local behind = physics.spherecast(pivot, -look, CAMERA_RADIUS, wanted, self.entity)
    if behind then wanted = behind.distance end
    if wanted < self.boom then
        self.boom = wanted
    else
        self.boom = math.min(wanted, self.boom + (dt or 0) * 4.0)
    end

    self.camera:set_position(pivot - look * self.boom)
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

    if self.controls.key_pressed("1") then self.arrow = 1 end
    if self.controls.key_pressed("2") then self.arrow = 2 end
    if self.controls.key_pressed("3") then self.arrow = 3 end
    if self.controls.key_pressed("4") then self.arrow = 4 end
    if self.carrying then return end -- hands full
    self.cooldown = math.max(self.cooldown - dt, 0)
    if self.controls.mouse_pressed("Left") then
        if self.aiming then self:aim_action() else self:plain_action() end
    end
end

-- Left mouse while aiming: shoot a bolt. Returns the bolt (nil while reloading).
function Player:aim_action()
    if self.cooldown > 0 or not self.aim_origin then return nil end
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
    return arrow
end

-- Left mouse without aiming: free for a future tool or attack.
function Player:plain_action() end

function Player:draw_hud(dt)
    local target = self.entity:get("PerceptionTarget")
    local w, h = ui.width(), ui.height()
    LightGem.draw(LightGem.update(self.gem, target.LightGem or 0, dt), w / 2, h - 50)

    local arrow = ARROWS[self.arrow]
    ui.text(20, h - 44, arrow.name .. " bolt  [1-4]", 2.4, arrow.color)
    local prompt = not self.busy and self:interaction_prompt() or nil
    if self.aiming then -- the dot: the bolt flies from the camera's line through the screen centre
        ui.rect(w / 2 - 4, h / 2 - 4, 8, 8, { 0, 0, 0, 0.6 })
        ui.rect(w / 2 - 3, h / 2 - 3, 6, 6, { 1, 1, 1, 0.9 })
    elseif self.crosshair and not self.busy then
        -- Always there, small and faint; brighter and warm when F would do something.
        local color = prompt and { 1.0, 0.85, 0.4, 0.95 } or { 1, 1, 1, 0.55 }
        ui.rect(w / 2 - 3, h / 2 - 3, 6, 6, { 0, 0, 0, 0.4 })
        ui.rect(w / 2 - 2, h / 2 - 2, 4, 4, color)
    end
    if prompt then ui.text(w / 2 - #prompt * 7.2, h / 2 + 40, prompt, 2.4, { 1, 1, 1, 0.85 }) end
    if self.crouched then ui.text(w / 2 + 70, h - 58, "crouching", 2, { 0.7, 0.7, 0.7, 1 }) end
    if self.leaning then ui.text(w / 2 + 70, h - 34, self.lean < 0 and "leaning left" or "leaning right", 2, { 0.7, 0.7, 0.7, 1 }) end
    if self.noise_probe then
        local step = self.last_step
        local floor = Noises.surface_under(self.entity)
        ui.text(20, h - 76, string.format("floor %s   last step %s", floor,
            step and string.format("%.1f/10 (%s)", step.loudness, step.material) or "-"), 1.8, { 0.7, 0.7, 0.7, 1 })
        ui.text(20, h - 100, string.format("noise probe %d/10  [- =] G", self.probe_loudness), 1.8, { 0.7, 0.7, 0.7, 1 })
    end
    Compass.draw(w - 20, 16, Compass.heading(self.yaw, self.north_yaw))
    self:draw_controls(dt)
end

-- The controls panel: up for a while when the level starts, F1 toggles it.
function Player:draw_controls(dt)
    if self.controls.key_pressed("F1") then self.help_time = self.help_time > 0 and 0 or math.huge end
    self.help_time = math.max(self.help_time - dt, 0)
    local h = ui.height()
    if self.help_time <= 0 then
        ui.text(20, h - 124, "[F1] controls", 1.8, { 0.7, 0.7, 0.7, 0.8 })
        return
    end
    local x, y, line = 20, 220, 26
    ui.rect(x - 10, y - 12, 470, #CONTROLS * line + 20, { 0, 0, 0, 0.6 })
    for i, entry in ipairs(CONTROLS) do
        ui.text(x, y + (i - 1) * line, entry[1], 2, { 1.0, 0.85, 0.4, 1 })
        ui.text(x + 160, y + (i - 1) * line, entry[2], 2, { 0.9, 0.9, 0.9, 1 })
    end
end

function Player:on_update(dt)
    if self.controls.tick then self.controls.tick(dt) end -- the playtester decides what to press this frame
    if Mission.over() then -- escaped or failed: the summary is up
        character.move(self.entity, vec3(0, 0, 0))
        return
    end
    if self.controls.key_pressed("Escape") then input.set_cursor_locked(false) end
    if self.controls.mouse_pressed("Left") then input.set_cursor_locked(true) end
    self:update_look()
    self.aiming = self.controls.mouse_button("Right") and not self.busy and not self.carrying
    self:update_movement(dt)
    self:update_camera(dt)
    if self.busy then
        -- Both hands on a task (a lock): no shooting or using until it is over.
        if not self.busy:update(self, dt) then self.busy = nil end
    else
        self:update_arrows(dt)
        self:update_hands(dt)
    end
    self:update_noise_probe()
    self:draw_hud(dt)
end

-- ------------------------------------------------------------------ hands: use, blackjack, bodies

local function flat(v) return vec3(v.x, 0, v.z) end

-- The NPC (with Perception) nearest in front of the player within reach, matching the filter.
function Player:npc_in_front(filter)
    local position = self.entity:get_position()
    local forward = flat(self.entity:forward()):normalized()
    local best, best_distance = nil, REACH
    for _, npc in ipairs(scene.entities_with("Perception")) do
        local offset = flat(npc:get_position() - position)
        local distance = offset:length()
        if npc ~= self.entity and distance < best_distance and (distance < 0.3 or offset:normalized():dot(forward) > 0.5) and filter(npc) then
            best, best_distance = npc, distance
        end
    end
    return best
end

function Player:body_in_reach()
    return self:npc_in_front(function(npc) return ai.vital_state(npc) ~= "Awake" end)
end

-- The object under the crosshair that offers on_use (itself or an ancestor with such a script).
function Player:usable_in_reach()
    if not self.aim_origin then return nil end
    local hit = physics.raycast(self.aim_origin, self.aim_direction, REACH + 3.2, self.entity)
    if not hit or not hit.entity or hit.point:distance(self.entity:get_position()) > REACH + 0.8 then return nil end
    local entity = hit.entity
    while entity do
        local instance = entity:has("Script") and entity:script() or nil
        if instance and type(instance.on_use) == "function" then return entity, instance end
        entity = entity:parent()
    end
    return nil
end

-- Blackjack target: an awake NPC in front, whose back is turned and who is not on alert.
function Player:blackjack_target()
    return self:npc_in_front(function(npc) return ai.vital_state(npc) == "Awake" end)
end

local function from_behind(attacker, npc)
    local to_attacker = flat(attacker:get_position() - npc:get_position()):normalized()
    return flat(npc:forward()):normalized():dot(to_attacker) < -0.2
end

-- Loot on the belt of an NPC in front (a purse, a key): { npc, loot } or nil. Only from behind
-- while he is up; anyone lying down can be searched.
function Player:pocket_in_reach()
    local npc = self:npc_in_front(function(npc)
        return ai.vital_state(npc) ~= "Awake" or from_behind(self.entity, npc)
    end)
    if not npc then return nil end
    for _, child in ipairs(npc:children()) do
        local instance = child:has("Script") and child:script() or nil
        if instance and instance.pickpocket and not instance.taken then return { npc = npc, loot = instance } end
    end
    return nil
end

-- Lifting it unnoticed works while he is calm (Unaware or merely Suspicious) or down; a guard
-- who is looking for someone feels the hand and turns on the thief.
function Player:pick_pocket(pocket)
    local state = ai.state(pocket.npc)
    if ai.vital_state(pocket.npc) == "Awake" and state ~= "Unaware" and state ~= "Suspicious" then
        ai.notify(pocket.npc, self.entity:get_position(), 9.5, "Theft")
        return "noticed"
    end
    pocket.loot:take()
    return "pickpocket"
end

function Player:interaction_prompt()
    if self.carrying then return "[F] put the body down" end
    local pocket = self:pocket_in_reach()
    if pocket then return "[F] " .. pocket.loot.use_prompt end
    if self:body_in_reach() then return "[F] pick up the body" end
    local _, usable = self:usable_in_reach()
    if usable then return "[F] " .. (usable.use_prompt or "use") end
    local npc = self:blackjack_target()
    if npc and from_behind(self.entity, npc) then return "[V] knock out" end
    return nil
end

-- A task that keeps both hands busy (Scripts/World/Lockpick.lua): an object whose update(player, dt)
-- is called every frame and returns false once it is over. The thief stands still meanwhile.
function Player:start_busy(task)
    self.busy = task
end

-- F: drop the body, pick one up, or use what is in front. Returns what happened (for tests).
function Player:use()
    if self.carrying then return self:drop_body() end
    local pocket = self:pocket_in_reach()
    if pocket then return self:pick_pocket(pocket) end
    local body = self:body_in_reach()
    if body then return self:pick_up(body) end
    local entity, usable = self:usable_in_reach()
    if usable then
        usable:on_use(self.entity)
        return "used"
    end
    return nil
end

function Player:pick_up(body)
    -- Over the shoulders it is not a separate body anyone can find; it becomes one again when put down.
    -- A limp body (ragdoll) is lifted in the carrying pose of its animation.
    if physics.ragdoll_active(body) then physics.ragdoll_stop(body) end
    self.carrying = { entity = body, target = body:get("PerceptionTarget") }
    if self.carrying.target then body:remove("PerceptionTarget") end
    body:set_parent(self.entity)
    -- The body lies along its own +Z (it fell backwards): turned across the player and shifted so
    -- its middle rests on his shoulders.
    body:set_rotation(vec3(0, 90, 0))
    body:set_local_position(vec3(-CARRY_CENTER, 1.25, 0.05))
    Noises.emit("Rustle", self.entity:get_position(), self.entity)
    return "carry"
end

function Player:drop_body()
    local body = self.carrying.entity
    local target = self.carrying.target
    self.carrying = nil
    if not body:valid() then return "drop" end
    body:set_parent(nil)
    local forward = flat(self.entity:forward()):normalized()
    local spot = self.entity:get_position() + forward * 0.9
    local floor = physics.raycast(spot + vec3(0, 1, 0), vec3(0, -1, 0), 3, self.entity)
    body:set_position(floor and floor.point or spot)
    body:set_rotation(vec3(0, self.yaw, 0))
    if target then body:add("PerceptionTarget", target) end
    -- Put down, it slumps where it lands (a ragdoll again).
    if body:has("Ragdoll") then physics.ragdoll_start(body) end
    Noises.emit("BodyDrop", body:get_position(), self.entity)
    return "drop"
end

-- V: knock out an NPC from behind if he is not on alert; otherwise he notices the attack.
function Player:blackjack()
    if self.swing_cooldown > 0 or self.carrying then return nil end
    self.swing_cooldown = 0.8
    if self.model then
        anim.play(self.model, "1H_Melee_Attack_Chop", 0.05, false)
        self.shoot_timer = 0.5
    end
    local npc = self:blackjack_target()
    if not npc then return "miss" end
    local state = ai.state(npc)
    if from_behind(self.entity, npc) and state ~= "Searching" and state ~= "Alarmed" then
        ai.knock_out(npc)
        Noises.emit("Blackjack", npc:get_position() + vec3(0, 1.6, 0), self.entity)
        return "knockout"
    end
    -- Seen coming or already on guard: he blocks and raises the alarm.
    ai.notify(npc, self.entity:get_position(), 9.5, "Attack")
    Noises.emit("Blackjack", npc:get_position() + vec3(0, 1.6, 0), self.entity)
    return "blocked"
end

function Player:update_hands(dt)
    self.swing_cooldown = math.max(self.swing_cooldown - dt, 0)
    if self.controls.key_pressed("F") then self:use() end
    if self.controls.key_pressed("V") then self:blackjack() end
end

return Player
