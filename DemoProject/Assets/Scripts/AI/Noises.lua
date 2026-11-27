-- The noise table: how loud things are on the 0-10 scale guards hear (docs/Perception.md).
--
--   loudness  carries   example
--      1       ~2 m     sneaking on stone
--      3       ~4 m     walking, a quiet crack
--      5       ~8 m     running on stone
--      7      ~16 m     running on a metal grate
--      9      ~32 m     a metal bucket dropped on cobbles
--     10      ~45 m     an explosion
--
-- A guard reacts in proportion to what reaches him (after distance, walls and paths around
-- them): below 4 he stops and looks, 4-6 he walks over, 7-8 he searches the area, 9-10 he
-- raises the alarm; the reaction lasts 90 s * (strength / 10)^2. Every value here is a knob.

local Noises = {}

-- Footsteps on a neutral floor; the Surface.NoiseModifier of the floor underneath is added
-- (carpet -2, grass -1, moss -3, wood +0.5, metal grate +2.5).
Noises.footsteps = {
    crouch = { loudness = 1.0, interval = 0.6, volume = 0.12 },
    walk = { loudness = 3.0, interval = 0.5, volume = 0.35 },
    run = { loudness = 5.0, interval = 0.3, volume = 0.7 },
}

-- Landing after a jump or a fall: loudness per m/s of falling speed (a 1.3 m drop is ~5 m/s).
Noises.fall = { per_speed = 1.0, minimum_speed = 3.0, volume = 0.6 }

-- Footstep clips by floor material (anything else uses the stone step). A clip anywhere in this file may be a
-- list of variants (generated ones: Audio/Generated, Tools/AudioGen): each play picks one of them.
Noises.footstep_clips = {
    Stone = "Audio/Footstep.wav",
    Metal = "Audio/Footstep_Metal.wav",
    Wood = "Audio/Footstep_Wood.wav",
    Carpet = "Audio/Footstep_Soft.wav",
    Grass = "Audio/Footstep_Soft.wav",
    Moss = "Audio/Footstep_Soft.wav",
}

-- Named one-off noises: Noises.emit("Bucket", position, source).
Noises.sounds = {
    Crack = { loudness = 3, clip = "Audio/Footstep_Wood.wav", volume = 0.4 }, -- a twig, a creaking board
    Knife = { loudness = 5, clip = "Audio/Knife.wav", volume = 0.8 },         -- a knife dropped on a stone floor
    Pot = { loudness = 6, clip = "Audio/Clang.wav", volume = 0.7 },           -- a pot banged on the stove
    Blackjack = { loudness = 3, clip = "Audio/Thud.wav", volume = 0.7 },      -- a blackjack on a helmet
    Rustle = { loudness = 1.5, clip = "Audio/Footstep_Soft.wav", volume = 0.5 }, -- lifting a body
    BodyDrop = { loudness = 2.5, clip = "Audio/Thud.wav", volume = 0.5 },     -- putting a body down
    Bell = { loudness = 10, clip = "Audio/Bell.wav", volume = 1.0 },          -- the alarm bell
    Bow = { loudness = 3, clip = "Audio/Bow.wav", volume = 0.6 },
    Switch = { loudness = 2, clip = "Audio/Click.wav", volume = 0.5 },        -- a light switch flipped
    Moss = { loudness = 1, clip = "Audio/Footstep_Soft.wav", volume = 0.4 },  -- a moss arrow bursting
    DoorOpen = { loudness = 3, clip = "Audio/Door.wav", volume = 0.6 },       -- hinges creaking
    DoorShut = { loudness = 4, clip = "Audio/Thud.wav", volume = 0.6 },       -- a door falling shut
    Unlock = { loudness = 2, clip = "Audio/Click.wav", volume = 0.6 },        -- a key or a pick turning the lock
    Pick = { loudness = 1, clip = "Audio/Click.wav", volume = 0.2 },          -- a pin set while picking
    PickSlip = { loudness = 2, clip = "Audio/Click.wav", volume = 0.5 },      -- the pick slipping: a wrong key
    Shout = { loudness = 8 },
    Bucket = { loudness = 9, clip = "Audio/Clang.wav", volume = 1.0 },
    Explosion = { loudness = 10, clip = "Audio/Explosion.wav", volume = 1.0 },
}

