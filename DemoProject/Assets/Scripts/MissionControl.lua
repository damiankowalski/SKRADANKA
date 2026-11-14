-- Runs the mission of a level: starts it at the difficulty chosen in the menu (game.get), keeps
-- count of what happens (knockouts, kills, alarms, bodies found), shows the loot and the
-- objectives (Tab), and the summary when the thief escapes or fails.
-- Enter on the summary: back to the menu after a success, try again after a failure.

local Mission = require("Scripts/Mission/Mission")

local MissionControl = {
    Properties = {
        mission = "Scripts/Missions/Manor",
        menu_scene = "Scenes/Menu.pscene",
    }
}

local MARK = { done = "[x]", open = "[ ]", failed = "[!]" }
local COLOR = { done = { 0.55, 0.95, 0.6, 1 }, open = { 0.85, 0.85, 0.85, 1 }, failed = { 1, 0.35, 0.3, 1 } }
local RANK = { Unaware = 0, Suspicious = 1, Investigating = 2, Searching = 3, Alarmed = 4 }

function MissionControl:on_create()
    self.mission = Mission.start(self.mission, game.get("difficulty", "Normal"))
    local total = 0
    for _, entity in ipairs(scene.entities_with("Script")) do
        local instance = entity:script()
        if instance and instance.value and instance.label and instance.value > 0 then total = total + 1 end
    end
    self.mission.total_items = total
    -- Everyone who walks about but the thief (the cook has no eyes to count alarms with, but can die).
    self.npcs = {}
    for _, npc in ipairs(scene.entities_with("CharacterController")) do
        if npc:get_name() ~= "Player" then
            self.npcs[#self.npcs + 1] = { entity = npc, name = npc:get_name(), vital = "Awake", state = "Unaware", cause = "",
                                          sees = npc:has("Perception") }
        end
    end
    self.show_objectives = 4.0 -- seconds the list stays up at the start
end

-- Knockouts, kills, alarms and bodies found, from what every NPC is doing.
function MissionControl:watch_npcs()
    for _, npc in ipairs(self.npcs) do
        if npc.entity:valid() then
            local vital = ai.vital_state(npc.entity)
            if vital ~= npc.vital and vital ~= "Awake" then
                -- Finishing off someone knocked out turns that knockout into a kill.
                if npc.vital == "Unconscious" then self.mission.stats.knockouts = self.mission.stats.knockouts - 1 end
                Mission.on_down(npc.name, vital)
            end
            npc.vital = vital
            if vital == "Awake" and npc.sees then
                local reaction = ai.reaction(npc.entity)
                if reaction.state == "Alarmed" and RANK[npc.state] < RANK.Alarmed then Mission.count("alerts") end
                if reaction.cause == "Body" and npc.cause ~= "Body" then Mission.count("bodies_found") end
                npc.state, npc.cause = reaction.state, reaction.cause
            end
        end
    end
end

function MissionControl:draw_objectives(x, y)
    for _, objective in ipairs(self.mission.objectives) do
        local status = Mission.status(self.mission, objective)
        ui.text(x, y, MARK[status] .. " " .. objective.text, 1.6, COLOR[status])
        y = y + 24
    end
end

function MissionControl:draw_hud(dt)
    local w = ui.width()
    local loot_target = 0
    for _, objective in ipairs(self.mission.objectives) do
        if objective.loot then loot_target = objective.target end
    end
    local loot = string.format("Loot %d / %d", self.mission.loot, loot_target)
    ui.text(w - #loot * 12 - 20, 20, loot, 2, { 1.0, 0.85, 0.4, 1 })
    ui.text(w - 220, 48, self.mission.difficulty .. "   [Tab] objectives", 1.4, { 0.7, 0.7, 0.7, 0.8 })

    self.show_objectives = math.max(self.show_objectives - dt, 0)
    if input.key("Tab") or self.show_objectives > 0 then self:draw_objectives(20, 50) end
end

function MissionControl:draw_summary()
    local w, h = ui.width(), ui.height()
    ui.rect(0, 0, w, h, { 0, 0, 0, 0.75 })
    local complete = self.mission.state == "complete"
    local title = complete and "MISSION COMPLETE" or "MISSION FAILED"
    ui.text(w / 2 - #title * 12, 90, title, 4, complete and { 0.5, 1.0, 0.6, 1 } or { 1, 0.35, 0.3, 1 })
    local y = 170
    if self.mission.reason then
        ui.text(w / 2 - #self.mission.reason * 9, y, self.mission.reason, 2, { 1, 0.7, 0.6, 1 })
        y = y + 50
    end
    for _, line in ipairs(Mission.summary(self.mission)) do
        ui.text(w / 2 - 220, y, line, 2, { 0.9, 0.9, 0.9, 1 })
        y = y + 32
    end
    self:draw_objectives(w / 2 - 220, y + 20)
    local next_step = complete and "[Enter] back to the menu" or "[Enter] try again"
    ui.text(w / 2 - #next_step * 9, h - 80, next_step, 2, { 0.8, 0.8, 0.8, 1 })
end

-- Enter on the summary: what happens next (returned for tests).
function MissionControl:continue()
    if self.mission.state == "complete" then
        scene.load(self.menu_scene)
        return "menu"
    elseif self.mission.state == "failed" then
        scene.load(self.mission.definition.scene or self.menu_scene)
        return "retry"
    end
    return nil
end

function MissionControl:on_update(dt)
    if self.mission.state == "running" then
        self:watch_npcs()
        self:draw_hud(dt)
        return
    end
    -- Over: the thief stands still (Player.lua) and the summary shows how it went.
    input.set_cursor_locked(false)
    self:draw_summary()
    if input.key_pressed("Enter") then self:continue() end
end

return MissionControl
