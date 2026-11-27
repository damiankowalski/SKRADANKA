-- Title screen: a choice of mission (left and right), its briefing, a choice of difficulty (1 / 2 / 3,
-- or up and down) and what each one asks of the thief. Enter starts the mission; the difficulty and
-- the mission go with it in the game state (game.set), so they also hold when a caught thief starts
-- over or comes back to the menu.

local Mission = require("Scripts/Mission/Mission")

local Menu = {
    Properties = {
        missions = "Scripts/Missions/Manor", -- comma separated mission modules, the first chosen by default
    }
}

function Menu:on_create()
    self.list = {}
    for path in string.gmatch(self.missions, "[^,%s]+") do table.insert(self.list, path) end
    self.mission_index = 1
    local last = game.get("mission", self.list[1])
    for i, path in ipairs(self.list) do
        if path == last then self.mission_index = i end
    end
    self.definition = require(self.list[self.mission_index])
    self.choice = 1
    local current = game.get("difficulty", "Normal")
    for i, name in ipairs(Mission.DIFFICULTIES) do
        if name == current then self.choice = i end
    end
    input.set_cursor_locked(false)
end

function Menu:difficulty() return Mission.DIFFICULTIES[self.choice] end

-- Picks a mission by its place in the list; returns its name.
function Menu:pick_mission(index)
    if index >= 1 and index <= #self.list then
        self.mission_index = index
        self.definition = require(self.list[index])
    end
    return self.definition.name
end

-- Picks a difficulty by number (1-3).
function Menu:choose(index)
    if index >= 1 and index <= #Mission.DIFFICULTIES then self.choice = index end
    return self:difficulty()
end

-- Starts the mission at the chosen difficulty.
function Menu:begin()
    game.set("difficulty", self:difficulty())
    game.set("mission", self.list[self.mission_index])
    scene.load(self.definition.scene)
    return self:difficulty()
end

function Menu:on_update(dt)
    for i = 1, #Mission.DIFFICULTIES do
        if input.key_pressed(tostring(i)) then self:choose(i) end
    end
    if input.key_pressed("Up") then self:choose(self.choice - 1) end
    if input.key_pressed("Down") then self:choose(self.choice + 1) end
    if input.key_pressed("Left") then self:pick_mission(self.mission_index - 1) end
    if input.key_pressed("Right") then self:pick_mission(self.mission_index + 1) end
    if input.key_pressed("Enter") then self:begin() end

    local w, h = ui.width(), ui.height()
    local x = math.max(w / 2 - 420, 30)
    ui.text(x, 70, "PENUMBRA", 5, { 0.85, 0.8, 0.7, 1 })
    ui.text(x, 150, self.definition.name, 3, { 1.0, 0.85, 0.5, 1 })
    if #self.list > 1 then
        ui.text(x + 560, 158, string.format("[Left / Right] mission %d of %d", self.mission_index, #self.list), 1.6,
            { 0.7, 0.7, 0.7, 1 })
    end
    local y = 210
    for _, line in ipairs(self.definition.briefing or {}) do
        ui.text(x, y, line, 1.6, { 0.8, 0.8, 0.8, 1 })
        y = y + 26
    end

    y = y + 30
    for i, name in ipairs(Mission.DIFFICULTIES) do
        local selected = i == self.choice
        ui.text(x, y, string.format("%s [%d] %s", selected and ">" or " ", i, name), 2.2,
            selected and { 1, 0.9, 0.6, 1 } or { 0.6, 0.6, 0.6, 1 })
        y = y + 36
    end

    y = y + 20
    ui.text(x, y, "Objectives", 2, { 0.9, 0.9, 0.9, 1 })
    y = y + 34
    for _, objective in ipairs(Mission.objectives_for(self.definition, self:difficulty())) do
        ui.text(x + 20, y, "- " .. objective.text, 1.6, { 0.8, 0.8, 0.8, 1 })
        y = y + 26
    end
    ui.text(x, h - 70, "[Enter] begin", 2.2, { 0.9, 0.9, 0.9, 1 })
end

return Menu
