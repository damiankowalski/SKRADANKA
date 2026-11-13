-- Something worth stealing: a goblet on a table (E takes it) or a purse or key on a belt
-- (pickpocket = true: taken from behind an NPC who is not on alert, see Player.lua).
-- value counts towards the mission's loot; item names it for "take this" objectives.

local Mission = require("Scripts/Mission/Mission")

local Loot = {
    Properties = {
        label = "loot",
        value = 50,
        item = "",          -- id for objectives ("CellarKey"); empty for plain loot
        pickpocket = false, -- on an NPC's belt
    }
}

function Loot:on_create()
    self.taken = false
    self.use_prompt = self:prompt()
end

function Loot:prompt()
    local verb = self.pickpocket and "pick" or "take"
    if self.value > 0 then return string.format("%s the %s (%d)", verb, self.label, self.value) end
    return string.format("%s the %s", verb, self.label)
end

-- Taken: counts for the mission and disappears. Returns false when already gone.
function Loot:take()
    if self.taken then return false end
    self.taken = true
    Mission.take(self.value, self.item, self.pickpocket and "pickpocketed" or "taken")
    audio.play_at("Audio/Coins.wav", self.entity:get_position(), 0.5)
    scene.destroy(self.entity)
    return true
end

function Loot:on_use(user)
    if self.pickpocket then return end -- belts are handled by the thief's hands
    self:take()
end

return Loot
