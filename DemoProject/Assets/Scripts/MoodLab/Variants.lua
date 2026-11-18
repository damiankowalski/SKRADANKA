-- Mood lab (docs/art/mood/README.md): the lighting variants of Scenes/MoodLab.pscene, all in one table.
-- A variant sets the environment, the moon, the garden's low mist, which light sources burn (by name
-- prefix; every other source is hidden whole) and how fire and astral light look. Apply one with
--   require("Scripts/MoodLab/Variants").apply("Fire")
-- (docs/art/mood/MoodLab.jsonl does, through script.exec) or play the scene with --set mood=Fire.

local M = {}

-- What the cloister has today (Tools/BuildDemoProject.py add_environment): the comparison.
local BASE_ENVIRONMENT = {
    EnvironmentIntensity = 0.12, SkyboxIntensity = 0.6, AmbientColor = vec3(0.006, 0.006, 0.0084),
    Exposure = 1.4, FogDensity = 0.01, FogColor = vec3(0.02, 0.025, 0.035), BloomIntensity = 0.05,
    -- The engine's defaults for what the cloister does not set (volumetric fog, no height fog).
    VolumetricFog = true, FogScattering = 0.5, FogAnisotropy = 0.3, FogHeight = 0.0, FogHeightFalloff = 0.0,
}

-- The thick fog of A-C (v2, volumetric): dense at the ground, thinning above 1.5 m so the roofs and the
-- sky show through; it scatters a little of every lamp's light, mostly forwards (a glare looking into one).
local THICK_FOG = {
    VolumetricFog = true, FogDensity = 0.06, FogHeight = 1.5, FogHeightFalloff = 0.25, FogScattering = 0.12, FogAnisotropy = 0.4,
}

local function with_fog(environment)
    for key, value in pairs(THICK_FOG) do
        environment[key] = value
    end
    return environment
end

-- Fire: warm, flickering. Astral: cold cyan and violet, steady.
local FIRE = {
    Torch = { Color = vec3(1.0, 0.55, 0.25), Intensity = 11.0, Range = 10.0, Flicker = 0.18 },
    Brazier = { Color = vec3(1.0, 0.5, 0.2), Intensity = 20.0, Range = 12.0, Flicker = 0.2 },
}
local ASTRAL = {
    Cyan = { Color = vec3(0.35, 0.85, 1.0), Intensity = 14.0, Range = 10.0, Flicker = 0.0, Crystal = vec3(0.35, 0.85, 1.0) },
    Violet = { Color = vec3(0.6, 0.4, 1.0), Intensity = 14.0, Range = 10.0, Flicker = 0.0, Crystal = vec3(0.65, 0.45, 1.0) },
}

M.Variants = {
    -- 0. The cloister as it is: its torches, its environment.
    Base = {
        environment = BASE_ENVIRONMENT,
        moon = 0.08,
        burning = { "Fire_Warm_Torch", "Fire_Cold_Torch" },
    },
    -- A. Fire: thick fog, a near-black world, only torches and fire baskets.
    Fire = {
        mist = true,
        environment = with_fog {
            EnvironmentIntensity = 0.02, SkyboxIntensity = 0.12, AmbientColor = vec3(0.0015, 0.0013, 0.0012),
            Exposure = 1.6, FogColor = vec3(0.014, 0.010, 0.008), BloomIntensity = 0.09,
        },
        moon = 0.015,
        burning = { "Fire_" },
    },
    -- B. Astral: the same fog, only astral lamps (cyan in the east, violet in the west).
    Astral = {
        mist = true,
        environment = with_fog {
            EnvironmentIntensity = 0.02, SkyboxIntensity = 0.12, AmbientColor = vec3(0.0012, 0.0014, 0.002),
            Exposure = 1.6, FogColor = vec3(0.008, 0.011, 0.016), BloomIntensity = 0.12,
        },
        moon = 0.015,
        burning = { "Astral_" },
    },
    -- C. Mixed: fire in the east (warm zone), astral lamps in the west (cold zone), darkness between.
    Mixed = {
        mist = true,
        environment = with_fog {
            EnvironmentIntensity = 0.02, SkyboxIntensity = 0.12, AmbientColor = vec3(0.0013, 0.0013, 0.0016),
            Exposure = 1.6, FogColor = vec3(0.011, 0.010, 0.012), BloomIntensity = 0.1,
        },
        moon = 0.015,
        burning = { "Fire_Warm_", "Astral_Cold_" },
    },
}

local function starts_with(text, prefix)
    return text:sub(1, #prefix) == prefix
end

local function burns(variant, name)
    for _, prefix in ipairs(variant.burning) do
        if starts_with(name, prefix) then return true end
    end
    return false
end

-- The look of a source: fire by kind, astral by zone.
local function look_of(name)
    if starts_with(name, "Fire_") then
        return name:find("Brazier") and FIRE.Brazier or FIRE.Torch
    end
    return name:find("_Cold_") and ASTRAL.Violet or ASTRAL.Cyan
end

local function each_in_hierarchy(entity, fn)
    fn(entity)
    for _, child in ipairs(entity:children()) do
        each_in_hierarchy(child, fn)
    end
end

-- A source burns with its look, or is hidden whole (meshes, flame, light).
local function set_source(root, on)
    local look = look_of(root:get_name())
    each_in_hierarchy(root, function(e)
        if e:has("Mesh") then e:set("Mesh", { Visible = on }) end
        if e:has("Flame") then e:set("Flame", { Enabled = on }) end
        if e:has("PointLight") then
            e:set("PointLight", { Enabled = on, Color = look.Color, Intensity = look.Intensity, Range = look.Range, Flicker = look.Flicker })
        end
        if look.Crystal and e:get_name() == "Crystal" then
            e:set("Material", { EmissiveColor = look.Crystal })
        end
    end)
end

-- Applies a variant by name; returns its name (or raises an error for an unknown one).
function M.apply(name)
    local variant = M.Variants[name]
    if not variant then error("unknown mood variant '" .. tostring(name) .. "'") end
    scene.find("Environment"):set("Environment", variant.environment)
    scene.find("Moon"):set("DirectionalLight", { Intensity = variant.moon })
    scene.find("Mist_Garden"):set("FogVolume", { Enabled = variant.mist == true })
    for _, e in ipairs(scene.entities_with("Transform")) do
        local entity_name = e:get_name()
        if (starts_with(entity_name, "Fire_") or starts_with(entity_name, "Astral_")) and not e:parent() then
            set_source(e, burns(variant, entity_name))
        end
    end
    return name
end

return M
