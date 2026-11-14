# Penumbra

A 3D game engine built for third-person stealth games (shipping on Windows, developed on Windows and macOS), where guards notice you the way
eyes do: by the **contrast of your silhouette against what is behind you**, not by a "you are in
shadow" flag.

* C++20, CMake, Direct3D 12 and Vulkan (MoltenVK on macOS) through [NVRHI](https://github.com/NVIDIA-RTX/NVRHI), GLFW, glm
* PBR renderer: glTF assets, image-based lighting from HDRIs, soft shadows, SSAO, HDR + tonemapping
* Skeletal animation: glTF skins and clips (step/linear/cubic), crossfades, GPU (compute) skinning feeding every pass
* [Jolt](https://github.com/jrouwe/JoltPhysics) physics, [miniaudio](https://github.com/mackron/miniaudio) audio
* Lua 5.4 gameplay scripting (sol2), EnTT entity-component system, JSON scenes and prefabs
* Silhouette-contrast NPC perception with eye adaptation, peripheral/motion sensitivity, smoke,
  hearing and a suspicion meter (`docs/GAME ENGINE SKRADANKA.md`, `docs/Perception.md`)
* Guards that patrol and search on a [Recast/Detour](https://github.com/recastnavigation/recastnavigation)
  navmesh, hear on a 0-10 loudness scale (sound travels around corners, floors change footsteps) and
  react for as long as the stimulus was strong; area events (a cook at work) and a storm compete with the
  player for their attention
* Act/React elemental system: water arrows douse torches, fire arrows ignite powder kegs
* Editor with gizmos, play mode and a JSON automation API so AI agents can build games end-to-end
* One-command export of a standalone game

## Quick start

```bat
Tools\Build.bat debug
Tools\Test.bat debug
```

Play the demo (WASD, mouse, Shift run, C crouch, 1-4 water/fire/gas/moss arrow, left click shoot, E use / open doors / pick locks / carry a body,
F blackjack from behind):

```bat
build\debug\bin\PenumbraRuntime.exe --project DemoProject
```

Other scenes start with `--scene`: `Scenes/Cloister.pscene` (cross a garden ringed by corridors from the east
wing to the west, past two guards and four pairs of doors), `Scenes/PhysicsLab.pscene` (push a table down a ramp
or shoot the copper jug off it), `Scenes/AILab.pscene`, `Scenes/PerceptionLab.pscene`:

```bat
build\debug\bin\PenumbraRuntime.exe --project DemoProject --scene Scenes/Cloister.pscene
```

Open it in the editor (`F5` plays, gizmo W/E/R, right-drag + WASD flies):

```bat
build\debug\bin\PenumbraEditor.exe --project DemoProject
```

`--project` accepts a folder or `project.json`; relative paths are found from the current folder or from any
folder above the executable, so the commands work from the repository root and from `build\debug\bin`.

Requires Visual Studio 2022 or newer with the "Desktop development with C++" workload. On a Mac (Apple Silicon):
`Tools/Build.sh debug`, `Tools/Test.sh debug`, executables without `.exe`; setup in [docs/macOS.md](docs/macOS.md).
Docs: `docs/Perception.md`, `docs/ScriptingAPI.md`, `docs/Automation.md`, `docs/Components.md`; plans in `docs/Roadmap.md`.
See [AGENTS.md](AGENTS.md) for the architecture, conventions and workflow.
