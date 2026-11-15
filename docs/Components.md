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
| Environment | HDRI map, ambient, exposure, fog (also the perception atmosphere), SSAO, bloom, `AmbientNoise` (0-10 background noise that masks quiet sounds for NPCs). |
| RigidBody | Static / Dynamic / Kinematic body, mass, friction, `IsTrigger`, `ContinuousCollision`, `TransparentToSight`. |
| BoxCollider, SphereCollider, CapsuleCollider, MeshCollider | Collision shapes (an entity with colliders and no RigidBody is static). |
| CharacterController | Walking character (player, guards); driven by `character.move/jump`. |
| Script | Lua script path + per-entity property overrides. |
| AudioSource, AudioListener | 3D sound; `NoiseLoudness` (0-10) makes it audible to NPCs. |
| Surface | What a floor or object is made of: `Material` (reported as `Footstep.<Material>` / `Impact.<Material>`), `NoiseModifier` (levels added to footsteps and impacts on it), `ImpactLoudness` + `ImpactSound` for dynamic objects that clatter when they hit something. |
| NavMesh | Navmesh bake settings (cell size, agent radius/height/climb/slope); baked from static colliders at start. Runtime: `Polygons`, `BuildMilliseconds`. |
| NavAgent | Walks a CharacterController along the navmesh (`nav.move_to`): `Speed`, `TurnSpeed`, `StoppingDistance`, `FaceMovement`. Agents give way to each other: keep right when meeting head on, go round one who stands, stop beside a destination another agent stands on. Runtime: `Status`, `Destination`, `RemainingDistance`, `PartialPath`. |
| Perception | NPC eyes and ears (see `Perception.md`): vision, hearing sensitivity, reaction duration and state levels, `Diligence` against distractions, `BodyAlarm`, `ProjectileAlarm`, touch (`TouchRange`, `TouchAlarm`) and close range (`CloseRange`, `CloseRangeStimulus`); runtime: suspicion, state, reaction strength/time, stimulus kind/cause/position, alertness. |
| PerceptionTarget | Something NPCs can see: `Kind` Intruder (the player), Body (a downed NPC, found once) or Projectile (an arrow in flight, judged every frame); `Emissive` for things that glow; silhouette points, reflectance; runtime: light level, exposure. |
| Vitality | `State` Awake / Unconscious / Dead (`ai.knock_out`, the `Knockout` stim action) and `Health` (the `Damage` stim action, `ai.damage`; 0 = Dead). Downed NPCs perceive nothing and lie where they fell. |
| Flame | Procedural animated fire (height, radius, colour, intensity, speed); doused/ignited together with lights. |
| SmokeVolume | Sphere of smoke that hides silhouettes (and renders as smoke); optional lifetime. |
| StimSource, StimResponse | Act/React: emit named stims (Fire, Water, ...) by contact / radius / continuously; react with rules (extinguish, ignite, explode, spawn, impulse, sound, noise, script event, knockout, damage, alarm: an NPC hit searches where the stim came from). |
| Prefab | Records which prefab an entity tree came from. |
