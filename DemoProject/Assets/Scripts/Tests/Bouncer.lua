-- Counts collisions (feature test: on_collision and on_stim_event callbacks).
local Bouncer = {}

function Bouncer:on_create()
    self.collisions = 0
end

function Bouncer:on_collision(other)
    self.collisions = self.collisions + 1
end

function Bouncer:on_stim_event(name)
    log.info("stim event " .. name)
end

return Bouncer
