-- A compass strip for the HUD: the cardinal points slide past a fixed marker as the camera turns.
-- It shows only where the camera looks, never where the thief is: no minimap, no GPS.
--
-- Bearings are degrees clockwise from north. North is the world's -Z unless a level turns it
-- (the player's north_yaw property).

local Compass = {
    width = 300,  -- logical pixels
    span = 180,   -- degrees visible across the strip
}

local MARKS = {
    { 0, "N", true }, { 45, "NE" }, { 90, "E", true }, { 135, "SE" },
    { 180, "S", true }, { 225, "SW" }, { 270, "W", true }, { 315, "NW" },
}

-- Wraps an angle to [-180, 180).
local function wrap(degrees)
    return (degrees + 180) % 360 - 180
end

-- The bearing the camera faces, from the player's yaw (forward = (-sin yaw, 0, -cos yaw)).
function Compass.heading(yaw, north_yaw)
    return (-(yaw - (north_yaw or 0))) % 360
end

-- Draws the strip with its right edge at `right`, top at `top`.
function Compass.draw(right, top, heading)
    local width, half = Compass.width, Compass.width * 0.5
    local left = right - width
    local center = left + half
    ui.rect(left, top, width, 34, { 0, 0, 0, 0.45 })
    for bearing = 0, 345, 15 do
        local offset = wrap(bearing - heading)
        if math.abs(offset) <= Compass.span * 0.5 then
            local x = center + offset / (Compass.span * 0.5) * half
            local tall = bearing % 45 == 0
            ui.rect(x - 1, top + (tall and 2 or 4), 2, tall and 8 or 5, { 0.85, 0.85, 0.8, 0.6 })
        end
    end
    for _, mark in ipairs(MARKS) do
        local offset = wrap(mark[1] - heading)
        if math.abs(offset) <= Compass.span * 0.5 then
            local x = center + offset / (Compass.span * 0.5) * half
            local fade = 1 - math.abs(offset) / (Compass.span * 0.5) * 0.7
            local scale = mark[3] and 2.2 or 1.6
            local color = mark[1] == 0 and { 1.0, 0.45, 0.35, fade } or { 0.95, 0.9, 0.8, fade }
            ui.text(x - #mark[2] * 3 * scale, top + 13, mark[2], scale, color)
        end
    end
    ui.rect(center - 1, top - 3, 3, 40, { 1.0, 0.85, 0.4, 0.9 }) -- where the camera looks
end

return Compass
