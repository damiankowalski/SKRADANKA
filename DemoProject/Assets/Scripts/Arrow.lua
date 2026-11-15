-- An arrow striking something makes a noise: a dull thunk in wood, a crack on stone, a clang on
-- metal, next to nothing in moss or carpet (the struck Surface's NoiseModifier is added), so a
-- near miss can give the archer away. What the arrow does (douse, ignite, hurt) is its stim.

local Noises = require("Scripts/AI/Noises")

local Arrow = {
    push_impulse = 50.0, -- N*s a bolt gives the part of a limp body it strikes (far more than its 0.1 kg would: the hit must read)
}

-- A bolt striking a limp body (a ragdoll) shoves the part it hit, along its flight.
function Arrow.push(arrow, other)
    if other and other:valid() and physics.ragdoll_active(other) then
        physics.ragdoll_impulse(other, arrow:forward() * Arrow.push_impulse, arrow:get_position())
        return true
    end
    return false
end

-- What the arrow actually struck: the first surface along its flight (a thin floor over the
-- ground reports both contacts, in no particular order).
function Arrow.struck_entity(arrow, fallback)
    local forward = arrow:forward()
    local hit = physics.raycast(arrow:get_position() - forward * 0.6, forward, 1.2, arrow)
    return hit and hit.entity or fallback
end

function Arrow:on_collision(other)
    if self.struck then return end
    self.struck = true
    Arrow.push(self.entity, other)
    Noises.arrow_impact(self.entity:get_position(), Arrow.struck_entity(self.entity, other), self.entity)
end

return Arrow
