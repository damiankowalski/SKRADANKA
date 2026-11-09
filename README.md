# Penumbra

A 3D game engine for Windows built for third-person stealth games, where guards notice you the way
eyes do: by the **contrast of your silhouette against what is behind you**, not by a "you are in
shadow" flag.

* C++20, CMake, Direct3D 12 through [NVRHI](https://github.com/NVIDIA-RTX/NVRHI), GLFW, glm
* PBR renderer: glTF assets, image-based lighting from HDRIs, soft shadows, SSAO, HDR + tonemapping
* [Jolt](https://github.com/jrouwe/JoltPhysics) physics, [miniaudio](https://github.com/mackron/miniaudio) audio
* Lua 5.4 gameplay scripting (sol2), EnTT entity-component system, JSON scenes and prefabs
* Silhouette-contrast NPC perception with eye adaptation, peripheral/motion sensitivity, smoke,
  hearing and a suspicion meter (`docs/GAME ENGINE SKRADANKA.md`, `docs/Perception.md`)
* Act/React elemental system: water arrows douse torches, fire arrows ignite powder kegs
* Editor with gizmos, play mode and a JSON automation API so AI agents can build games end-to-end
* One-command export of a standalone game

## Quick start

```bat
Tools\Build.bat debug
Tools\Test.bat debug
```

Requires Visual Studio 2022 or newer with the "Desktop development with C++" workload.
See [AGENTS.md](AGENTS.md) for the architecture, conventions and workflow.
