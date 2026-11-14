-- Exit zone. With a mission running (MissionControl.lua) the thief may leave once the other
-- objectives are done; otherwise he is told what is missing. Without one, reaching it wins.

local Mission = require("Scripts/Mission/Mission")

local Goal = {}

function Goal:on_create()
    self.won = false
    self.notice = nil
    self.notice_timer = 0
end

-- The player stepped in: returns whether he got away (for tests).
function Goal:reach()
    if self.won then return true end
    if Mission.current then
        local escaped, missing = Mission.try_escape()
        if not escaped then
            self.notice = "Not yet: " .. table.concat(missing, "; ")
            self.notice_timer = 4
            return false
        end
    end
    self.won = true
    log.info("Level complete")
    audio.play_at("Audio/Win.wav", self.entity:get_position(), 0.8)
    return true
end

function Goal:on_collision(other)
    if other:get_name() == "Player" then self:reach() end
end

function Goal:on_update(dt)
    if Mission.current then
        if self.notice then
            self.notice_timer = self.notice_timer - dt
            ui.text(ui.width() / 2 - #self.notice * 6, ui.height() / 2 - 80, self.notice, 2, { 1, 0.85, 0.6, 1 })
            if self.notice_timer <= 0 then self.notice = nil end
        end
        return -- the mission draws the rest
    end
    if self.won then
        ui.text(ui.width() / 2 - 120, ui.height() / 2 - 20, "YOU ESCAPED", 4, { 0.5, 1.0, 0.6, 1 })
    else
        ui.text(20, 20, "Reach the gate on the far side. Stay out of sight.", 2, { 0.8, 0.8, 0.8, 0.8 })
    end
end

return Goal
