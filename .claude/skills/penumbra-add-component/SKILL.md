---
name: penumbra-add-component
description: Add a new ECS component to Penumbra so it is serializable, editable in the editor, scriptable from Lua and reachable through the automation API.
---

# Adding a component

1. **Declare** the struct in `Penumbra/src/Penumbra/Scene/Components.h`.
   - Authorable fields first, with sensible defaults and a one-line comment for non-obvious units.
   - Runtime-only state goes last, prefixed `Runtime` (never saved, read-only in generic APIs).
2. **Register** it in `Penumbra/src/Penumbra/Scene/ComponentRegistry.cpp`:
   - add a `VisitFields` overload listing every field: `v("Name", c.Name);` and for runtime fields
     `v("Name", c.RuntimeName, FieldKind::Runtime);`
   - add `registry.push_back(MakeInfo<YourComponent>("YourName"));` in `BuildRegistry()`.
   - Enums need an `EnumNames<>` specialization in `Scene/JsonSerialization.h`.
3. **Copy support**: append the type to `AllComponents` at the bottom of `Components.h`
   (play-mode scene copies and entity duplication use it).
4. **Behaviour**: implement it in the owning system (physics, audio, AI, stim, renderer...). Systems
   must tolerate the component being added or removed at runtime.
5. **Editor**: the generic property inspector picks up fields automatically from the registry.
   Add a custom widget in `PenumbraEditor/src/Panels/PropertiesPanel.cpp` only if needed.
6. **Tests** (mandatory):
   - `SerializationTests.cpp` round-trips every registered component automatically — make sure it passes.
   - Add behaviour tests for the system in the matching `PenumbraTests/src/*Tests.cpp`.
   - Exercise it from Lua in `DemoProject/Assets/Scripts/Tests/FeatureTest.lua`.
7. **Docs**: add the component and its fields to `docs/Components.md`.
