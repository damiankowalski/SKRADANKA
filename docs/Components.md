# Components

The authoritative field list with defaults is generated at runtime: `{"cmd":"component.list"}`
(or the editor's Add Component menu). Definitions: `Penumbra/src/Penumbra/Scene/Components.h`.
Fields prefixed `Runtime` in C++ are read-only state, visible through `entity.get {runtime:true}` and `Entity:get()`.

| Component | Purpose |
|---|---|
| Tag, Transform | Name; local translation / rotation (Euler degrees) / scale. Always present. |
| Camera | Perspective camera; the `Primary` one renders the game. |
| Mesh, Material | `builtin:cube/sphere/plane/capsule/cylinder` or a glTF path; `HiddenNodes` lists source-file node names not to draw (unused weapons). Material overrides all mesh materials (PBR, emission, textures, translucency). |
| Animator | Plays the skeletal clips of a skinned/animated glTF on the same entity: `Clip`, `Speed`, `Loop`, `Playing`, `BlendDuration` (crossfade seconds when `Clip` changes); runtime: `CurrentClip`, `Time`, `Blend`, `Finished`. Driven from Lua with `anim.*`. |
| DirectionalLight, PointLight, SpotLight | Lights; `CastShadows`, `Flicker` (torches), `Enabled`. |
| Environment | HDRI map, ambient, exposure, fog (also the perception atmosphere), SSAO, bloom. |
| RigidBody | Static / Dynamic / Kinematic body, mass, friction, `IsTrigger`, `ContinuousCollision`, `TransparentToSight`. |
| BoxCollider, SphereCollider, CapsuleCollider, MeshCollider | Collision shapes (an entity with colliders and no RigidBody is static). |
| CharacterController | Walking character (player, guards); driven by `character.move/jump`. |
| Script | Lua script path + per-entity property overrides. |
| AudioSource, AudioListener | 3D sound; `NoiseRadius` makes it audible to NPCs. |
| Perception | NPC eyes and ears (see `Perception.md`); runtime: suspicion, state, last stimulus. |
| PerceptionTarget | Something NPCs can see (the player): silhouette points, reflectance; runtime: light level, exposure. |
| SmokeVolume | Sphere of smoke that hides silhouettes (and renders as smoke); optional lifetime. |
| StimSource, StimResponse | Act/React: emit named stims (Fire, Water, ...) by contact / radius / continuously; react with rules (extinguish, ignite, explode, spawn, impulse, sound, noise, script event). |
| Prefab | Records which prefab an entity tree came from. |
