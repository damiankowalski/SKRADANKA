-- The event table: things that happen without the player and compete with him for the guards'
-- attention (docs/Perception.md, "Events and distractions"). Every value is a designer knob.
--
-- Area events (Scripts/World/EventZone.lua) make the same noises the player makes, from the
-- zone's actor, so guards react to them exactly as they would to the player.
-- World events (Scripts/World/WorldEvents.lua) affect the whole level: they mask sounds, change
-- the light and offer guards an innocent explanation for what they heard (ai.explain_away).
-- Randomness comes from math.random, seeded by the scene (reproducible in tests, --seed).

local Events = {}

-- Area events by zone set. noise = a name from Scripts/AI/Noises.lua (its loudness and clip);
-- per_minute = average occurrences while the player is near; cooldown = minimum seconds between
-- two of the same; animation = clip the actor plays (seconds of it: duration).
Events.zones = {
    Kitchen = {
        { noise = "Knife", per_minute = 1.0, cooldown = 25, animation = "Interact", duration = 1.2 },
        { noise = "Pot", per_minute = 0.6, cooldown = 40, animation = "Use_Item", duration = 2.0 },
        { noise = "Crack", per_minute = 1.5, cooldown = 10, animation = "PickUp", duration = 1.5 },
    },
}

-- World events.
Events.world = {
    Storm = {
        per_minute = 0.25,          -- chance per minute that a storm starts while it is calm
        duration = { 90, 180 },     -- seconds
        rain_noise = 2.0,           -- Environment.AmbientNoise while it rains (masks the quietest steps)
        thunder_every = { 15, 40 }, -- seconds between strikes
        thunder = {
            strength = 6.0,         -- distraction strength against what guards are reacting to (0-10)
            spread = 2.0,           -- luck either way: guards roll +-spread
            masking = 7.0,          -- AmbientNoise while it rumbles: footsteps vanish in it
            masking_time = 3.0,     -- seconds of rumble
            delay = { 0.3, 1.5 },   -- seconds from the flash to the sound (distance of the strike)
        },
        lightning = {
            intensity = 3.0,        -- intensity of the "Lightning" directional light during the flash
            window_glow = 12.0,     -- emissive intensity of the windows during the flash
            time = 0.15,            -- seconds
        },
    },
    Dogs = {
        per_minute = 0.5,           -- howls outside the walls (more often during a storm)
        storm_multiplier = 3.0,
        strength = 3.0,             -- a weak excuse: only small doubts are put down to the dogs
        spread = 1.0,
        cooldown = 20,
    },
}

return Events
