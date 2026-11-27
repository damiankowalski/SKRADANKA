-- A hinged door. The entity stands at the hinge; its "Panel" child is a kinematic body with a
-- Surface whose SoundLoss lets sound through muffled (docs/Perception.md), and it blocks sight
-- and light like any wall while closed. Opened, it swings away from whoever opens it.
--
--   The thief: E opens or closes it. Locked: the right key (a Mission item, e.g. the cellar key
--   picked from the captain's belt) unlocks it at once; without it, E starts picking the lock
--   (Scripts/World/Lockpick.lua).
--   NPCs: walking up to a door (towards it, not past it) they open it (guards and servants carry
--   the keys) and close it behind them once nobody is in the way; a locked door stays locked.
--   The creak and the thud are heard by the thief either way; the guards take note only when the
--   thief works a door (the household's own comings and goings are nothing to check).
--
-- The "Lock" child marks where the lock is: the lockpicking bar hangs there.

local Noises = require("Scripts/AI/Noises")
local Mission = require("Scripts/Mission/Mission")

-- A door's noise: for the guards to check when the thief (or nobody known) worked it, only a sound
-- when one of the household did.
local function door_noise(name, position, who)
    if who and who:valid() and who:has("Perception") then
        Noises.sound(name, position)
    else
        Noises.emit(name, position, who)
    end
end

local Door = {
    Properties = {
        label = "door",
        width = 1.2,          -- metres from the hinge to the far edge (the panel's extent along +X)
        open_angle = 95.0,    -- degrees
        swing_speed = 160.0,  -- degrees per second
        locked = false,
        key = "",             -- Mission item that unlocks it ("CellarKey"); empty: no key, only picking
        key_label = "key",
        pins = 6,             -- letters to press when picking the lock
        npc_reach = 1.7,      -- an NPC this close (and walking) gets the door opened
        close_delay = 1.5,    -- seconds with nobody near before an NPC's door swings shut
        jam_time = 6.0,       -- an NPC's door that someone stays by without passing is shut after this
    }
}

function Door:on_create()
    self.is_door = true
    -- The yaw from where the closed panel runs (its local +X). The Euler angles may read back as
    -- (180, 0, 180) for a leaf turned half round, whose y alone would be 0.
    local right = self.entity:right()
    self.base_yaw = math.deg(math.atan(-right.z, right.x))
    self.angle = 0          -- current swing, degrees (0 = closed)
    self.target = 0
    self.npc_opened = false -- opened by an NPC, who closes it again
    self.empty_time = 0
    self.check = 0
    for _, child in ipairs(self.entity:children()) do
        if child:get_name() == "Lock" then self.lock = child end
        if child:get_name() == "Panel" then self.panel = child end
    end
    self:refresh()
end

function Door:is_open() return self.target ~= 0 end

-- The middle of the doorway (closed panel), on the floor.
function Door:center()
    return self.entity:get_position() + self:hinge_right() * (self.width * 0.5)
end

-- The closed panel runs along this direction from the hinge.
function Door:hinge_right()
    local yaw = math.rad(self.base_yaw)
    return vec3(math.cos(yaw), 0, -math.sin(yaw))
end

-- The closed door's facing (its -Z): a positive swing moves the panel to this side.
function Door:facing()
    local yaw = math.rad(self.base_yaw)
    return vec3(-math.sin(yaw), 0, -math.cos(yaw))
end

function Door:refresh()
    if self:is_open() then
        self.use_prompt = "close the " .. self.label
    elseif self.locked then
        local has_key = self.key ~= "" and Mission.has(self.key)
        self.use_prompt = has_key and ("unlock the " .. self.label .. " with the " .. self.key_label) or ("pick the lock of the " .. self.label)
    else
        self.use_prompt = "open the " .. self.label
    end
end

-- Swings it open away from `who` (an entity, or nil: towards the facing side).
function Door:open(who)
    if self:is_open() then return false end
    if self.picking and who ~= self.picking.user then -- someone else opened it: the picking is over
        self.picking:abort()
        self.picking = nil
    end
    local away = 1
    if who and who:valid() then
        local side = (who:get_position() - self:center()):dot(self:facing())
        away = side > 0 and -1 or 1
    end
    self.target = away * self.open_angle
    door_noise("DoorOpen", self:center() + vec3(0, 1, 0), who)
    self:refresh()
    return true
end

function Door:close(who)
    if not self:is_open() then return false end
    self.target = 0
    self.npc_opened = false
    self.closing_by = who
    self:refresh()
    return true
end

function Door:unlock(who)
    if not self.locked then return false end
    self.locked = false
    Noises.emit("Unlock", self:center() + vec3(0, 1, 0), who)
    self:refresh()
    return true
end

-- E from the thief.
function Door:on_use(user)
    if self.picking then return end
    if self:is_open() then
        self:close(user)
    elseif not self.locked then
        self:open(user)
    elseif self.key ~= "" and Mission.has(self.key) then
        self:unlock(user)
        self:open(user)
    else
        local player = user and user:script()
        if player and player.start_busy then
            self.picking = require("Scripts/World/Lockpick").new(self, self.pins, user)
            player:start_busy(self.picking)
        end
    end
end

-- The lockpicking task is over (opened or given up).
function Door:picking_done(success, user)
    self.picking = nil
    if success then
        self:unlock(user)
        self:open(user)
    end
end

-- People count for a door only on its own storey: within this many metres above or below its foot.
local FLOOR_REACH = 1.5

-- Anyone near the doorway: NPCs who are up, and the thief.
function Door:someone_near(radius)
    local center = self:center()
    for _, npc in ipairs(scene.entities_with("CharacterController")) do
        local offset = npc:get_position() - center
        if vec3(offset.x, 0, offset.z):length() < radius and math.abs(offset.y) < FLOOR_REACH then return npc end
    end
    return nil
end

-- An awake NPC walking up to it (towards the doorway, not just past it along a corridor), or nil.
function Door:npc_coming()
    local center = self:center()
    local facing, along = self:facing(), self:hinge_right()
    for _, npc in ipairs(scene.entities_with("Perception")) do
        if ai.vital_state(npc) == "Awake" and nav.status(npc) == "Moving" then
            local offset = npc:get_position() - center
            local rise = offset.y
            offset = vec3(offset.x, 0, offset.z)
            if math.abs(rise) >= FLOOR_REACH then offset = vec3(1e6, 0, 0) end -- another storey
            if offset:length() < self.npc_reach and math.abs(offset:dot(along)) < self.width * 0.5 + 0.5 then
                local velocity = character.get_velocity(npc)
                local flat = vec3(velocity.x, 0, velocity.z)
                local across = flat:dot(facing)
                -- Heading for the door plane from either side (side * velocity < 0), at a walk, and
                -- mostly at it: someone walking past along a corridor does not open a side door.
                if offset:dot(facing) * across < 0 and math.abs(across) > 0.3 and math.abs(across) > 0.6 * flat:length() then
                    return npc
                end
            end
        end
    end
    return nil
end

function Door:update_npcs(dt)
    self.check = self.check - dt
    if self.check > 0 then return end
    self.check = 0.15
    if not self:is_open() then
        local npc = self:npc_coming()
        if npc then
            self:open(npc)
            self.npc_opened = true
            self.opened_by = npc -- it swings shut behind him: his noise, the household's
            self.empty_time = 0
            self.jammed_time = 0
        end
    elseif self.npc_opened then
        if self:someone_near(self.width + 1.0) then
            self.empty_time = 0
            -- Someone stays by it without going through (the leaf may be in his way): shut it, so
            -- whoever comes next opens it away from himself.
            self.jammed_time = (self.jammed_time or 0) + 0.15
            if self.jammed_time >= self.jam_time then
                self.jammed_time = 0
                self:close(self.opened_by)
            end
        else
            self.jammed_time = 0
            self.empty_time = self.empty_time + 0.15
            if self.empty_time >= self.close_delay then self:close(self.opened_by) end
        end
    end
end

function Door:on_update(dt)
    self:update_npcs(dt)
    if self.angle ~= self.target then
        local step = self.swing_speed * dt
        local delta = self.target - self.angle
        if math.abs(delta) <= step then
            self.angle = self.target
            if self.target == 0 then door_noise("DoorShut", self:center() + vec3(0, 1, 0), self.closing_by) end
        else
            self.angle = self.angle + (delta > 0 and step or -step)
        end
        self.entity:set_rotation(vec3(0, self.base_yaw + self.angle, 0))
    end
    if self.picking then self.picking:draw() end
end

return Door
