-- Perception lab readout (F1 toggles): everything that goes into the guard's decision, live.
-- B >= 1 means the guard registers the player; suspicion D then rises.

local PerceptionDebug = { Properties = { guard = "Guard", player = "Player" } }

local STATE_COLORS = {
    Unaware = { 0.4, 0.9, 0.5, 1 },
    Suspicious = { 1.0, 0.9, 0.2, 1 },
    Searching = { 1.0, 0.55, 0.1, 1 },
    Alarmed = { 1.0, 0.2, 0.15, 1 },
}

function PerceptionDebug:on_create()
    self.visible = true
    self.guard_entity = scene.find(self.guard)
    self.player_entity = scene.find(self.player)
    self.start = self.player_entity and self.player_entity:get_position()
end

local function line(y, text, color)
    ui.text(20, y, text, 2, color or { 0.85, 0.85, 0.85, 1 })
    return y + 22
end

function PerceptionDebug:on_update(dt)
    if input.key_pressed("F1") then self.visible = not self.visible end
    if input.key_pressed("F2") and self.player_entity and self.start then
        self.player_entity:set_position(self.start) -- back to the start position
    end
    if not self.visible or not self.guard_entity or not self.player_entity then return end

    local v = ai.visibility(self.guard_entity, self.player_entity)
    local p = self.guard_entity:get("Perception")
    local t = self.player_entity:get("PerceptionTarget")
    local state = ai.state(self.guard_entity)

    ui.rect(10, 50, 470, 330, { 0, 0, 0, 0.55 })
    local y = 60
    y = line(y, string.format("GUARD  %s   suspicion %.2f", state, p.Suspicion), STATE_COLORS[state])
    ui.rect(20, y, 440, 8, { 0.2, 0.2, 0.2, 1 })
    ui.rect(20, y, 440 * p.Suspicion, 8, STATE_COLORS[state])
    y = y + 18
    y = line(y, string.format("stimulus B      %.2f  (>= 1 noticed)", v.stimulus), v.stimulus >= 1 and { 1, 0.5, 0.4, 1 } or nil)
    y = line(y, string.format("visible points  %d / %d", v.visible_points, v.total_points))
    y = line(y, string.format("player  Lp      %.4f", v.target_luminance))
    y = line(y, string.format("background Lb   %.4f", v.background_luminance))
    y = line(y, string.format("contrast C      %.2f   threshold %.2f", v.contrast, v.threshold))
    y = line(y, string.format("size %.2f  periphery %.2f  motion %.2f", v.size, v.peripheral, v.motion))
    y = line(y, string.format("atmosphere %.2f   distance %.1f m", v.atmosphere, v.distance))
    y = line(y, string.format("guard adaptation La  %.4f", p.Adaptation))
    y = line(y, string.format("light gem %.2f   (body luminance %.4f)", t.LightGem, t.LightLevel))
    line(y + 8, "F1 hide   F2 reset player   C crouch", { 0.6, 0.6, 0.6, 1 })
end

return PerceptionDebug