local function clamp(value, low, high) return math.max(low, math.min(high, value)) end

-- One clip out of a clip or a list of variants (the scene's seeded math.random picks; a single clip draws nothing).
function Noises.clip(clip)
    if type(clip) ~= "table" then return clip end
    if #clip <= 1 then return clip[1] end
    return clip[math.random(#clip)]
end

-- Material and noise modifier of the floor under an entity ("Stone", 0 without a Surface).
function Noises.surface_under(entity)
    local hit = physics.raycast(entity:get_position() + vec3(0, 0.3, 0), vec3(0, -1, 0), 1.0, entity)
    if hit and hit.entity and hit.entity:has("Surface") then
        local surface = hit.entity:get("Surface")
        return surface.Material, surface.NoiseModifier
    end
    return "Stone", 0
end

-- One footstep of the given gait ("crouch", "walk", "run"); returns its loudness and material.
function Noises.footstep(entity, gait)
    local step = Noises.footsteps[gait] or Noises.footsteps.walk
    local material, modifier = Noises.surface_under(entity)
    local loudness = clamp(step.loudness + modifier, 0, 10)
    local feet = entity:get_position()
    if loudness > 0 then ai.emit_noise(feet, loudness, entity, "Footstep." .. material) end
    -- What the player hears follows what the guards hear.
    local volume = step.volume * clamp(1 + modifier * 0.25, 0.2, 2.0)
    audio.play_at(Noises.clip(Noises.footstep_clips[material] or Noises.footstep_clips.Stone), feet, volume)
    return loudness, material
end

-- Landing with the given downward speed (m/s); returns the loudness (0 for soft landings).
function Noises.landing(entity, fall_speed)
    if fall_speed < Noises.fall.minimum_speed then return 0 end
    local material, modifier = Noises.surface_under(entity)
    local loudness = clamp(fall_speed * Noises.fall.per_speed + modifier, 0, 10)
    local feet = entity:get_position()
    ai.emit_noise(feet, loudness, entity, "Landing." .. material)
    audio.play_at(Noises.clip(Noises.footstep_clips[material] or Noises.footstep_clips.Stone), feet, Noises.fall.volume)
    return loudness
end

-- An arrow striking something: base loudness plus the struck surface's modifier (the surface of
-- the object hit, or of its parent). Returns the loudness.
Noises.arrow = { loudness = 3.5, clip = "Audio/Footstep_Wood.wav", volume = 0.6 }

function Noises.arrow_impact(position, struck, source)
    local material, modifier = "Stone", 0
    local entity = struck
    while entity do
        if entity:has("Surface") then
            local surface = entity:get("Surface")
            material, modifier = surface.Material, surface.NoiseModifier
            break
        end
        entity = entity:parent()
    end
    local loudness = clamp(Noises.arrow.loudness + modifier, 0, 10)
    if loudness > 0 then ai.emit_noise(position, loudness, source, "Arrow." .. material) end
    audio.play_at(Noises.clip(material ~= "Stone" and Noises.footstep_clips[material] or Noises.arrow.clip), position,
        Noises.arrow.volume)
    return loudness
end

-- A named noise from Noises.sounds; returns its loudness (errors on unknown names).
function Noises.emit(name, position, source)
    local sound = Noises.sounds[name]
    if not sound then error("unknown noise '" .. tostring(name) .. "'") end
    ai.emit_noise(position, sound.loudness, source, name)
    if sound.clip then audio.play_at(Noises.clip(sound.clip), position, sound.volume or 1.0) end
    return sound.loudness
end

-- The same sound with nothing for the guards to hear: the household's own doors and chores.
function Noises.sound(name, position)
    local sound = Noises.sounds[name]
    if not sound then error("unknown noise '" .. tostring(name) .. "'") end
    if sound.clip then audio.play_at(Noises.clip(sound.clip), position, sound.volume or 1.0) end
    return sound.loudness
end

return Noises
