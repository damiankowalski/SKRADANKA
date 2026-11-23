-- The agent playtester (docs/Playtest.md): a deterministic bot that plays the thief along a route and records
-- what the guards made of him, one JSON line at a time (playtest.record; PenumbraRuntime --playtest-out).
--
-- It is a virtual keyboard: Player.lua reads every key and mouse move through self.controls, and with the game
-- value playtest set (--set playtest=<route>) that is this bot instead of the input table. So the thief moves,
-- crouches, leans, makes footsteps and shows his silhouette through exactly the code a person plays with.
--
--   --set playtest=Playtest/Stealth/West     a route module (Assets/Playtest/...; format in docs/Playtest.md)
--   --set playtest=baseline                  no file: crouch along the shortest navmesh path, taking the
--                                            nearest loot until the mission's target is met, then the exit
--   --set playtest_seed=N                    the bot's own timing jitter (the runner passes the scene seed)
--   --set playtest_detail=false              skip the ai.visibility breakdown in samples
--
-- A route with survive = true (a lab scenario: stand somewhere for a while) succeeds as "undetected" when its steps
-- or its time limit run out with no alarm.
--
-- A run ends when the thief escapes, a guard is Alarmed, the mission fails, the time limit passes or the bot
-- cannot get on (stuck). It then records the result and quits the runtime (scene.quit).

local Mission = require("Scripts/Mission/Mission")

local Bot = {}
Bot.__index = Bot

Bot.VERSION = 1
local SAMPLE_EVERY = 6      -- frames between samples (10 Hz at 60 Hz)
local TURN_RATE = 540       -- degrees per second the bot turns the view at most
local WALK_TURN = 50        -- he only walks while facing within this many degrees of where he goes
local CORNER = 0.3          -- metres: a path corner counts as reached this close
local ARRIVE = 0.35         -- metres: a waypoint counts as reached this close
local REACH = 1.4           -- metres from an object (horizontally) to use it
local STUCK_TIME = 4.0      -- seconds without 0.25 m of progress: repath once, then give up
local USE_TIMEOUT = 8.0     -- seconds to aim at and use something
local PICK_INTERVAL = 0.3   -- seconds between lockpick letters
local DEFAULT_TIME_LIMIT = 300
local START_DELAY = 2.0     -- seconds of start delay at jitter 1 (uniform 0..2)
local WAIT_SPREAD = 0.2     -- waits vary by +-20% at jitter 1

local RANK = { Unaware = 0, Suspicious = 1, Investigating = 2, Searching = 3, Alarmed = 4 }

-- ------------------------------------------------------------------ helpers

local function round(x, digits)
    local scale = 10 ^ (digits or 3)
    return math.floor(x * scale + 0.5) / scale
end

local function vec(v) return { round(v.x, 2), round(v.y, 2), round(v.z, 2) } end

local function flat(v) return vec3(v.x, 0, v.z) end

local function flat_distance(a, b) return flat(a - b):length() end

local function wrap(degrees)
    degrees = degrees % 360
    if degrees > 180 then degrees = degrees - 360 end
    return degrees
end

local function yaw_towards(from, to)
    local d = to - from
    return math.deg(math.atan(-d.x, -d.z))
end

-- The bot's own random numbers (xorshift32), so its jitter never touches the game's math.random stream.
local function make_random(seed)
    local state = (math.floor(seed) * 2654435761 + 1) % 4294967296
    if state == 0 then state = 0x9E3779B9 end
    return function()
        state = state ~ ((state << 13) & 0xFFFFFFFF)
        state = state ~ (state >> 17)
        state = state ~ ((state << 5) & 0xFFFFFFFF)
        return state / 4294967296
    end
end

local function entity_or_point(target)
    if type(target) == "userdata" and target.valid then
        return target:valid() and target:get_position() or nil, target
    end
    if type(target) == "string" then
        local entity = scene.find(target)
        return entity and entity:valid() and entity:get_position() or nil, entity
    end
    return target, nil
end

-- A light's display name: the flames of torches are called Flame, their torch has the useful name.
local function light_name(entity)
    local name = entity:get_name()
    local parent = entity:parent()
    if parent and (name == "Flame" or name == "Light") then return parent:get_name() end
    return name
end

-- ------------------------------------------------------------------ routes

