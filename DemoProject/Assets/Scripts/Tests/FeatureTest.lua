-- Feature test scene: exercises every engine system and every function of the Lua API.
-- Run headless:  PenumbraRuntime --project DemoProject --scene Scenes/FeatureTest.pscene
--                 --headless --warp --fixed-timestep 0.0166667 --exit-on-test
-- Every new Lua API function must be called here (see AGENTS.md).

local H = require("Scripts/Tests/Helpers")

local FeatureTest = { Properties = { label = "feature-test", factor = 2.0, flag = true, offset = vec3(1, 2, 3) } }

local function run(self)
    H.wait(0) -- start on the second frame so frame counters are non-zero
    -- ---------------------------------------------------------------- properties, log, time
    test.equal(self.label, "feature-test", "string property")
    test.near(self.factor, 3.0, 1e-6, "number property overridden by the scene")
    test.check(self.flag == true, "boolean property")
    test.check(self.offset == vec3(1, 2, 3), "vec3 property")
    log.trace("trace"); log.info("info", 1, true); log.warn("warn"); print("print")
    test.check(time.delta() > 0, "time.delta")
    test.check(time.frame() >= 1, "time.frame")
    local start = time.elapsed()

    -- ---------------------------------------------------------------- vec3
    local a, b = vec3(1, 2, 3), vec3(4, 5, 6)
    test.check(a + b == vec3(5, 7, 9) and b - a == vec3(3, 3, 3), "vec3 add/sub")
    test.check(a * 2 == vec3(2, 4, 6) and 2 * a == vec3(2, 4, 6) and a * b == vec3(4, 10, 18), "vec3 mul")
    test.check(b / 2 == vec3(2, 2.5, 3) and -a == vec3(-1, -2, -3), "vec3 div/neg")
    test.near(vec3(3, 4, 0):length(), 5, 1e-5, "length")
    test.near(vec3(0, 0, 9):normalized().z, 1, 1e-5, "normalized")
    test.near(a:dot(b), 32, 1e-5, "dot")
    test.check(vec3(1, 0, 0):cross(vec3(0, 1, 0)) == vec3(0, 0, 1), "cross")
    test.near(a:distance(b), math.sqrt(27), 1e-4, "distance")
    test.check(a:lerp(b, 0.5) == vec3(2.5, 3.5, 4.5), "lerp")
    test.check(tostring(a):find("vec3") ~= nil, "tostring")
    test.check(vec3().x == 0 and vec3(7).y == 7, "constructors")

    -- ---------------------------------------------------------------- scene and entities
    local e = scene.create("Temp")
    test.check(e:valid() and e:get_name() == "Temp", "scene.create")
    e:set_name("Temp2")
    test.equal(scene.find("Temp2").id, e.id, "set_name/find")
    test.check(scene.get(e.id) == e, "scene.get")
    test.check(scene.find("NoSuchEntity") == nil, "find missing")
    e:set_position(vec3(1, 2, 3))
    test.check(e:get_position() == vec3(1, 2, 3), "world position")
    e:set_local_position(vec3(4, 5, 6))
    test.check(e:get_local_position() == vec3(4, 5, 6), "local position")
    e:set_rotation(vec3(0, 90, 0))
    test.near(e:get_rotation().y, 90, 1e-3, "rotation")
    test.near(e:forward().x, -1, 1e-4, "forward")
    test.near(e:right().z, -1, 1e-4, "right")
    test.near(e:up().y, 1, 1e-4, "up")
    e:set_scale(vec3(2, 2, 2))
    test.check(e:get_scale() == vec3(2, 2, 2), "scale")
    e:look_at(vec3(4, 5, 0), true)
    test.near(e:forward().z, -1, 1e-4, "look_at yaw only")

    e:add("PointLight", { Intensity = 3 })
    test.check(e:has("PointLight"), "add/has")
    test.near(e:get("PointLight").Intensity, 3, 1e-6, "get")
    e:set("PointLight", { Range = 2 })
    test.near(e:get("PointLight").Range, 2, 1e-6, "set merges")
    test.near(e:get("PointLight").Intensity, 3, 1e-6, "set keeps other fields")
    e:remove("PointLight")
    test.check(not e:has("PointLight"), "remove")
    test.check(e:get("SpotLight") == nil, "get missing component")
    test.check(not pcall(function() e:add("NoSuchComponent") end), "unknown component errors")

    local child = scene.create("TempChild")
    child:set_parent(e)
    test.check(child:parent() == e and #e:children() == 1, "hierarchy")
    child:set_parent(nil)
    test.check(child:parent() == nil, "unparent")
    test.check(#scene.entities_with("Perception") == 1, "entities_with")
    local count = scene.count()
    e:destroy()
    scene.destroy(child)
    H.wait(0)
    test.check(not e:valid() and scene.count() == count - 2, "destroy")
    test.check(not pcall(function() return e:get_position() end), "destroyed handle errors")

    -- Game values kept across scene loads.
    test.equal(game.get("difficulty", "Normal"), "Normal", "game.get default")
    game.set("difficulty", "Hard")
    game.set("score", 120)
    test.equal(game.get("difficulty"), "Hard", "game.set / game.get")
    test.near(game.get("score"), 120, 1e-9, "game numbers")
    game.set("score", nil)
    test.check(game.get("score") == nil, "game.set nil erases")
    test.check(not pcall(function() game.set("bad", {}) end), "game.set refuses tables")
    game.clear()
    test.check(game.get("difficulty") == nil, "game.clear")

    -- Cross-script access.
    test.equal(scene.find("Runner"):script().label, "feature-test", "Entity:script")

    -- ---------------------------------------------------------------- input
    test.check(input.key("W") == false and input.key_pressed("W") == false and input.key_released("W") == false, "keys")
    test.check(input.mouse_button("Left") == false and input.mouse_pressed("Right") == false, "mouse buttons")
    local mx, my = input.mouse_position()
    local dx, dy = input.mouse_delta()
    test.check(type(mx) == "number" and type(dy) == "number" and type(input.scroll()) == "number", "mouse")
    input.set_cursor_locked(false)
    test.check(not pcall(input.key, "NotAKey"), "bad key errors")

    -- ---------------------------------------------------------------- physics
    local crate = scene.find("Crate")
    local hit = physics.raycast(vec3(0, 10, 8), vec3(0, -1, 0), 20)
    test.check(hit ~= nil and hit.entity:get_name() == "Ground", "raycast")
    test.near(hit.normal.y, 1, 1e-3, "raycast normal")
    test.check(physics.raycast(vec3(0, 10, 8), vec3(0, -1, 0), 20, hit.entity) == nil, "raycast ignore")
    test.check(#physics.overlap_sphere(vec3(0, 0, 8), 1) >= 1, "overlap_sphere")
    test.check(physics.get_gravity().y < 0, "gravity")
    physics.set_gravity(vec3(0, -9.81, 0))
    physics.set_velocity(crate, vec3(0, 6, 0))
    test.near(physics.get_velocity(crate).y, 6, 1e-3, "velocity")
    physics.add_impulse(crate, vec3(5, 0, 0))
    physics.add_force(crate, vec3(0, 0, 1))
    physics.radial_impulse(vec3(0, 0, 0), 0.1, 1)
    H.wait(0.5)
    test.check(crate:get_position().x > 4.05, "impulse moved the crate")

    -- ---------------------------------------------------------------- character
    local player = scene.find("Player")
    test.check(character.is_grounded(player), "character grounded")
    local before = player:get_position()
    character.move(player, vec3(2, 0, 0))
    H.wait(0.5)
    character.move(player, vec3(0, 0, 0))
    test.check(player:get_position().x > before.x + 0.5, "character.move")
    test.check(type(character.get_velocity(player).x) == "number", "character velocity")
    character.jump(player, 4)
    H.wait(0.15)
    test.check(not character.is_grounded(player), "character.jump")
    H.wait(1.0)

    -- ---------------------------------------------------------------- skeletal animation
    local model = nil
    for _, child in ipairs(player:children()) do
        if child:get_name() == "Model" then model = child end
    end
    test.check(model ~= nil and model:has("Animator"), "player model is animated")
    local names = {}
    for _, name in ipairs(anim.clips(model)) do names[name] = true end
    test.check(names["Idle"] and names["Crouch_Walk"] and names["Look_Around"], "anim.clips")
    test.check(anim.is_playing(model, "Idle"), "anim.is_playing")
    anim.play(model, "Crouch_Walk", 0.1)
    test.check(anim.is_playing(model, "Crouch_Walk") and not anim.is_playing(model, "Idle"), "anim.play")
    anim.set_speed(model, 2)
    H.wait(0.3)
    local animator = model:get("Animator")
    test.equal(animator.CurrentClip, "Crouch_Walk", "animator switched clips")
    test.check(animator.Speed == 2 and animator.Time > 0.3, "anim.set_speed")
    anim.play(model, "1H_Ranged_Shoot", 0, false)
    H.wait(3.0)
    test.check(not anim.is_playing(model), "one-shot clips finish")
    test.check(not pcall(anim.play, model, "NoSuchClip"), "unknown clips error")
    anim.play(model, "Idle", 0.2, true)
    anim.set_speed(model, 1)

    -- ---------------------------------------------------------------- audio
    local speaker = scene.find("Speaker")
    test.check(audio.play(speaker), "audio.play")
    test.check(audio.is_playing(speaker), "audio.is_playing")
    audio.stop(speaker)
    test.check(not audio.is_playing(speaker), "audio.stop")
    test.check(audio.play_at("Audio/Bow.wav", vec3(0, 1, 0), 0.5), "audio.play_at")

    -- ---------------------------------------------------------------- AI perception
    local guard = scene.find("Guard")
    -- Until now a curtain hid the player; drop it and start from a calm guard.
    scene.find("Curtain"):destroy()
    H.wait(0) -- destruction (and the physics body) goes away at the end of the frame
    ai.calm(guard)
    test.equal(ai.state(guard), "Unaware", "guard starts unaware")
    test.check(ai.luminance(vec3(0, 1, 0)) >= 0, "ai.luminance")
    local seen = ai.visibility(guard, player)
    test.equal(seen.total_points, 7, "visibility table")
    test.check(seen.visible_fraction > 0, "player in view")
    -- The player stands in front of the bright doorway: the guard must notice the silhouette.
    H.wait(2.0)
    test.check(ai.suspicion(guard) > 0.3, "silhouette against the doorway is noticed")
    test.check(ai.last_stimulus(guard) ~= nil, "ai.last_stimulus")
    local reaction = ai.reaction(guard)
    test.check(reaction.state ~= "Unaware" and reaction.kind == "Seen" and reaction.strength > 3, "ai.reaction after a sighting")
    ai.set_suspicion(guard, 0)
    test.near(ai.suspicion(guard), 0, 1e-6, "ai.set_suspicion")
    ai.calm(guard)
    test.equal(ai.state(guard), "Unaware", "ai.calm")
    -- Smoke between them hides the silhouette.
    scene.instantiate("Prefabs/Smoke.pprefab", vec3(0, 1.5, -3))
    H.wait(1.0)
    ai.calm(guard)
    H.wait(1.0)
    test.check(ai.suspicion(guard) < 0.3, "smoke blocks the silhouette")
    ai.calm(guard)
    ai.emit_noise(guard:get_position() + vec3(0, 1.6, -2), 6, player, "Crash")
    H.wait(0.1)
    test.equal(ai.reaction(guard).cause, "Crash", "noise is heard")
    test.check(ai.reaction(guard).strength > 4, "loud noise close by")
    test.near(ai.noise_range(6), 11.2, 0.01, "ai.noise_range")
    test.check(ai.hearing(guard, guard:get_position() + vec3(0, 1.65, -1.4), 5) > 4.9, "ai.hearing")
    test.check(ai.notify(guard, vec3(3, 0, 3), 9.5, "Shout"), "ai.notify")
    test.equal(ai.state(guard), "Alarmed", "a strong report alarms")
    test.check(ai.alertness(guard) > 0.9, "ai.alertness after an alarm")
    ai.calm(guard)
    ai.notify(guard, vec3(3, 0, 3), 5, "Creak")
    local excused = ai.explain_away(7, "Thunder", { spread = 0 })
    test.check(#excused == 1 and excused[1] == guard and ai.state(guard) == "Unaware", "ai.explain_away")
    test.equal(#ai.explain_away(7, "Thunder", { center = vec3(0, 0, 0), radius = 5 }), 0, "nothing left to explain")

    -- ---------------------------------------------------------------- navigation
    test.check(nav.polygon_count() > 0, "nav.polygon_count (baked)")
    local path = nav.path(vec3(-6, 0, 6), vec3(6, 0, 6))
    test.check(path ~= nil and path.complete and #path.points >= 2, "nav.path")
    test.near(nav.distance(vec3(-6, 0, 6), vec3(6, 0, 6)), path.length, 1e-4, "nav.distance")
    test.check(nav.closest_point(vec3(-6, 3, 6), 4) ~= nil, "nav.closest_point")
    test.check(nav.closest_point(vec3(0, 50, 0)) == nil, "nav.closest_point off the mesh")
    local spot = nav.random_point(vec3(-6, 0, 6), 3)
    test.check(spot ~= nil and spot:distance(vec3(-6, spot.y, 6)) <= 3.01, "nav.random_point")
    test.check(nav.move_to(guard, vec3(-3, 0, 4), 3.0), "nav.move_to")
    test.equal(nav.status(guard), "Moving", "nav.status moving")
    test.check(nav.remaining(guard) > 1, "nav.remaining")
    H.wait(4.0)
    test.equal(nav.status(guard), "Arrived", "guard walked there")
    test.check(guard:get_position():distance(vec3(-3, guard:get_position().y, 4)) < 0.6, "guard at the destination")
    nav.move_to(guard, vec3(0, 0, 0))
    nav.stop(guard)
    test.equal(nav.status(guard), "Idle", "nav.stop")
    test.check(nav.rebuild(), "nav.rebuild")

    -- ---------------------------------------------------------------- surfaces and impact noise
    local bucket = scene.find("Bucket")
    test.equal(bucket:get("Surface").Material, "Metal", "Surface component")
    ai.calm(guard)
    physics.set_velocity(bucket, vec3(0, 6, 0)) -- toss it up; the landing is the noise
    H.wait(1.6)
    test.equal(ai.reaction(guard).cause, "Impact.Metal", "a dropped bucket is heard")

    -- ---------------------------------------------------------------- takedowns, shouts, subtitles
    local sx, sy = ui.world_to_screen(guard:get_position() + vec3(0, 2, 0))
    test.check(sx ~= nil and sy ~= nil, "ui.world_to_screen (guard in view)")
    test.check(ui.world_to_screen(scene.find("Camera"):get_position() - scene.find("Camera"):forward() * 5) == nil,
        "ui.world_to_screen behind the camera")
    test.equal(ai.vital_state(guard), "Awake", "ai.vital_state")
    test.equal(type(ai.shout(guard, 8)), "table", "ai.shout")
    test.near(ai.damage(guard, 30), 70, 1e-4, "ai.damage")
    test.check(ai.knock_out(guard), "ai.knock_out")
    test.equal(ai.vital_state(guard), "Unconscious", "knocked out")
    test.check(not ai.knock_out(guard, true), "already down")
    test.equal(guard:get("PerceptionTarget").Kind, "Body", "a knocked-out guard is a body")

    -- ---------------------------------------------------------------- stims (Act/React)
    local torch = scene.find("Torch")
    local flame = nil
    for _, child in ipairs(torch:children()) do
        if child:get_name() == "Flame" then flame = child end
    end
    test.check(flame ~= nil, "torch has a flame")
    test.check(flame:get("PointLight").Enabled, "torch lit")
    stim.send(torch, "Water", 1.0)
    H.wait(0.1)
    test.check(not flame:get("PointLight").Enabled, "water extinguishes")
    test.check(not flame:get("Flame").Enabled, "water puts out the visible flame too")
    stim.emit("Fire", torch:get_position(), 1.0, 1.0)
    H.wait(0.1)
    test.check(flame:get("PointLight").Enabled, "fire re-ignites")
    stim.set_lights(torch, false)
    test.check(not flame:get("PointLight").Enabled, "stim.set_lights")
    local fireArrow = scene.instantiate("Prefabs/FireArrow.pprefab", vec3(10, 0.5, 0), vec3(0, 0, 0))
    H.wait(0)
    physics.set_velocity(fireArrow, vec3(0, 0, -30))
    H.wait(1.0)
    test.check(scene.find("PowderKeg") == nil, "fire arrow detonates the powder keg")
    stim.send(self.entity, "Fire", 0.5)
    test.check(self.stims["Fire"] ~= nil, "on_stim callback")

    -- ---------------------------------------------------------------- ui
    ui.text(10, 10, "Feature test", 2, { 1, 1, 1, 1 })
    ui.rect(0, 0, 10, 10)
    test.check(ui.width() > 0 and ui.height() > 0, "ui size")

    -- ---------------------------------------------------------------- lifecycle, collisions
    test.check(self.created, "on_create ran")
    test.check(self.updates > 10, "on_update runs every frame")
    test.check(scene.find("Bouncer"):script().collisions > 0, "on_collision callback")
    test.check(time.elapsed() > start, "time advances")
    test.check(test.failures() == 0, "no failures so far")
    test.complete()
end

function FeatureTest:on_create()
    self.created = true
    self.updates = 0
    self.stims = {}
    self.sequence = H.sequence(function() run(self) end)
end

function FeatureTest:on_stim(name, intensity, source)
    self.stims[name] = intensity
end

function FeatureTest:on_update(dt)
    self.updates = self.updates + 1
    self.sequence(dt)
end

function FeatureTest:on_destroy()
    log.info("feature test runner destroyed")
end

return FeatureTest
