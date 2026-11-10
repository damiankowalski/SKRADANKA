# AGENTS.md — working on Penumbra

Penumbra is a C++20 / Direct3D 12 game engine for third-person stealth games. This file is the
contract for every human or AI agent changing this repository. Read it fully before editing.

## Repository map

| Path | What lives there |
|---|---|
| `Penumbra/src/Penumbra/Core` | Application loop, window (GLFW), input, logging, file system, UUID |
| `Penumbra/src/Penumbra/Scene` | ECS (EnTT): `Scene`, `Entity`, `Components.h`, JSON serialization, prefabs |
| `Penumbra/src/Penumbra/Asset` | glTF / texture importers, built-in primitives, `AssetManager` cache |
| `Penumbra/src/Penumbra/Renderer` | NVRHI D3D12 device, scene renderer, shaders in `Penumbra/shaders` |
| `Penumbra/src/Penumbra/Physics` | Jolt wrapper (`PhysicsWorld`): bodies, characters, raycasts, contacts |
| `Penumbra/src/Penumbra/Audio` | miniaudio wrapper (`AudioScene`) |
| `Penumbra/src/Penumbra/Scripting` | Lua 5.4 + sol2: `ScriptEngine`, the whole Lua API in `ScriptBindings.cpp` |
| `Penumbra/src/Penumbra/AI` | Silhouette-contrast perception (`PerceptionModel`, `PerceptionSystem`) |
| `Penumbra/src/Penumbra/Stim` | Act/React elemental interactions (`StimSystem`) |
| `Penumbra/src/Penumbra/Automation` | JSON command API used by the editor, CLI and AI agents |
| `PenumbraEditor` | Editor executable (ImGui, gizmos, command server) |
| `PenumbraRuntime` | Shipping game executable (no editor code) |
| `PenumbraTests` | doctest unit + integration tests |
| `DemoProject` | Demo stealth level and the `FeatureTest` scene covering every feature |
| `docs/` | Design documents (`GAME ENGINE SKRADANKA.md` is the original brief), API references, examples |
| `Tools/` | Build/test scripts, `BuildDemoProject.py` (regenerates demo scenes, prefabs, sounds) |
| `cmake/patches/` | Patches applied to pinned dependencies (each explains why) |

## Build and test

All commands run from the repository root in a normal `cmd`/PowerShell (the scripts locate MSVC):

```bat
Tools\Build.bat debug        :: or release / dist
Tools\Test.bat debug         :: unit tests + headless feature-test scene
build\debug\bin\PenumbraTests.exe --test-suite=Perception   :: one suite
build\debug\bin\PenumbraTests.exe --trace -tc="*torch*"     :: one case with engine trace logs
```

* Requirements: Visual Studio 2022+ with the C++ workload (CMake + Ninja come with it), Windows SDK.
* Dependencies are fetched by CMake (`cmake/Dependencies.cmake`), pinned to exact tags/commits.
* `/W4 /WX` is on for engine code: a warning is a build break. Third-party headers are `SYSTEM`.
* GPU tests use the WARP software rasterizer when no GPU exists (CI), so they run anywhere.
* Demo content is generated: edit `Tools/BuildDemoProject.py`, run `python Tools/BuildDemoProject.py`,
  commit the generated files. Lua scripts and the HDRI are hand-made.
* Crashes write a symbolized stack to the log and a minidump to `<exe dir>/CrashDumps`.

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
4. **Simulation order** (per frame, `Scene::RunRuntimeFrame`): scripts → physics (fixed 60 Hz) →
   contacts → stims → perception → smoke → audio → deferred destruction. Do not destroy entities
   immediately inside systems: `Scene::DestroyEntity` defers during updates.
5. **Shared lighting math.** Light falloff lives in `Scene/LightUtils.h` and is mirrored in
   `shaders/Lighting.hlsli`. Change both or neither — perception must agree with what is rendered.
6. **Tests are mandatory.** Every behaviour change ships with a doctest test in `PenumbraTests` (unit
   test for pure logic, scene-level integration test for systems). Every new Lua API function is also
   called from `DemoProject/Assets/Scripts/Tests/FeatureTest.lua`.
7. **Determinism for automation.** Headless runs use fixed timesteps (`--fixed-timestep`) so scripted
   tests are reproducible.

## Workflow

1. Branch from `main` (`feature/<topic>`), keep commits focused.
2. Build debug **and** release, run `Tools\Test.bat`.
3. Review your own diff before committing: style, error handling, tests, docs. Fix findings first.
4. Open a PR; CI (`.github/workflows/ci.yml`) must be green before merging (squash merge).

## Driving the engine as an agent

The editor and runtime are fully scriptable; see `docs/Automation.md`, `docs/ScriptingAPI.md`,
`docs/Perception.md` and the `penumbra-build-game` skill. In short: author scenes/prefabs as JSON or through the command API
(`PenumbraEditor --headless --exec commands.json`), write gameplay in Lua, verify with
`PenumbraRuntime --headless --scene <scene> --frames N` and scripted `test.*` assertions.
