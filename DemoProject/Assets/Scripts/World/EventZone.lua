-- Things that happen on their own in one part of the map while the player is near: in the
-- kitchen the cook drops a knife, bangs a pot, cracks something. They make the same noises the
-- player makes (Scripts/AI/Noises.lua), so a guard who hears one walks over to check exactly as
-- he would for the player. Events and their odds live in Scripts/World/Events.lua.

local Events = require("Scripts/World/Events")
local Noises = require("Scripts/AI/Noises")

local EventZone = {
    Properties = {
        set = "Kitchen",      -- key in Events.zones
        radius = 15.0,        -- active while the player is this close to the zone
        actor = "",           -- entity the noises come from (and that animates); "" = the zone itself
        chance_scale = 1.0,   -- multiplies every per_minute (0 switches the zone off)
    }
}

function EventZone:on_create()
    self.events = Events.zones[self.set]
    if not self.events then log.warn("EventZone: no event set '" .. tostring(self.set) .. "'") end
    self.cooldowns = {}
    self.player = scene.find("Player")
    self.actor_entity = self.actor ~= "" and scene.find(self.actor) or nil
    if self.actor ~= "" and not self.actor_entity then log.warn("EventZone: actor '" .. self.actor .. "' not found") end
    self.actor_model = nil
    if self.actor_entity then
        for _, child in ipairs(self.actor_entity:children()) do
            if child:get_name() == "Model" then self.actor_model = child end
        end
    end
    self.animation_timer = 0
    self.fired = 0
end

function EventZone:active()
    if not self.player or not self.player:valid() then return true end
    return self.player:get_position():distance(self.entity:get_position()) <= self.radius
end

function EventZone:fire(event)
    local source = self.actor_entity or self.entity
    local position = source:get_position() + vec3(0, 0.9, 0)
    Noises.emit(event.noise, position, source)
    self.fired = self.fired + 1
    if self.actor_model and event.animation then
        anim.play(self.actor_model, event.animation, 0.2, false)
        self.animation_timer = event.duration or 1.5
    end
    log.trace("EventZone " .. self.set .. ": " .. event.noise)
end

function EventZone:on_update(dt)
    if self.animation_timer > 0 then
        self.animation_timer = self.animation_timer - dt
        if self.animation_timer <= 0 and self.actor_model then anim.play(self.actor_model, "Idle", 0.3, true) end
    end
    if not self.events or not self:active() then return end
    for index, event in ipairs(self.events) do
        local cooldown = (self.cooldowns[index] or 0) - dt
        self.cooldowns[index] = cooldown
        if cooldown <= 0 and math.random() < event.per_minute * self.chance_scale * dt / 60 then
            self:fire(event)
            self.cooldowns[index] = event.cooldown
        end
    end
end

return EventZone
