# AGENTS.md — working on Penumbra

Penumbra is a C++20 game engine for third-person stealth games: Direct3D 12 on Windows (where the game
ships) and Vulkan, which also runs it on macOS through MoltenVK (an equal development platform). This file is the
contract for every human or AI agent changing this repository. Read it fully before editing.

## Repository map

| Path | What lives there |
|---|---|
| `Penumbra/src/Penumbra/Core` | Application loop, window (GLFW), input, logging, file system, UUID |
| `Penumbra/src/Penumbra/Scene` | ECS (EnTT): `Scene`, `Entity`, `Components.h`, JSON serialization, prefabs |
| `Penumbra/src/Penumbra/Asset` | glTF / texture importers (meshes, skins, clips), built-in primitives, `AssetManager` cache |
| `Penumbra/src/Penumbra/Animation` | Keyframe sampling, pose blending, skinning palettes, `AnimationSystem` (Animator component) |
| `Penumbra/src/Penumbra/Renderer` | NVRHI device (D3D12 and Vulkan backends), scene renderer, shaders in `Penumbra/shaders` |
| `Penumbra/src/Penumbra/Physics` | Jolt wrapper (`PhysicsWorld`): bodies, characters, raycasts, contacts |
| `Penumbra/src/Penumbra/Audio` | miniaudio wrapper (`AudioScene`) |
| `Penumbra/src/Penumbra/Scripting` | Lua 5.4 + sol2: `ScriptEngine`, the whole Lua API in `ScriptBindings.cpp` |
| `Penumbra/src/Penumbra/AI` | Silhouette-contrast perception, hearing (0-10 loudness) and reactions (`PerceptionModel`, `PerceptionSystem`); the player's fog of war (`FogOfWarSystem`) |
| `Penumbra/src/Penumbra/Navigation` | Recast/Detour navmesh (`NavMesh`) and agent steering (`NavigationSystem`) |
| `Penumbra/src/Penumbra/Stim` | Act/React elemental interactions (`StimSystem`) |
| `Penumbra/src/Penumbra/Debug` | Testing tools: performance monitor, freelook camera, debug overlays (view fans, noise rings), noise probe, object inspector |
| `Penumbra/src/Penumbra/ImGui` | Dear ImGui host and NVRHI renderer (editor UI, the runtime's debug panel), component field widgets |
| `Penumbra/src/Penumbra/Automation` | JSON command API used by the editor, CLI and AI agents; TCP command server; MCP server |
| `PenumbraEditor` | Editor executable (ImGui, gizmos, command server) |
| `PenumbraRuntime` | Shipping game executable (no editor code); F3-F7 testing tools (`docs/DebugTools.md`) |
| `PenumbraTests` | doctest unit + integration tests |
| `DemoProject` | Demo stealth level and the `FeatureTest` scene covering every feature |
| `docs/` | Design documents (`GAME ENGINE SKRADANKA.md` is the original brief), API references, examples |
| `Tools/` | Build/test scripts, `BuildDemoProject.py` (regenerates demo scenes, prefabs, sounds) |
| `cmake/patches/` | Patches applied to pinned dependencies (each explains why) |

## Build and test

All commands run from the repository root. Windows, in a normal `cmd`/PowerShell (the scripts locate MSVC):

```bat
Tools\Build.bat debug        :: or release / dist
Tools\Test.bat debug         :: unit tests + headless feature-test scene
build\debug\bin\PenumbraTests.exe --test-suite=Perception   :: one suite
build\debug\bin\PenumbraTests.exe --trace -tc="*torch*"     :: one case with engine trace logs
```

macOS (setup in `docs/macOS.md`): `Tools/Build.sh debug`, `Tools/Test.sh debug`, `build/debug/bin/PenumbraTests`.

* Requirements: Windows: Visual Studio 2022+ with the C++ workload (CMake + Ninja come with it), Windows SDK.
  macOS: Xcode command line tools, CMake, Ninja and the LunarG Vulkan SDK (`docs/macOS.md`).
* Dependencies are fetched by CMake (`cmake/Dependencies.cmake`), pinned to exact tags/commits.
* `/W4 /WX` (clang: `-Wall -Wextra -Werror`) is on for engine code: a warning is a build break. Third-party
  headers are `SYSTEM`.
* Platform code stays behind `#ifdef _WIN32` / `__APPLE__` in a few files (`Core/Platform`, `Core/FileSystem`,
  `Core/CrashHandler`, `Automation/CommandServer`, the render backends). Everything else is portable C++20.
* Rendering changes are checked on both backends: `PENUMBRA_GRAPHICS_API=vulkan` runs the tests on Vulkan on
  Windows. Shaders carry `PN_PUSH_CONSTANT` on push-constant buffers and follow the SPIR-V binding rules in
  `cmake/Shaders.cmake`.
* GPU tests use the WARP software rasterizer when no GPU exists (Windows CI), so they run anywhere.
* Demo content is generated: edit `Tools/BuildDemoProject.py`, run `python Tools/BuildDemoProject.py`,
  commit the generated files. Lua scripts and the HDRI are hand-made. Character models come from
  `Tools/ImportCharacters.py` (KayKit, CC0; see `DemoProject/Assets/Models/Characters/README.md`).
* Crashes write a symbolized stack to the log and (Windows) a minidump to `<exe dir>/CrashDumps`.

## Code style (Hazel conventions)

* Types, functions, methods, public members, enum values: `PascalCase`.
* Members `m_Name`, statics `s_Name`, locals and parameters `camelCase`, macros `PN_UPPER_CASE`.
* Tabs for indentation, Allman braces, `#pragma once`, `namespace Penumbra {` with indented contents.
* Every `.cpp` starts with `#include "pnpch.h"` then its own header. Headers must compile on their
  own (include `Penumbra/Core/Base.h` instead of relying on the precompiled header).
* Ownership: `Scope<T>` (unique) / `Ref<T>` (shared). No naked `new` except NVRHI/Jolt ref-counted objects.
* `.clang-format` encodes the formatting; run it on files you touch.

## Engineering rules

1. **Errors are values.** Missing files, bad JSON, invalid component data, script errors: return
   `false`/`std::nullopt` with a message and log it. Never crash on user content. `PN_CORE_ASSERT`
   is only for programmer errors (broken invariants).
2. **One source of truth for components.** A new component is added to `Components.h`, its fields to
   `ComponentRegistry.cpp` (`VisitFields`), and to `AllComponents`. That single registration makes it
   serializable, editable in the editor, scriptable from Lua (`entity:get/set/add`) and reachable from
   the automation API. See `.claude/skills/penumbra-add-component`.
3. **Runtime state is separate.** Fields prefixed `Runtime` are never written to scene files and are
   read-only through the generic APIs.
4. **Simulation order** (per frame, `Scene::RunRuntimeFrame`): scripts → navigation agents → physics
   (fixed 60 Hz) → contacts (collisions, stims, impact noise) → stims → perception → smoke → audio →
   ragdolls → animation → fog of war → deferred destruction. Do not destroy entities
   immediately inside systems: `Scene::DestroyEntity` defers during updates.
5. **Shared lighting math.** Light falloff lives in `Scene/LightUtils.h` and is mirrored in
   `shaders/Lighting.hlsli`. Change both or neither — perception must agree with what is rendered.
   Likewise the fog of war's coverage (`FogOfWarView::Coverage` / `shaders/FogOfWar.hlsli`) and the fog's density
   (`Scene/FogUtils.h` / `shaders/Fog.hlsli` and `Fog.hlsl`): guards see through the fog the player sees.
6. **Tests are mandatory.** Every behaviour change ships with a doctest test in `PenumbraTests` (unit
   test for pure logic, scene-level integration test for systems). Every new Lua API function is also
   called from `DemoProject/Assets/Scripts/Tests/FeatureTest.lua`.
7. **Determinism for automation.** Headless runs use fixed timesteps (`--fixed-timestep`) and the scene's
   fixed random seed (`Scene::SetRandomSeed`, `--seed`; Lua `math.random` follows it) so scripted tests are
   reproducible. Engine-side rolls use `Scene::NextRandom`, never an unseeded generator.

## Workflow

1. Branch from `main` (`feature/<topic>`), keep commits focused.
2. Build debug **and** release, run `Tools\Test.bat` (or `Tools/Build.sh` + `Tools/Test.sh` on a Mac; CI builds
   both platforms).
3. Review your own diff before committing: style, error handling, tests, docs. Fix findings first.
4. Open a PR; CI (`.github/workflows/ci.yml`) must be green before merging (squash merge).

## Working with the owner

The owner (GitHub `damiankowalski`) plans in a claude.ai project synced with this repository and builds
with Claude Code on Windows and macOS. The repository is the only memory those share: rules and project
state live here (this section, `docs/Roadmap.md`), never only in an agent's local memory, which may hold
machine-specific notes (paths, toolchain quirks) and nothing else.

* Talk to the owner in **Polish**. Code, comments, commits, PRs and docs stay in English.
* Always say where the work happens (branch, worktree, PR) and what is waiting on the owner.
* One PR at a time. No parallel agent sessions unless the owner asks for them.
* Ask before merging: the owner often plays the build first. Merge only with green CI, squash merge.
* Before deleting a branch, check in the same command that its PR is `MERGED`
  (`st=$(gh pr view N --json state --jq .state); [ "$st" = MERGED ] && git push origin --delete <branch>`):
  deleting the head branch of an open PR closes it.
* Never enable GitHub auto-merge unless `main` requires the CI checks. Don't poll CI in loops.
* Gameplay ideas from the owner get an estimate (S/M/L, engine areas, dependencies, risks) and a clear
  flag when they go beyond what the engine can do today. They enter `docs/Roadmap.md` only once approved.
* Never use code from outside this repository as a reference (fetched dependencies are part of the build).
  Never capture the owner's desktop: screenshots come from the engine (`--screenshot`, `render.screenshot`).
* The project's state is `docs/Roadmap.md` (Done / In progress / Next): the PR that finishes a package
  updates it.

## Driving the engine as an agent

The editor and runtime are fully scriptable; see `docs/Automation.md`, `docs/ScriptingAPI.md`,
`docs/Perception.md` and the `penumbra-build-game` skill. In short: author scenes/prefabs as JSON or through the command API
(`PenumbraEditor --headless --exec commands.json`, or the `penumbra` MCP server from `.mcp.json`, which drives the open
editor live), write gameplay in Lua, verify with
`PenumbraRuntime --headless --scene <scene> --frames N` and scripted `test.*` assertions.
