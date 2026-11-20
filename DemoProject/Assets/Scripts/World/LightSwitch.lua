-- A light switch on the wall: E turns its lamps off or on (stim.set_lights: the lights, their
-- glowing fixtures and anything else lit on them). Flipping it clicks, quietly (Noises "Switch").
-- NPCs notice a lamp gone dark and come to turn it back on (Scripts/AI/Lamps.lua).
--
-- The switch entity carries a "Lever" child that tips up (on) or down (off).

local Noises = require("Scripts/AI/Noises")

local LightSwitch = {
    Properties = {
        lamps = "",            -- comma separated entity names of the lamps it controls
        label = "lights",      -- shown in the prompt: "[F] put out the hall lights"
        lever_angle = 35.0,    -- degrees the lever tips up or down
    }
}

function LightSwitch:on_create()
    self.is_light_switch = true
    self.lamp_entities = {}
    for name in string.gmatch(self.lamps, "[^,%s]+") do
        local lamp = scene.find(name)
        if lamp then
            table.insert(self.lamp_entities, lamp)
        else
            log.warn("LightSwitch: lamp '" .. name .. "' not found")
        end
    end
    for _, child in ipairs(self.entity:children()) do
        if child:get_name() == "Lever" then self.lever = child end
    end
    self.lit = self:lamps_lit()
    self.claimed_by = nil -- the NPC on his way to turn it back on (Lamps.lua)
    self:refresh()
end

-- The lamps count as lit when the first one's light is on.
function LightSwitch:lamps_lit()
    local lamp = self.lamp_entities[1]
    if not lamp then return false end
    for _, kind in ipairs({ "PointLight", "SpotLight" }) do
        if lamp:has(kind) then return lamp:get(kind).Enabled end
    end
    return false
end

function LightSwitch:refresh()
    self.use_prompt = (self.lit and "put out the " or "light the ") .. self.label
    if self.lever then
        self.lever:set_rotation(vec3(self.lit and -self.lever_angle or self.lever_angle, 0, 0))
    end
end

-- Turns the lamps on or off (nothing happens when they already are). user may be nil.
function LightSwitch:set(lit, user)
    if lit == self.lit then return false end
    self.lit = lit
    for _, lamp in ipairs(self.lamp_entities) do
        if lamp:valid() then stim.set_lights(lamp, lit) end
    end
    Noises.emit("Switch", self.entity:get_position(), user)
    if lit then self.claimed_by = nil end
    self:refresh()
    return true
end

function LightSwitch:on_use(user)
    self:set(not self.lit, user)
end

-- Where someone stands to reach it: in front of the plate (its forward side), on the floor.
function LightSwitch:standing_point()
    local position = self.entity:get_position() + self.entity:forward() * 0.7
    local floor = physics.raycast(position, vec3(0, -1, 0), 3.0, self.entity)
    return floor and floor.point or position
end

return LightSwitch
