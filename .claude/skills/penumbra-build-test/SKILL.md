---
name: penumbra-build-test
description: Build Penumbra and run its tests (unit, integration, headless feature-test scene). Use after any engine, script or scene change and before every commit.
---

# Build and test Penumbra

1. Build both configurations that CI checks:
   ```bat
   Tools\Build.bat debug
   Tools\Build.bat release
   ```
   `/W4 /WX` is enabled: fix every warning, never suppress one in engine code without a comment explaining why.
2. Run the test suites:
   ```bat
   Tools\Test.bat debug
   ```
   This runs every doctest suite (`Unit.*`) and the headless `FeatureTest` scene (`Scene.FeatureTest`).
3. Narrow down failures:
   ```bat
   build\debug\bin\PenumbraTests.exe --test-suite=Physics
   build\debug\bin\PenumbraTests.exe --trace -tc="*keg*"
   ```
   `--trace` enables engine trace logging (stim deliveries, reactions, asset loads).
4. A shipping build must also compile: `Tools\Build.bat dist` (no editor, no tests, asserts off).

Report the exact pass/fail counts. Never claim tests pass without running them.
