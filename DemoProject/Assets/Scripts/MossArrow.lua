-- Moss arrow: bursts into a patch of moss where it lands, so footsteps across it are near silent
-- (Prefabs/MossPatch.pprefab: a Moss Surface, NoiseModifier -3). Moss only takes on a floor (or
-- anything else facing up); striking a wall or a person it just falls apart, softly.

local Noises = require("Scripts/AI/Noises")

local MossArrow = {
    Properties = {
        patch = "Prefabs/MossPatch.pprefab",
        min_up = 0.7, -- the struck surface's normal must point at least this much upwards
    }
}

-- Where the moss goes: the point of the floor struck, or nil when the arrow did not hit a floor.
function MossArrow.floor_point(arrow, min_up)
    local forward = arrow:forward()
    local hit = physics.raycast(arrow:get_position() - forward * 0.6, forward, 1.2, arrow)
    if not hit or hit.normal.y < min_up then
        -- A shallow shot can touch the floor before the shaft lines up with it: look straight down.
        hit = physics.raycast(arrow:get_position() + vec3(0, 0.2, 0), vec3(0, -1, 0), 0.4, arrow)
    end
    if hit and hit.normal.y >= min_up then return hit.point end
    return nil
end

function MossArrow:on_collision(other)
    if self.burst then return end
    self.burst = true
    require("Scripts/Arrow").push(self.entity, other)
    local point = MossArrow.floor_point(self.entity, self.min_up)
    if point then scene.instantiate(self.patch, point + vec3(0, 0.01, 0)) end -- resting on the floor, not in it
    Noises.emit("Moss", self.entity:get_position(), self.entity)
    self.entity:destroy()
end

return MossArrow
