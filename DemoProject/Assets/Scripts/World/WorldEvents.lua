-- Events that affect the whole level and compete with the player for the guards' attention.
--
-- Storm (a placeholder for any weather): rain raises the background noise a little; each strike
-- is a flash of light (the "Lightning" light and the windows) that lights the player up and
-- dazzles the guards, then thunder that masks every sound for a few seconds and offers guards an
-- innocent explanation for what they heard: those reacting to something weaker than the thunder
-- (plus their diligence, give or take luck) put it down to the storm and go back to their route.
-- Dogs howling outside are a weaker excuse of the same kind.
--
-- Odds and strengths: Scripts/World/Events.lua. Randomness: math.random (the scene's seed).

local Events = require("Scripts/World/Events")

local WorldEvents = {
    Properties = {
        storms = true,
        dogs = true,
        force_storm = false,        -- start with a storm (testing)
        lightning = "Lightning",    -- entity with a DirectionalLight used for flashes
        windows = "",               -- comma separated emissive entities that glow with each flash
        dogs_position = vec3(0, 1, 30), -- where the howls come from (outside the walls)
        show_status = true,         -- "storm" note on the HUD
    }
}

local function between(range) return range[1] + (range[2] - range[1]) * math.random() end

function WorldEvents:on_create()
    self.storm = Events.world.Storm
    self.dog = Events.world.Dogs
    self.environment = nil
    for _, entity in ipairs(scene.entities_with("Environment")) do self.environment = self.environment or entity end
    self.base_noise = self.environment and self.environment:get("Environment").AmbientNoise or 0
    self.light = scene.find(self.lightning)
    self.window_entities = {}
    for name in string.gmatch(self.windows, "[^,%s]+") do
        local window = scene.find(name)
        if window then
            table.insert(self.window_entities, { entity = window, glow = window:get("Material").EmissiveIntensity })
        else
            log.warn("WorldEvents: window '" .. name .. "' not found")
        end
    end
    self.storm_time = 0       -- > 0 while a storm lasts
    self.next_strike = 0
    self.flash_time = 0
    self.thunder_delay = nil
    self.rumble_time = 0
    self.dog_cooldown = 0
    self.strikes = 0
    self.excused = 0          -- guards who put something down to the weather (for the HUD and tests)
    if self.force_storm then self:start_storm() end
end

function WorldEvents:set_noise(level)
    if self.environment then self.environment:set("Environment", { AmbientNoise = level }) end
end

function WorldEvents:start_storm()
    self.storm_time = between(self.storm.duration)
    self.next_strike = between(self.storm.thunder_every) * 0.3 -- the first strike comes early
    self:set_noise(math.max(self.base_noise, self.storm.rain_noise))
    if scene.find("Rain") then audio.play(scene.find("Rain")) end
    log.info("A storm is coming")
end

function WorldEvents:end_storm()
    self.storm_time = 0
    self:set_noise(self.base_noise)
    if scene.find("Rain") then audio.stop(scene.find("Rain")) end
    log.info("The storm has passed")
end

function WorldEvents:set_flash(on)
    local lightning = self.storm.lightning
    if self.light then
        self.light:set("DirectionalLight", { Enabled = on, Intensity = on and lightning.intensity or 0 })
    end
    for _, window in ipairs(self.window_entities) do
        window.entity:set("Material", { EmissiveIntensity = on and lightning.window_glow or window.glow })
    end
end

function WorldEvents:strike()
    self.strikes = self.strikes + 1
    self:set_flash(true)
    self.flash_time = self.storm.lightning.time
    self.thunder_delay = between(self.storm.thunder.delay)
end

function WorldEvents:excuse(strength, cause, spread)
    local calmed = ai.explain_away(strength, cause, { spread = spread })
    for _, guard in ipairs(calmed) do
        local instance = guard:script()
        if instance and type(instance.bark) == "function" then
            instance:bark(cause == "Thunder" and "Just the storm." or "Only the dogs.")
        else
            audio.play_at("Audio/Mumble.wav", guard:get_position() + vec3(0, 1.6, 0), 0.6)
        end
    end
    if #calmed > 0 then log.info(string.format("%d guard(s) put it down to %s", #calmed, cause)) end
    self.excused = self.excused + #calmed
end

function WorldEvents:thunder()
    local thunder = self.storm.thunder
    audio.play_at("Audio/Thunder.wav", vec3(0, 20, 0), 1.0)
    self.rumble_time = thunder.masking_time
    self:set_noise(math.max(self.base_noise, thunder.masking))
    self:excuse(thunder.strength, "Thunder", thunder.spread)
end

function WorldEvents:update_storm(dt)
    if self.flash_time > 0 then
        self.flash_time = self.flash_time - dt
        if self.flash_time <= 0 then self:set_flash(false) end
    end
    if self.thunder_delay then
        self.thunder_delay = self.thunder_delay - dt
        if self.thunder_delay <= 0 then
            self.thunder_delay = nil
            self:thunder()
        end
    end
    if self.rumble_time > 0 then
        self.rumble_time = self.rumble_time - dt
        if self.rumble_time <= 0 then
            self:set_noise(self.storm_time > 0 and math.max(self.base_noise, self.storm.rain_noise) or self.base_noise)
        end
    end

    if self.storm_time > 0 then
        self.storm_time = self.storm_time - dt
        self.next_strike = self.next_strike - dt
        if self.next_strike <= 0 then
            self:strike()
            self.next_strike = between(self.storm.thunder_every)
        end
        if self.storm_time <= 0 then self:end_storm() end
    elseif self.storms and math.random() < self.storm.per_minute * dt / 60 then
        self:start_storm()
    end
end

function WorldEvents:update_dogs(dt)
    if not self.dogs then return end
    self.dog_cooldown = self.dog_cooldown - dt
    local rate = self.dog.per_minute * (self.storm_time > 0 and self.dog.storm_multiplier or 1)
    if self.dog_cooldown <= 0 and math.random() < rate * dt / 60 then
        self.dog_cooldown = self.dog.cooldown
        audio.play_at("Audio/Howl.wav", self.dogs_position, 0.8)
        self:excuse(self.dog.strength, "Dogs", self.dog.spread)
    end
end

function WorldEvents:on_update(dt)
    self:update_storm(dt)
    self:update_dogs(dt)
    if self.show_status and self.storm_time > 0 then
        ui.text(ui.width() - 220, 20, self.rumble_time > 0 and "storm: thunder" or "storm", 2, { 0.6, 0.7, 1.0, 1 })
    end
end

return WorldEvents
