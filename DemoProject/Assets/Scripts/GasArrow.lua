-- Gas arrow: bursts into a cloud of sleeping gas wherever it lands (Prefabs/GasCloud.pprefab).
-- The cloud is a continuous "Gas" stim; NPCs whose StimResponse has a Gas -> Knockout rule fall
-- asleep in it (the demo's guards and servants do).

local Noises = require("Scripts/AI/Noises")

local GasArrow = {}

function GasArrow:on_collision(other)
    if self.burst then return end
    self.burst = true
    local position = self.entity:get_position()
    scene.instantiate("Prefabs/GasCloud.pprefab", position)
    audio.play_at("Audio/Hiss.wav", position, 0.8)
    Noises.arrow_impact(position, require("Scripts/Arrow").struck_entity(self.entity, other), self.entity)
    self.entity:destroy()
end

return GasArrow
