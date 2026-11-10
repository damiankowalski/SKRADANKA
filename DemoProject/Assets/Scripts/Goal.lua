-- Exit zone: reaching it unseen wins the level.
local Goal = {}

function Goal:on_create()
    self.won = false
end

function Goal:on_collision(other)
    if other:get_name() == "Player" and not self.won then
        self.won = true
        log.info("Level complete")
        audio.play_at("Audio/Win.wav", self.entity:get_position(), 0.8)
    end
end

function Goal:on_update(dt)
    if self.won then
        ui.text(ui.width() / 2 - 120, ui.height() / 2 - 20, "YOU ESCAPED", 4, { 0.5, 1.0, 0.6, 1 })
    else
        ui.text(20, 20, "Reach the gate on the far side. Stay out of sight.", 2, { 0.8, 0.8, 0.8, 0.8 })
    end
end

return Goal
