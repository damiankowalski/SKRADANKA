# Automation API

Everything an editor user can do is available as JSON commands, so tools and AI agents can build,
test and ship a game without a human. Implementation: `Penumbra/src/Penumbra/Automation/CommandProcessor.cpp`.

## Three ways in

| Way | Use |
|---|---|
| `PenumbraEditor --headless --exec cmds.jsonl [--project dir] [--warp]` | Batch, no window. Prints one JSON response per line; exit code 0 only if every command succeeded. |
| Running editor, TCP `127.0.0.1:7777` (`--port`) | One JSON request per line, one JSON response line back. Executes on the editor's main thread; the editor shows every change live and each command is undoable. |
| Editor console | Type a JSON command (edit mode) or Lua (play mode). |

Request: `{"cmd": "<name>", ...arguments}` → response `{"ok": true, "result": ...}` or `{"ok": false, "error": "..."}`.
JSON-Lines files may contain `#` comments; a JSON array of requests also works. A batch stops at the first failure
(`--continue-on-error` to keep going).

## Commands (`{"cmd":"help"}` prints this list with arguments)

| Area | Commands |
|---|---|
| Project | `project.new {path,name}`, `project.open {path}`, `project.info`, `project.set {name?,startScene?,windowWidth?,windowHeight?}`, `project.export {output}` |
| Scene | `scene.new`, `scene.open {path}`, `scene.save {path?}`, `scene.get`, `scene.list {component?}` |
| Entities | `entity.create {name?,parent?,components?}`, `entity.get {id\|name,runtime?}`, `entity.set {id\|name,components}`, `entity.remove_component`, `entity.destroy`, `entity.duplicate`, `entity.set_parent {parent\|null}`, `entity.select` |
| Data | `component.list` (every component with defaults), `prefab.save {id\|name,path}`, `prefab.instantiate {path,position?,rotation?}` |
| Assets | `asset.list {directory?}`, `asset.read {path}`, `asset.write {path,content}`, `asset.import {source,destination}` |
| Play | `play`, `stop`, `pause {paused}`, `simulate {seconds\|frames,dt?}`, `input.key {key,down}`, `input.mouse {button?,down?,x?,y?}`, `script.exec {code}`, `script.errors` |
| Verify | `test.run {scene,seconds?}` (fails unless the scene's `test.*` assertions all pass), `render.screenshot {path,width?,height?,camera?}`, `log.tail {count?}` |

Components are addressed by name with partial field updates: `{"Mesh": {"Mesh": "builtin:sphere"}}`
merges into an existing Mesh component. Unknown components/fields and wrong types are rejected with a
precise message, and a failed update leaves the entity untouched. Rotations are Euler degrees. Entity ids
are decimal strings. Asset paths are relative to the project's asset directory and may not escape it.

## Recommended agent loop

1. `project.new` / `project.open`; `component.list` to learn the data model.
2. Build the level with `entity.create` / `prefab.instantiate`; write gameplay with `asset.write` (Lua, see `ScriptingAPI.md`).
3. Write a test scene whose scripts call `test.check`/`test.complete`; verify with `test.run`.
4. Look at the result with `render.screenshot`; fix and iterate.
5. `scene.save`, `project.set {startScene}`, `project.export {output}`.

`docs/examples/AgentQuickstart.jsonl` is a complete example and runs in CI.