-- Loot lying about (not on a belt), not taken yet: { entity, instance }.
local function loose_loot()
    local list = {}
    for _, entity in ipairs(scene.entities_with("Script")) do
        local instance = entity:script()
        if instance and instance.label and instance.value and instance.value > 0 and not instance.pickpocket and not instance.taken then
            list[#list + 1] = { entity = entity, instance = instance }
        end
    end
    return list
end

-- Doors on the way (Scripts/World/Door.lua): the navmesh runs through them, the thief has to open them.
local function find_doors()
    local list = {}
    for _, entity in ipairs(scene.entities_with("Script")) do
        local instance = entity:script()
        if instance and type(instance.is_open) == "function" and type(instance.on_use) == "function" then list[#list + 1] = entity end
    end
    return list
end

local function find_goal()
    for _, entity in ipairs(scene.entities_with("Script")) do
        local instance = entity:script()
        if instance and type(instance.reach) == "function" then return entity end
    end
    return nil
end

-- The baseline: crouched, nearest loot first (by navmesh distance) until the mission's loot target is met, then
-- the exit.
local function baseline_route(start)
    local steps = {}
    local target = 0
    if Mission.current then
        for _, objective in ipairs(Mission.current.objectives) do
            if objective.loot then target = objective.target end
        end
    end
    local position, total = start, 0
    local loot = loose_loot()
    while total < target and #loot > 0 do
        local best, best_distance = nil, math.huge
        for i, item in ipairs(loot) do
            local distance = nav.distance(position, item.entity:get_position())
            if distance and distance < best_distance then best, best_distance = i, distance end
        end
        if not best then break end
        local item = table.remove(loot, best)
        steps[#steps + 1] = { use = item.entity:get_name(), gait = "crouch", label = "take " .. item.instance.label }
        position = item.entity:get_position()
        total = total + item.instance.value
    end
    steps[#steps + 1] = { goal = true, gait = "crouch", label = "escape" }
    return { name = "Baseline", steps = steps }
end

local function load_route(name, start)
    if name == "baseline" then return baseline_route(start) end
    local ok, route = pcall(require, name)
    if not ok or type(route) ~= "table" or type(route.steps) ~= "table" then
        log.error("Playtest: cannot load route '" .. tostring(name) .. "': " .. tostring(route))
        return nil
    end
    return route
end

-- ------------------------------------------------------------------ the bot

function Bot.attach(player, route_name)
    local self = setmetatable({}, Bot)
    self.player = player
    self.route_name = route_name
    self.seed = tonumber(game.get("playtest_seed", 0)) or 0
    self.detail = game.get("playtest_detail", true) ~= false
    self.random = make_random(self.seed)
    self.down, self.pressed = {}, {}
    self.dx, self.dy = 0, 0
    self.frame = 0
    self.started = false
    self.finished = false

    local bot = self
    return {
        key = function(key) return bot.down[key] == true end,
        key_pressed = function(key) return bot.pressed[key] == true end,
        key_released = function() return false end,
        mouse_button = function(button) return bot.down["Mouse" .. button] == true end,
        mouse_pressed = function() return false end,
        mouse_delta = function() return bot.dx, bot.dy end,
        tick = function(dt) bot:tick(dt) end,
        bot = bot,
    }
end

function Bot:record(line) playtest.record(line) end

function Bot:start()
    self.started = true
    self.route = load_route(self.route_name, self.player.entity:get_position())
    -- A lab scenario may place the thief first (start = { at = vec3, yaw = degrees }).
    if self.route and self.route.start then
        self.player.entity:set_position(self.route.start.at)
        if self.route.start.yaw then self.player.yaw = self.route.start.yaw end
    end
    local position = self.player.entity:get_position()
    self.t0 = time.elapsed()
    self.jitter = self.route and (self.route.jitter or 1.0) or 0
    self.time_limit = self.route and (self.route.time_limit or DEFAULT_TIME_LIMIT) or DEFAULT_TIME_LIMIT
    self.guards = {}
    for _, entity in ipairs(scene.entities_with("Perception")) do
        self.guards[#self.guards + 1] = { entity = entity, name = entity:get_name(), state = "Unaware", vital = "Awake" }
    end
    self.lights = {}
    for _, kind in ipairs({ "PointLight", "SpotLight" }) do
        for _, entity in ipairs(scene.entities_with(kind)) do self.lights[#self.lights + 1] = entity end
    end
    self.goal = find_goal()
    self.doors = find_doors()
    self:record({
        type = "run", version = Bot.VERSION, route = self.route and (self.route.name or self.route_name) or self.route_name,
        route_module = self.route_name, seed = self.seed, jitter = self.jitter, time_limit = self.time_limit,
        difficulty = Mission.current and Mission.current.difficulty or nil,
        mission = Mission.current and Mission.current.definition.name or nil,
        steps = self.route and #self.route.steps or 0, guards = self:guard_names(), start = vec(position),
    })
    if not self.route then
        self:finish("error", "route not found: " .. tostring(self.route_name))
        return
    end
    self.step_index = 0
    self.delay = self.random() * START_DELAY * self.jitter
    self:next_step()
end

function Bot:guard_names()
    local names = {}
    for i, guard in ipairs(self.guards) do names[i] = guard.name end
    return names
end

function Bot:now() return time.elapsed() - self.t0 end

-- ------------------------------------------------------------------ steps

function Bot:next_step()
    if self.step then self:close_segment() end
    self.step_index = self.step_index + 1
    self.step = self.route.steps[self.step_index]
    self.path, self.phase, self.timer = nil, nil, 0
    self.progress_at, self.progress_time, self.repathed = nil, 0, false
    if not self.step then return end
    local step = self.step
    if step.gait then self.gait = step.gait end
    if step.wait then
        self.timer = step.wait * (1 + (self.random() * 2 - 1) * WAIT_SPREAD * self.jitter)
    elseif step.hold then
        self.timer = step.hold
    end
    self:open_segment()
end

function Bot:step_label(step)
    if step.label then return step.label end
    if step.to then return "to " .. (type(step.to) == "string" and step.to or "point") end
    if step.use then return "use " .. tostring(step.use) end
    if step.goal then return "escape" end
    if step.wait then return "wait" end
    if step.wait_for then return "wait for " .. step.wait_for end
    if step.lean then return "lean " .. step.lean end
    return "step"
end

function Bot:open_segment()
    self.segment = { index = self.step_index, label = self:step_label(self.step), t0 = round(self:now(), 2), guards = {} }
    for _, guard in ipairs(self.guards) do
        self.segment.guards[guard.name] = { max_b = 0, max_d = 0, max_strength = 0, max_heard = -10, peak_state = "Unaware" }
    end
end

function Bot:close_segment()
    if not self.segment then return end
    local guards = {}
    for _, guard in ipairs(self.guards) do
        local s = self.segment.guards[guard.name]
        guards[guard.name] = { max_b = round(s.max_b), max_d = round(s.max_d), max_strength = round(s.max_strength, 2),
                               max_heard = round(s.max_heard, 2), peak_state = s.peak_state }
    end
    self:record({ type = "segment", index = self.segment.index, label = self.segment.label, t0 = self.segment.t0,
                  t1 = round(self:now(), 2), guards = guards })
    self.segment = nil
end

-- Turn the view towards a yaw (and pitch) at most TURN_RATE per second: returns the yaw still to go.
function Bot:look(yaw, pitch, dt)
    local p = self.player
    local step = TURN_RATE * dt
    local turn = math.max(-step, math.min(step, wrap(yaw - p.yaw)))
    self.dx = -turn / p.mouse_sensitivity
    if pitch then
        local tilt = math.max(-step, math.min(step, pitch - p.pitch))
        self.dy = -tilt / p.mouse_sensitivity
    end
    return math.abs(wrap(yaw - p.yaw) - turn)
end

-- Walk along the navmesh towards a point; true once within `arrive` metres (horizontally).
function Bot:go(target, arrive, dt)
    local position = self.player.entity:get_position()
    if flat_distance(position, target) <= arrive then return true end
    if not self.path then
        local path = nav.path(position, target)
        if not path or #path.points == 0 then
            self:finish("stuck", "no navmesh path to " .. self:step_label(self.step))
            return false
        end
        self.path, self.corner = path.points, 1
    end
    while self.corner < #self.path and flat_distance(position, self.path[self.corner]) <= CORNER do
        self.corner = self.corner + 1
    end
    local corner = self.path[self.corner]
    if self.corner == #self.path and flat_distance(position, corner) <= CORNER then corner = target end
    local left = self:look(yaw_towards(position, corner), -10, dt)
    if left < WALK_TURN then self.down.W = true end

    -- No progress for a while: a closed door in the way gets opened (picked if locked); otherwise look for
    -- another way once, then give up.
    if not self.progress_at or flat_distance(position, self.progress_at) > 0.25 then
        self.progress_at, self.progress_time = position, 0
    else
        self.progress_time = self.progress_time + dt
        if self.progress_time > 0.5 and self:open_door_ahead(position) then return false end
        if self.progress_time > STUCK_TIME then
            if self.repathed then
                self:finish("stuck", "no progress on " .. self:step_label(self.step))
            else
                self.repathed, self.path, self.progress_time = true, nil, 0
            end
        end
    end
    return false
end

-- A closed door within reach: open it as a step of its own, then carry on with this one.
function Bot:open_door_ahead(position)
    for _, door in ipairs(self.doors) do
        if door:valid() and not door:script():is_open() and flat_distance(door:get_position(), position) < 2.0 then
            self.saved = { step = self.step, path = nil }
            self.step = { use = door, label = "open " .. door:get_name() }
            self.phase, self.timer, self.path = nil, 0, nil
            self.progress_at, self.progress_time = nil, 0
            self:record({ type = "door", t = round(self:now(), 2), door = door:get_name(), locked = door:script().locked == true })
            return true
        end
    end
    return false
end

-- Whether a use step is done: loot taken, door open (or the entity gone).
local function used(entity, step)
    if not entity or not entity:valid() then return true end
    local instance = entity:has("Script") and entity:script() or nil
    if not instance then return true end
    if instance.taken ~= nil then return instance.taken end
    if instance.is_light_switch then return instance.lit == (step.lit == true) end -- default: put the lamps out
    if type(instance.is_open) == "function" then return instance:is_open() end
    return false
end

function Bot:target_name(target)
    if type(target) == "userdata" then return target:valid() and target:get_name() or "?" end
    return tostring(target)
end

function Bot:use_step(dt)
    local step = self.step
    local position, entity = entity_or_point(step.use)
    if not position or used(entity, step) then
        if self.phase then self:record({ type = "use", t = round(self:now(), 2), target = self:target_name(step.use), ok = true }) end
        return true
    end
    self.timer = self.timer + dt
    if self.timer > USE_TIMEOUT + (self.phase and 0 or 120) then
        self:record({ type = "use", t = round(self:now(), 2), target = self:target_name(step.use), ok = false })
        return true
    end
    local player = self.player
    if player.busy then -- picking a lock: one letter at a time
        self.pick_timer = (self.pick_timer or 0) - dt
        if self.pick_timer <= 0 and player.busy.current then
            self.pressed[player.busy:current()] = true
            self.pick_timer = PICK_INTERVAL
        end
        self.timer = 0
        return false
    end
    if not self.phase then
        local spot = nav.closest_point(position, 2.0) or position
        if flat_distance(player.entity:get_position(), position) > REACH and not self:go(spot, 0.5, dt) then return false end
        self.phase, self.timer = "aim", 0
    end
    -- Aim the camera's line at it, then press F (again every second until it works).
    local origin = player.aim_origin or (player.entity:get_position() + vec3(0, 1.6, 0))
    local d = position - origin
    local pitch = math.deg(math.asin(math.max(-1, math.min(1, d.y / math.max(d:length(), 1e-3)))))
    local left = self:look(yaw_towards(origin, position), pitch, dt)
    self.use_timer = (self.use_timer or 0) - dt
    if left < 2 and math.abs(pitch - player.pitch) < 3 and self.use_timer <= 0 then
        self.pressed.F = true
        self.use_timer = 1.0
    end
    return false
end

-- Wait until a guard is farther than `away` metres from the thief and walking off (as a careful player does),
-- at most `timeout` seconds (then carry on regardless, recorded).
function Bot:wait_for_step(dt)
    local step = self.step
    local guard = scene.find(step.wait_for)
    if not guard or not guard:valid() or ai.vital_state(guard) ~= "Awake" then return true end
    local distance = guard:get_position():distance(self.player.entity:get_position())
    local leaving = self.last_distance and distance > self.last_distance + 1e-4
    self.last_distance = distance
    self.timer = self.timer + dt
    if distance > (step.away or 10) and (leaving or step.leaving == false) then
        self.last_distance = nil
        return true
    end
    if self.timer > (step.timeout or 60) then
        self:record({ type = "wait", t = round(self:now(), 2), guard = step.wait_for, ok = false })
        self.last_distance = nil
        return true
    end
    return false
end

function Bot:goal_step(dt)
    if not self.goal then
        self:finish("error", "no exit (Goal) in the level")
        return false
    end
    local target = nav.closest_point(self.goal:get_position(), 3.0) or self.goal:get_position()
    if self:go(target, ARRIVE, dt) then
        self.timer = self.timer + dt
        if self.timer > 2.0 then
            local missing = Mission.current and table.concat(Mission.missing(Mission.current), "; ") or ""
            self:finish("incomplete", "at the exit, missing: " .. missing)
        end
    end
    return false
end

function Bot:run_step(dt)
    local step = self.step
    if step.wait then
        self.timer = self.timer - dt
        return self.timer <= 0
    elseif step.wait_for then
        return self:wait_for_step(dt)
    elseif step.lean then
        self.down[step.lean == "left" and "Q" or "E"] = true
        if step.face then self:look(step.face, nil, dt) end
        self.timer = self.timer - dt
        return self.timer <= 0
    elseif step.to then
        local target = entity_or_point(step.to)
        if not target then
            self:finish("error", "waypoint not found: " .. tostring(step.to))
            return false
        end
        return self:go(target, step.arrive or ARRIVE, dt)
    elseif step.use then
        return self:use_step(dt)
    elseif step.goal then
        return self:goal_step(dt)
    elseif step.face then
        return self:look(step.face, -10, dt) < 1
    end
    self:finish("error", "unknown step " .. self.step_index)
    return false
end

-- ------------------------------------------------------------------ watching the guards

function Bot:nearest_light(position)
    local best, best_distance = nil, math.huge
    for _, light in ipairs(self.lights) do
        if light:valid() then
            local distance = light:get_position():distance(position)
            if distance < best_distance then best, best_distance = light, distance end
        end
    end
    if not best then return nil, nil end
    return light_name(best), round(best_distance, 1)
end

function Bot:watch(dt)
    local t = round(self:now(), 3)
    local thief = self.player.entity
    local at = thief:get_position()
    local target = thief:get("PerceptionTarget")
    for _, guard in ipairs(self.guards) do
        local entity = guard.entity
        if entity:valid() then
            local vital = ai.vital_state(entity)
            if vital ~= guard.vital then
                self:record({ type = "down", t = t, guard = guard.name, vital = vital })
                guard.vital = vital
            end
            if vital == "Awake" then
                local perception = entity:get("Perception")
                local b, d = perception.Stimulus, perception.Suspicion
                local reaction = ai.reaction(entity)
                local s = self.segment and self.segment.guards[guard.name]
                if s then
                    s.max_b = math.max(s.max_b, b)
                    s.max_d = math.max(s.max_d, d)
                    s.max_strength = math.max(s.max_strength, reaction.strength)
                    if RANK[reaction.state] > RANK[s.peak_state] then s.peak_state = reaction.state end
                end

                -- Reaction time at the threshold: from the first frame with B >= 1 (a fresh look: suspicion 0)
                -- to the guard's first change of state from what he saw.
                if not guard.episode and b >= 1 then
                    guard.episode = { start = t, max_b = b, reacted = false }
                elseif guard.episode then
                    guard.episode.max_b = math.max(guard.episode.max_b, b)
                end

                if reaction.state ~= guard.state then
                    local rising = RANK[reaction.state] > RANK[guard.state]
                    local light, light_distance = self:nearest_light(at)
                    local line = {
                        type = "event", t = t, guard = guard.name, from = guard.state, to = reaction.state,
                        kind = reaction.kind, cause = reaction.cause, strength = round(reaction.strength, 2),
                        b = round(b), d = round(d), step = self.step_index, label = self.segment and self.segment.label or nil,
                        guard_at = vec(entity:get_position()), thief_at = vec(at),
                        stimulus_at = reaction.position and vec(reaction.position) or nil,
                        distance = round(entity:get_position():distance(at), 2), light = light, light_distance = light_distance,
                        gem = round(target.LightGem or 0), crouched = self.player.crouched,
                    }
                    if rising and reaction.kind == "Seen" then
                        line.reaction_time = guard.episode and round(t - guard.episode.start, 3) or nil
                        if guard.episode then guard.episode.reacted = true end
                    end
                    self:record(line)
                    guard.state = reaction.state
                end

                if guard.episode and b < 1 and d <= 0 then
                    if not guard.episode.reacted then
                        self:record({ type = "exposure", t = t, guard = guard.name, start = guard.episode.start,
                                      duration = round(t - guard.episode.start, 3), max_b = round(guard.episode.max_b) })
                    end
                    guard.episode = nil
                end
            end
        end
    end

    -- Footsteps (and landings): how loud each one reached every guard (> 0: heard).
    local step = self.player.last_step
    if step and step ~= self.last_step then
        self.last_step = step
        local heard = {}
        for _, guard in ipairs(self.guards) do
            if guard.entity:valid() and guard.vital == "Awake" then
                local level = ai.hearing(guard.entity, at, step.loudness)
                heard[guard.name] = round(level, 2)
                local s = self.segment and self.segment.guards[guard.name]
                if s then s.max_heard = math.max(s.max_heard, level) end
            end
        end
        self:record({ type = "noise", t = t, loudness = round(step.loudness, 2), material = step.material, heard = heard, at = vec(at) })
    end

    if self.frame % SAMPLE_EVERY == 0 then self:sample(t, at, target) end
end

function Bot:sample(t, at, target)
    local guards = {}
    for _, guard in ipairs(self.guards) do
        local entity = guard.entity
        if entity:valid() and guard.vital == "Awake" then
            local perception = entity:get("Perception")
            local reaction = ai.reaction(entity)
            local line = { name = guard.name, state = reaction.state, b = round(perception.Stimulus), d = round(perception.Suspicion),
                           strength = round(reaction.strength, 2), distance = round(entity:get_position():distance(at), 2),
                           at = vec(entity:get_position()) }
            if self.detail then
                local v = ai.visibility(entity, self.player.entity)
                line.vis = {
                    contrast = round(v.contrast), threshold = round(v.threshold), size = round(v.size), peripheral = round(v.peripheral),
                    motion = round(v.motion), atmosphere = round(v.atmosphere), fog_t = round(v.fog_transmittance),
                    fog_front = round(v.fog_glow_front, 4), fog_behind = round(v.fog_glow_behind, 4), self_contrast = round(v.self_contrast),
                    visible = v.visible_points, points = v.total_points, lp = round(v.target_luminance, 4), lb = round(v.background_luminance, 4),
                }
            end
            guards[#guards + 1] = line
        end
    end
    self:record({ type = "sample", t = t, step = self.step_index, thief = {
        at = vec(at), gem = round(target.LightGem or 0), light = round(target.LightLevel or 0, 4), crouched = self.player.crouched,
        lean = round(self.player.lean, 2), gait = self.gait or "walk",
    }, guards = guards })
end

-- ------------------------------------------------------------------ the frame

function Bot:outcome()
    local mission = Mission.current
    if mission and mission.state == "complete" then return "escaped" end
    if mission and mission.state == "failed" then return "failed", mission.reason end
    if not mission and self.goal and self.goal:valid() and self.goal:script() and self.goal:script().won then return "escaped" end
    for _, guard in ipairs(self.guards) do
        if guard.vital == "Awake" and guard.state == "Alarmed" then return "detected", guard.name end
    end
    if self:now() >= self.time_limit then return self.route.survive and "undetected" or "timeout" end
    return nil
end

function Bot:finish(outcome, reason)
    if self.finished then return end
    self.finished = true
    if self.step then self:close_segment() end
    local mission = Mission.current
    local alarmed = nil
    for _, guard in ipairs(self.guards or {}) do
        if guard.state == "Alarmed" then alarmed = guard.name end
    end
    self:record({
        type = "result", outcome = outcome, reason = reason, t = round(self:now(), 2),
        steps_done = math.max((self.step_index or 1) - 1, 0), steps = self.route and #self.route.steps or 0,
        loot = mission and mission.loot or nil, alarmed = alarmed, thief_at = vec(self.player.entity:get_position()),
    })
    self.down, self.pressed, self.dx, self.dy = {}, {}, 0, 0
    scene.quit(0)
end

function Bot:tick(dt)
    if self.finished then
        self.down, self.pressed, self.dx, self.dy = {}, {}, 0, 0
        return
    end
    if not self.started then self:start() end
    if self.finished then return end
    self.frame = self.frame + 1
    self.pressed, self.dx, self.dy = {}, 0, 0
    self.down = {}

    self:watch(dt)
    local outcome, reason = self:outcome()
    if outcome then
        self:finish(outcome, reason)
        return
    end

    if self.delay > 0 then
        self.delay = self.delay - dt
        if self.gait == "crouch" then self.down.C = true end
        return
    end
    if not self.step then
        if self.route.survive then self:finish("undetected") else self:finish("incomplete", "route ended") end
        return
    end
    if self.gait == "crouch" then self.down.C = true elseif self.gait == "run" then self.down.LeftShift = true end
    if self:run_step(dt) and not self.finished then
        if self.saved then -- back from a door to the step it interrupted
            self.step, self.saved = self.saved.step, nil
            self.phase, self.timer, self.path, self.progress_at, self.progress_time = nil, 0, nil, nil, 0
        else
            self:next_step()
        end
    end
end

return Bot
