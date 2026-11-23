-- Picking a lock. A progress bar hangs at the lock in the scene (ui.world_rect): it is not drawn
-- on top of everything, so walls and the thief's own body can hide it, and turning the camera
-- needs care. Under it a row of letters from W, A, S, D, drawn at random, to press in order: each
-- right key sets a pin; a wrong one makes the pick slip and the last pin set falls back.
-- The thief cannot walk meanwhile (W, A, S, D are the letters); E gives up.
--
-- Started by Scripts/World/Door.lua; the thief's script calls update() every frame while busy
-- (Player:start_busy), the door calls draw().

local Noises = require("Scripts/AI/Noises")

local Lockpick = {
    letters = { "W", "A", "S", "D" },
    slip_penalty = 1,  -- pins lost on a wrong key
    bar_width = 0.5,   -- metres
    letter_height = 0.07,
    letter_spacing = 0.1,
    standoff = 0.3,    -- how far out from the door it hangs (letters must not sink into the frame)
}
Lockpick.__index = Lockpick

-- door: the Door script instance; pins: how many letters; user: the thief's entity.
function Lockpick.new(door, pins, user)
    local self = setmetatable({ door = door, sequence = {}, index = 1, done = false, slip = 0 }, Lockpick)
    for i = 1, math.max(1, pins) do self.sequence[i] = Lockpick.letters[math.random(#Lockpick.letters)] end
    self.user = user
    -- The bar hangs a little out from the door on the thief's side.
    self.side = 1
    if user and user:valid() then
        self.side = (user:get_position() - door:center()):dot(door:facing()) >= 0 and 1 or -1
    end
    self.camera = scene.find("Camera")
    return self
end

function Lockpick:progress() return (self.index - 1) / #self.sequence end
function Lockpick:current() return self.sequence[self.index] end

-- Where the bar hangs: above the lock, out from the door's face on the thief's side.
function Lockpick:anchor()
    local lock = self.door.lock and self.door.lock:valid() and self.door.lock:get_position() or (self.door:center() + vec3(0, 1, 0))
    return lock + self.door:facing() * (Lockpick.standoff * self.side) + vec3(0, 0.25, 0)
end

-- One key: true when it set a pin.
function Lockpick:press(key)
    if self.done then return false end
    local at = self:anchor()
    if key == self.sequence[self.index] then
        self.index = self.index + 1
        Noises.emit("Pick", at, self.user)
        if self.index > #self.sequence then self:finish(true) end
        return true
    end
    self.index = math.max(1, self.index - Lockpick.slip_penalty)
    self.slip = 0.35
    Noises.emit("PickSlip", at, self.user)
    return false
end

function Lockpick:finish(success)
    if self.done then return end
    self.done = true
    self.door:picking_done(success, self.user)
end

-- Gone without a result (the door was opened by someone else).
function Lockpick:abort() self.done = true end

-- The thief's input, every frame while busy. Returns false once it is over.
function Lockpick:update(player, dt)
    if self.done then return false end
    self.slip = math.max(self.slip - dt, 0)
    if player.controls.key_pressed("F") then
        self:finish(false)
        return false
    end
    for _, key in ipairs(Lockpick.letters) do
        if player.controls.key_pressed(key) then self:press(key) end
    end
    local w, h = ui.width(), ui.height()
    local hint = "[W A S D] pick the lock   [F] give up"
    ui.text(w / 2 - #hint * 6, h - 130, hint, 2, { 1, 1, 1, 0.7 }) -- low: the bar itself hangs near the middle
    return not self.done
end

function Lockpick:draw()
    if self.done then return end
    local right = self.camera and self.camera:valid() and self.camera:right() or self.door:hinge_right()
    right = vec3(right.x, 0, right.z):normalized()
    local at = self:anchor()
    local width = Lockpick.bar_width
    ui.world_rect(at, width + 0.02, 0.055, { 0.03, 0.03, 0.03, 0.85 })
    local progress = self:progress()
    if progress > 0 then
        local fill = width * progress
        ui.world_rect(at - right * ((width - fill) * 0.5), fill, 0.035, { 0.85, 0.7, 0.25, 0.95 })
    end
    -- The letters, centred under the bar: set ones dim, the next one bright (red after a slip).
    local count = #self.sequence
    local row = at - vec3(0, 0.1, 0)
    for i, letter in ipairs(self.sequence) do
        local color
        if i < self.index then
            color = { 0.45, 0.6, 0.4, 0.8 }
        elseif i == self.index then
            color = self.slip > 0 and { 1.0, 0.25, 0.2, 1.0 } or { 1.0, 0.9, 0.35, 1.0 }
        else
            color = { 0.9, 0.9, 0.9, 0.85 }
        end
        local offset = (i - (count + 1) * 0.5) * Lockpick.letter_spacing
        local height = i == self.index and Lockpick.letter_height * 1.4 or Lockpick.letter_height
        ui.world_text(row + right * offset, letter, height, color)
    end
end

return Lockpick
