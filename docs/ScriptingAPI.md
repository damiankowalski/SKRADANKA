# Lua scripting API

Gameplay is written in Lua 5.4. Bindings: `Penumbra/src/Penumbra/Scripting/ScriptBindings.cpp`.
Every function below is exercised by `DemoProject/Assets/Scripts/Tests/FeatureTest.lua`.

## Script shape

```lua
local Door = { Properties = { speed = 2.0, open = false } } -- defaults, overridable per entity
function Door:on_create() end                    -- after the scene (or the entity) appears
function Door:on_update(dt) end                  -- every frame
function Door:on_destroy() end
function Door:on_collision(other) end            -- physics contact began (other: Entity)
function Door:on_stim(stim, intensity, source) end
function Door:on_stim_event(name) end            -- from a ScriptEvent stim action
return Door
```

`self.entity` is the owning entity. Errors are logged and disable only the failing instance.
`require("Scripts/Util")` loads `<Assets>/Scripts/Util.lua`. `io`/`os` are not available.

## Types

**vec3** — `vec3(x,y,z)`, `vec3(s)`, `vec3()`; fields `x y z`; `+ - * /`, unary `-`, `==`, `tostring`;
`:length() :normalized() :dot(v) :cross(v) :distance(v) :lerp(v,t)`.

