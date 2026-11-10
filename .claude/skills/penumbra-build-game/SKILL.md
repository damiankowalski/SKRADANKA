---
name: penumbra-build-game
description: Build a complete game (or level) in the Penumbra engine end to end through the automation API and Lua - e.g. "make me a Tetris/stealth/platformer game". Use whenever asked to create or change game content rather than engine code.
---

# Building a game with Penumbra

Read `docs/Automation.md` (commands) and `docs/ScriptingAPI.md` (Lua) first. Engine code is not
changed for game work; if a feature is missing, add it to the engine first (with tests, see AGENTS.md).

1. **Build the tools once**: `Tools\Build.bat debug`. Executables: `build\debug\bin\PenumbraEditor.exe`, `PenumbraRuntime.exe`.
2. **Create the project** with a command file (JSON Lines) run headless:
   ```bat
   build\debug\bin\PenumbraEditor.exe --headless --exec my_commands.jsonl
   ```
   Start with `{"cmd":"project.new","path":"<dir>","name":"<Game>"}` and `{"cmd":"component.list"}`.
   If the user has the editor open, send the same lines to `127.0.0.1:7777` instead so they watch it happen.
3. **Author content**: `entity.create` with components (`Mesh` builtin primitives or glTF, `Material`,
   colliders, `RigidBody`, lights, `Camera`, `Environment`), `prefab.save` for repeated objects,
   `asset.write` for Lua scripts. Game rules live in scripts; HUD via `ui.text`/`ui.rect`.
   Grid games (Tetris, puzzles): one controller script that spawns/destroys block entities with
   `scene.instantiate` and positions them with `set_position`; input via `input.key_pressed`.
4. **Verify automatically** - never claim it works without this:
   - a test scene whose script drives the game (inject keys with `input.key` / call functions) and asserts
     with `test.check` + `test.complete`, run with `{"cmd":"test.run","scene":"Scenes/Test.pscene"}`;
   - `{"cmd":"render.screenshot","path":"shot.png"}` and look at the image;
   - `{"cmd":"script.errors"}` must be empty after `play` + `simulate`.
5. **Ship**: `scene.save`, `project.set {"startScene": ...}`, `project.export {"output": "<dir>"}`; the folder
   contains `<Game>.exe` + assets and runs standalone.

Report what was built, the test results (assertion counts) and where the exported game is.
