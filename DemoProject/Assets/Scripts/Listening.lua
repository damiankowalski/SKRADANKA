-- The listening scene (Scenes/Listening.pscene): the game's sounds where the game plays them, for judging generated
-- ones (Tools/AudioGen) in place. Walk the strip of floors (stone, wood, a metal grate, carpet, grass, moss) to hear
-- the footsteps (Scripts/AI/Noises.lua picks the clip by the floor). From the strip's end the doors to the east,
-- 3, 10 and 25 m away, creak open and fall shut in turn, the night's wind blows and the fog lies over everything.
--
--   1, 2, 3   work the near, the middle or the far door
--   0         the doors' cycle off and on

local Noises = require("Scripts/AI/Noises")

local Listening = {
    Properties = {
        interval = 4.0,                        -- seconds between two door sounds in the cycle
        door_names = "Door_3m,Door_10m,Door_25m",
        floors = "Stone,Wood,Metal,Carpet,Grass,Moss",  -- each lit by its Lamp_<floor>, which carries the label
    }
}

function Listening:on_create()
    self.doors = {}
    for name in string.gmatch(self.door_names, "[^,]+") do
        local door = scene.find(name)
        if door then table.insert(self.doors, door) end
    end
    self.cycle = true
    self.timer = self.interval
    self.next = 1
    self.worked = 0   -- door sounds so far (the tests read it)
    self.player = scene.find("Player")
    self.labels = {}
    for floor in string.gmatch(self.floors, "[^,]+") do
        local lamp = scene.find("Lamp_" .. floor)
        if lamp then table.insert(self.labels, { text = floor, position = lamp:get_position() + vec3(0.8, -2.5, 0) }) end
    end
end

-- Opens a closed door or shuts an open one (Scripts/World/Door.lua makes the creak and the thud).
function Listening:work(index)
    local door = self.doors[index]
    if not door then return end
    local instance = door:script()
    if instance:is_open() then instance:close(nil) else instance:open(nil) end
    self.worked = self.worked + 1
end

function Listening:on_update(dt)
    for index = 1, #self.doors do
        if input.key_pressed(tostring(index)) then self:work(index) end
    end
    if input.key_pressed("0") then self.cycle = not self.cycle end
    if self.cycle and #self.doors > 0 then
        self.timer = self.timer - dt
        if self.timer <= 0 then
            self.timer = self.interval
            self:work(self.next)
            -- Each door twice (open, shut), then the next one.
            if not self.doors[self.next]:script():is_open() then self.next = self.next % #self.doors + 1 end
        end
    end

    for _, label in ipairs(self.labels) do
        ui.world_text(label.position, label.text, 0.2, { 0.8, 0.8, 0.75, 1 })
    end
    for _, door in ipairs(self.doors) do
        ui.world_text(door:get_position() + vec3(0, 2.6, 0), (door:get_name():gsub("Door_", "")), 0.25, { 0.9, 0.85, 0.7, 1 })
    end
    local floor = self.player and self.player:valid() and Noises.surface_under(self.player) or nil
    ui.text(20, 20, "Listening: walk the floors; 1 2 3 work the doors, 0 the cycle " .. (self.cycle and "(on)" or "(off)"),
        1.8, { 0.8, 0.8, 0.8, 0.9 })
    if floor then ui.text(20, 48, "floor: " .. floor, 1.8, { 0.8, 0.8, 0.8, 0.9 }) end
end

return Listening