**Entity** — `.id`, `:valid()`, `:get_name()/:set_name(s)`, `:get_position()/:set_position(v)` (world),
`:get_local_position()/:set_local_position(v)`, `:get_rotation()/:set_rotation(deg)`, `:get_scale()/:set_scale(v)`,
`:forward() :right() :up()`, `:look_at(target, yawOnly?)`, `:has(c) :get(c) :set(c, table) :add(c, table?) :remove(c)`
(any component by name, e.g. `e:set("PointLight", {Intensity = 5})`; `get` includes runtime state such as
`Perception.Suspicion`), `:parent() :children() :set_parent(e|nil)`, `:destroy()`, `:script()` (other script's instance).

## Modules

| Module | Functions |
|---|---|
| `scene` | `create(name?)`, `find(name)`, `get(id)`, `entities_with(component)`, `instantiate(prefab, pos?, rotDeg?)`, `destroy(e)`, `load(path)`, `quit(code?)`, `count()` |
| `input` | `key(name)`, `key_pressed(name)`, `key_released(name)`, `mouse_button(btn)`, `mouse_pressed(btn)`, `mouse_position()`, `mouse_delta()`, `scroll()`, `set_cursor_locked(bool)` — keys: `"A".."Z"`, `"0".."9"`, `"F1".."F12"`, `Space`, `Escape`, `Enter`, `LeftShift`, ...; buttons `Left/Right/Middle` |
| `physics` | `raycast(origin, dir, max, ignore?)` → `{entity, point, normal, distance}` or nil, `spherecast(origin, dir, radius, max, ignore?)`: the same for a swept sphere (a thick ray; distance is how far its centre travelled, 0 when it starts touching; sensors are skipped), `overlap_sphere(c, r)`, `ragdoll_start(e)` → bool (hands a Ragdoll body to physics from its pose), `ragdoll_stop(e)` (back to the animation, e.g. to carry it), `ragdoll_active(e)`, `ragdoll_impulse(e, impulse, point)` (on the nearest part; `e:set_position` on a limp body moves it whole, in the pose it lay in), `add_impulse(e,v)`, `add_force(e,v)`, `set_velocity(e,v)`, `get_velocity(e)`, `radial_impulse(c,r,s)`, `get_gravity()`, `set_gravity(v)` |
| `character` | `move(e, velocity)`, `jump(e, speed)`, `is_grounded(e)`, `get_velocity(e)` |
| `audio` | `play(e)`, `stop(e)`, `is_playing(e)`, `play_at(clip, pos, volume?, noise_loudness? 0-10)` |
| `ai` | `state(e)` (`Unaware/Suspicious/Investigating/Searching/Alarmed`), `reaction(e)` → `{state, strength, time_left, kind, cause, position?}` (kind `Heard/Seen/Reported/None`), `notify(e, pos, strength, cause?)` → taken?, `explain_away(strength, cause, {center?, radius?, spread?}?)` → guards who put what they heard down to it, `calm(e)`, `knock_out(e, dead?)` → was up?, `damage(e, amount)` → health left (0 = dead), `vital_state(e)` (`Awake/Unconscious/Dead`), `shout(e, loudness?)` → NPCs who took the news, `alertness(e)`, `suspicion(e)`, `set_suspicion(e,v)`, `last_stimulus(e)`, `visibility(guard, target)` (all perception factors), `luminance(pos)`, `emit_noise(pos, loudness 0-10, source?, cause?)`, `hearing(guard, pos, loudness)` → level reaching him, `noise_range(loudness)` → metres (docs/Perception.md) |
| `fog` | Fog of war (`FogOfWar.md`), answers from the last finished frame: `active()` → is there fog (a viewer with an enabled FogOfWar, not switched off for testing), `can_see(pos or entity)` → does the player see it now (in his cone or circle and nothing solid in between; an entity: any of its silhouette points or points up its height; true without fog), `coverage(pos)` → 0-1, how much the point lies in his cone or circle and range (walls ignored), `hidden(e)` → is it left out of the picture (a character out of sight, or a part of one). Errors outside play. |
| `nav` | `move_to(e, pos, speed?)` → path found? (walks a CharacterController along the navmesh; adds a NavAgent), `stop(e)`, `status(e)` (`Idle/Moving/Arrived/Failed/Stuck`), `remaining(e)`, `path(from, to)` → `{points, length, complete}` or nil, `distance(from, to)` → walking distance or nil, `closest_point(pos, radius?)`, `random_point(center, radius)` (reachable, deterministic), `rebuild()`, `polygon_count()`. `character.move` on a moving agent takes it back from navigation. |
| `stim` | `emit(name, pos, radius, intensity, source?)`, `send(e, name, intensity, source?)`, `set_lights(e, enabled)` |
| `anim` | `play(e, clip, fade?, loop?)` (crossfades over `fade` s, default the Animator's `BlendDuration`; replaying the current clip only restarts a finished one-shot; unknown clips error), `set_speed(e, rate)`, `is_playing(e, clip?)`, `clips(e)` → names. `e` needs an Animator (on the entity with the Mesh). |
| `ui` | `text(x, y, text, scale?, {r,g,b,a}?)`, `rect(x, y, w, h, color?)`, `width()`, `height()`, `world_to_screen(pos)` → x, y (nil behind the camera), `scale()`: the pixel HUD works in logical pixels of a 1080-line screen; taller viewports (a Retina Mac, 4K) scale it up by `scale()` = height / 1080 (never below 1), so `width()` × `height()` is the logical size; `world_rect(center, w, h, color?)` and `world_text(center, text, height?, color?)`: hanging in the scene, facing the camera, sizes in metres (letter height, default 0.05), depth tested so walls and characters in front hide them |
| `time` | `delta()`, `elapsed()`, `frame()` |
| `game` | `get(key, default?)`, `set(key, value)` (boolean, number, string, vec3; `nil` erases), `clear()` — values kept across `scene.load` (the chosen difficulty, a score); a new play session starts empty, `PenumbraRuntime --set difficulty=Hard,score=0` seeds them |
| `log` | `trace/info/warn/error(...)`, `print(...)` |
| `test` | `check(cond, msg?)`, `equal(a, b, msg?)`, `near(a, b, eps?, msg?)`, `complete()`, `failures()` |

Freshly spawned entities get physics bodies in the same frame's physics step: set velocities on the next frame
(see `Player.lua`, `pending_arrow`).

`math.random` is seeded from the scene's random seed when the game starts (fixed in tests and headless
runs, new for every interactive scene load, `PenumbraRuntime --seed N` to replay one), so scripted
randomness is reproducible.
