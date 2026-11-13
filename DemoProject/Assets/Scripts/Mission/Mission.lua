-- The mission in progress: objectives for the chosen difficulty, loot taken and what happened on
-- the way (for the end-of-mission summary). One per scene, shared through require():
--   MissionControl.lua starts it and draws it, Loot.lua and the player's hands add loot,
--   Goal.lua asks whether the thief may leave.
--
-- A mission definition (Scripts/Missions/*.lua) lists objectives; each may be limited to some
-- difficulties:
--   { loot = { Normal = 300, Hard = 500, Expert = 700 } }        steal at least that much
--   { item = "CellarKey", text = "..." }                          take a particular item
--   { no_kill = "all" | { "Cook", ... }, text = "..." }          kill nobody (or none of them)
--   { escape = true, text = "..." }                               leave once the rest is done
-- Killing someone a no_kill objective protects fails the mission.

local Mission = {}

Mission.DIFFICULTIES = { "Normal", "Hard", "Expert" }

local function applies(objective, difficulty)
    if not objective.difficulty then return true end
    for _, name in ipairs(objective.difficulty) do
        if name == difficulty then return true end
    end
    return false
end

local function protects(objective, name)
    if objective.no_kill == "all" then return true end
    for _, protected in ipairs(objective.no_kill) do
        if protected == name then return true end
    end
    return false
end

-- The objectives of a definition at a difficulty, with their texts filled in.
function Mission.objectives_for(definition, difficulty)
    local list = {}
    for _, objective in ipairs(definition.objectives) do
        if applies(objective, difficulty) then
            local entry = {}
            for key, value in pairs(objective) do entry[key] = value end
            if objective.loot then
                entry.target = objective.loot[difficulty] or objective.loot.Normal or 0
                entry.text = string.format(objective.text or "Steal loot worth at least %d", entry.target)
            end
            table.insert(list, entry)
        end
    end
    return list
end

-- Starts a mission (the definition table or a module name) at a difficulty.
function Mission.start(definition, difficulty)
    if type(definition) == "string" then definition = require(definition) end
    difficulty = difficulty or "Normal"
    local self = {
        definition = definition,
        difficulty = difficulty,
        objectives = Mission.objectives_for(definition, difficulty),
        state = "running", -- running / complete / failed
        reason = nil,      -- why it failed
        loot = 0,
        loot_items = 0,
        items = {},        -- ids of special items taken
        stats = { pickpocketed = 0, knockouts = 0, kills = 0, alerts = 0, bodies_found = 0 },
        started = time.elapsed(),
        finished = nil,
    }
    Mission.current = self
    return self
end

-- True once the mission ended (escaped or failed): the thief stops, guards stop chasing.
function Mission.over()
    local mission = Mission.current
    return mission ~= nil and mission.state ~= "running"
end

function Mission.active()
    local mission = Mission.current
    return mission and mission.state == "running" and mission or nil
end

-- Loot or an item was taken (how: "taken" or "pickpocketed").
function Mission.take(value, item, how)
    local mission = Mission.active()
    if not mission then return end
    value = value or 0
    if value > 0 then
        mission.loot = mission.loot + value
        mission.loot_items = mission.loot_items + 1
    end
    if item and item ~= "" then mission.items[item] = true end
    if how == "pickpocketed" then mission.stats.pickpocketed = mission.stats.pickpocketed + 1 end
end

-- Someone went down: "Unconscious" or "Dead". Killing a protected NPC fails the mission.
function Mission.on_down(name, state)
    local mission = Mission.active()
    if not mission then return end
    if state == "Dead" then
        mission.stats.kills = mission.stats.kills + 1
        for _, objective in ipairs(mission.objectives) do
            if objective.no_kill and protects(objective, name) then
                objective.failed = true
                Mission.fail(string.format("You killed %s.", name))
                return
            end
        end
    else
        mission.stats.knockouts = mission.stats.knockouts + 1
    end
end

function Mission.count(stat)
    local mission = Mission.active()
    if mission then mission.stats[stat] = mission.stats[stat] + 1 end
end

-- Done / not done / failed for one objective (escape counts once the mission is complete).
function Mission.status(mission, objective)
    if objective.loot then return mission.loot >= objective.target and "done" or "open" end
    if objective.item then return mission.items[objective.item] and "done" or "open" end
    if objective.no_kill then return objective.failed and "failed" or "done" end
    if objective.escape then return mission.state == "complete" and "done" or "open" end
    return "open"
end

-- What still stands between the thief and the way out (texts), empty when he may leave.
function Mission.missing(mission)
    local missing = {}
    for _, objective in ipairs(mission.objectives) do
        if not objective.escape and Mission.status(mission, objective) ~= "done" then
            local text = objective.text
            if objective.loot then text = string.format("%s (%d so far)", text, mission.loot) end
            table.insert(missing, text)
        end
    end
    return missing
end

-- The thief reached the way out: the mission is complete when nothing is missing.
function Mission.try_escape()
    local mission = Mission.active()
    if not mission then return false, {} end
    local missing = Mission.missing(mission)
    if #missing > 0 then return false, missing end
    mission.state = "complete"
    mission.finished = time.elapsed()
    return true, {}
end

function Mission.fail(reason)
    local mission = Mission.active()
    if not mission then return end
    mission.state = "failed"
    mission.reason = reason
    mission.finished = time.elapsed()
end

-- Lines of the end-of-mission summary.
function Mission.summary(mission)
    local seconds = math.floor((mission.finished or time.elapsed()) - mission.started)
    local lines = {
        string.format("%s  -  %s", mission.definition.name or "Mission", mission.difficulty),
        string.format("Time            %d:%02d", seconds // 60, seconds % 60),
        string.format("Loot            %d  (%d of %d pieces)", mission.loot, mission.loot_items, mission.total_items or mission.loot_items),
        string.format("Pickpocketed    %d", mission.stats.pickpocketed),
        string.format("Knocked out     %d", mission.stats.knockouts),
        string.format("Killed          %d", mission.stats.kills),
        string.format("Alarms raised   %d", mission.stats.alerts),
        string.format("Bodies found    %d", mission.stats.bodies_found),
    }
    return lines
end

return Mission
