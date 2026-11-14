# Developing on macOS

The game ships on Windows (Direct3D 12). A Mac with Apple Silicon is an equal place to develop it: the
engine, editor, runtime and tests build there with clang and render through **Vulkan on MoltenVK** (Vulkan
on top of Metal). The same HLSL shaders are compiled to SPIR-V, the same Lua scripts and scenes run, and
perception is computed on the CPU, so guards see exactly what they see on Windows.

## One-time setup

1. **Xcode command line tools** (clang, git): `xcode-select --install`
2. **Homebrew** from <https://brew.sh>, then:
   ```bash
   brew install cmake ninja python gh
   ```
3. **LunarG Vulkan SDK for macOS** from <https://vulkan.lunarg.com/sdk/home#mac>: run the installer and tick
   *System Global Installation* (it puts the Vulkan loader and MoltenVK where the engine finds them). Then make
   the SDK's `dxc` (the HLSL compiler) visible to CMake by adding its setup script to `~/.zprofile`:
   ```bash
   echo 'source ~/VulkanSDK/<version>/setup-env.sh' >> ~/.zprofile
   ```
   and open a new terminal (`echo $VULKAN_SDK` must print a path). `setup-env.sh` also points the Vulkan loader at
   MoltenVK and the validation layer, which is what lets an SDK installed without the system-wide option work:
   *Cannot create a Vulkan instance (VK_ERROR_INCOMPATIBLE_DRIVER)* means the loader found neither. Check with
   `vulkaninfo --summary`, which must list an Apple GPU.
4. **The repository**:
   ```bash
   gh auth login
   git clone https://github.com/damiankowalski/SKRADANKA.git ~/DEV/SKRADANKA
   cd ~/DEV/SKRADANKA
   ```
5. **Build and test** (the first build downloads the pinned dependencies):
   ```bash
   Tools/Build.sh debug
   Tools/Test.sh debug
   ```
6. **Play / edit**:
   ```bash
   build/debug/bin/PenumbraRuntime --project DemoProject
   build/debug/bin/PenumbraEditor --project DemoProject
   ```

`Tools/Build.sh release` builds `build/release`, which the `penumbra` MCP server in `.mcp.json` uses.

## What differs from Windows

| | Windows | macOS |
|---|---|---|
| Graphics | Direct3D 12 (`--vulkan` switches to Vulkan) | Vulkan through MoltenVK |
| Shaders | DXIL and SPIR-V | SPIR-V |
| Scripts | `Tools\Build.bat`, `Tools\Test.bat` | `Tools/Build.sh`, `Tools/Test.sh` (presets `macos-debug`, `macos-release`) |
| Executables | `PenumbraRuntime.exe` | `PenumbraRuntime` |
| Crashes | stack in the log + minidump in `CrashDumps` | stack on stderr + macOS crash report (Console.app) |
| `--warp` | software rasterizer (CI) | ignored |
| Shipping (`Tools\Build.bat dist`, export) | yes | no: export on a Mac makes a Mac build for testing only |

The window renders one pixel per point; on a Retina display macOS scales the picture up. Input, ImGui and
the renderer then use the same coordinates as on Windows.

## Both backends on one Windows machine

`PenumbraRuntime --vulkan`, `PenumbraEditor --vulkan` or the environment variable
`PENUMBRA_GRAPHICS_API=vulkan` (which every test honours) run the Vulkan path on Windows with the GPU
driver's Vulkan. That is how a renderer change is checked on both backends before it reaches the Mac.

## Claude Code on the Mac

Open the cloned folder in Claude Code. `CLAUDE.md` / `AGENTS.md` carry every project rule and
`docs/Roadmap.md` the project's state, so nothing needs copying from the Windows machine.
