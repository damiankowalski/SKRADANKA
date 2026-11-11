-- The light gem: a Thief-style HUD indicator of how much light the player is standing in.
-- It shows only the player's own illumination (PerceptionTarget.LightGem, 0..1); whether the
-- silhouette stands out against the background is left to the player's judgement.

local LightGem = {}

local DARK = { 0.05, 0.09, 0.07 }   -- deep, barely visible green
local MID = { 0.35, 0.55, 0.25 }    -- dim torchlight
local BRIGHT = { 1.0, 0.95, 0.65 }  -- standing in full light

local function mix(a, b, t)
    return { a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t, a[3] + (b[3] - a[3]) * t }
end

-- Smoothed value so the gem breathes with flickering torches instead of jittering.
function LightGem.new()
    return { value = 0 }
end

function LightGem.update(state, target_value, dt)
    state.value = state.value + (target_value - state.value) * math.min(dt * 8, 1)
    return state.value
end

-- Draws a diamond of horizontal strips centred at (cx, cy).
local function diamond(cx, cy, half_width, half_height, color)
    local step = 2
    for y = -half_height, half_height - step, step do
        local t = 1 - math.abs(y + step * 0.5) / half_height
        local w = half_width * t
        ui.rect(cx - w, cy + y, w * 2, step, color)
    end
end

function LightGem.draw(value, cx, cy)
    local color = value < 0.5 and mix(DARK, MID, value * 2) or mix(MID, BRIGHT, (value - 0.5) * 2)
    -- Frame, gem body, and a highlight that grows with the light.
    diamond(cx, cy, 52, 26, { 0.02, 0.02, 0.02, 0.85 })
    diamond(cx, cy, 46, 22, { 0.25, 0.22, 0.16, 1 })
    diamond(cx, cy, 42, 19, { color[1], color[2], color[3], 1 })
    diamond(cx - 10, cy - 6, 14 * value + 2, 5 * value + 1, { 1, 1, 1, 0.25 + 0.5 * value })
end

return LightGem
