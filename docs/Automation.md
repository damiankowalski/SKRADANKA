# Automation API

Everything an editor user can do is available as JSON commands, so tools and AI agents can build,
test and ship a game without a human. Implementation: `Penumbra/src/Penumbra/Automation/CommandProcessor.cpp`.

## Four ways in

| Way | Use |
|---|---|
| MCP server: `PenumbraEditor --mcp` (registered in `.mcp.json`) | Claude Code / Claude Desktop call every command as a typed tool; `render_screenshot` returns the image. See [MCP server](#mcp-server). |
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
4. Look at the result with `render.screenshot`; fix and iterate. To see the editor's or runtime's own UI, run it windowed with
   `--frames N --window-screenshot <file.png>` (and `PENUMBRA_USER_DIR` set to a scratch folder, see `DebugTools.md`).
5. `scene.save`, `project.set {startScene}`, `project.export {output}`.

`docs/examples/AgentQuickstart.jsonl` is a complete example and runs in CI. So does `docs/art/mood/MoodLab.jsonl`, which switches lighting variants
through `script.exec` and renders them from fixed cameras (`docs/art/mood/README.md`).

## MCP server

`PenumbraEditor --mcp` speaks the [Model Context Protocol](https://modelcontextprotocol.io) (JSON-RPC 2.0 over
stdin/stdout, protocol 2025-11-25, older versions negotiated). Implementation: `Penumbra/src/Penumbra/Automation/McpServer.cpp`.

* **Claude Code**: the repository's `.mcp.json` registers it as `penumbra`, running `build/release/bin/PenumbraEditor --mcp`
  on Windows and macOS alike (build release once). Start `claude` in the repository root, approve the server, then `/mcp`
  shows its tools. `Tools\PenumbraMcp.bat` / `Tools/PenumbraMcp.sh` start it from a debug build too (release if built,
  else debug; `PENUMBRA_MCP_BUILD=debug` forces debug), for clients configured by hand.
* **Claude Desktop**: add to `%APPDATA%\Claude\claude_desktop_config.json`:
  ```json
  { "mcpServers": { "penumbra": { "command": "C:\\path\\to\\Penumbra\\build\\release\\bin\\PenumbraEditor.exe", "args": ["--mcp"] } } }
  ```

**Tools.** Every command is a tool named with `_` for `.` (`entity.create` → `entity_create`); its input schema comes
from the command table (argument strings + the type table in `Automation/CommandSchema.cpp`, which a test keeps complete).
Read-only commands carry `readOnlyHint`. Command failures come back as tool results with `isError: true` and the
engine's message. `render_screenshot` returns the PNG as image content (`path` is optional: give it to also keep the file).

**Backends.** Where tools run is chosen with `--backend` (default `auto`):

| Backend | Behaviour |
|---|---|
| `editor` | Forwards every call to the running editor's command server (`127.0.0.1:7777`, `--port` to change), one connection per call. The user watches each change live and can undo it. |
| `headless` | An in-process engine without a window (`--project <dir>` opens a project at start, `--warp` renders without a GPU). |
| `auto` | On the first call: the editor if it answers, otherwise headless. |

The `session_backend` tool shows the current backend and switches it (`{"mode": "editor"}` after starting the editor).

**Resources.** `penumbra://components` (component list with defaults), `penumbra://scene` (the open scene document) and
`penumbra://docs/{Automation,ScriptingAPI,Components,Perception}.md`.

stdout carries only protocol messages: the server redirects the engine log (and anything else the process prints) to
stderr before starting. Tests: the `Mcp` doctest suite drives the server in memory (including the editor backend against a
real command server); CTest `Automation.McpStdio` runs the executable over stdio (`PenumbraTests/mcp/`).
