#include "RuntimeLayer.h"

#include "Penumbra/Core/Log.h"

#include "Penumbra/Core/Application.h"
#include "Penumbra/Core/CrashHandler.h"
#include "Penumbra/Core/FileSystem.h"
#include "Penumbra/Project/Project.h"

#include <Windows.h>

using namespace Penumbra;

namespace {

	void PrintUsage()
	{
		std::puts(
			"PenumbraRuntime - plays a Penumbra project\n"
			"\n"
			"  --project <dir>          project directory (default: next to the executable)\n"
			"  --scene <path>           scene to start (default: the project's start scene)\n"
			"  --headless               no window; render offscreen (or not at all with --no-render)\n"
			"  --no-render              skip rendering entirely (pure simulation)\n"
			"  --warp                   use the WARP software rasterizer\n"
			"  --frames <n>             quit after n frames\n"
			"  --fixed-timestep <s>     advance exactly s seconds per frame (deterministic)\n"
			"  --exit-on-test           exit with 0/1 when the scene's scripts call test.complete()\n"
			"  --test-timeout <s>       fail the test run after s seconds of simulated time\n"
			"  --screenshot <file.png>  save the last frame before exiting\n"
			"  --width <px> --height <px>  resolution\n"
			"  --no-vsync               present without waiting for vertical blank\n"
			"  --debug-layer            enable the D3D12/NVRHI validation layers\n");
	}

	float ParseFloat(const CommandLine& args, std::string_view name, float fallback)
	{
		const std::optional<std::string> value = args.GetValue(name);
		if (!value)
			return fallback;
		try
		{
			return std::stof(*value);
		}
		catch (const std::exception&)
		{
			PN_CORE_WARN("Ignoring invalid --{} value '{}'", name, *value);
			return fallback;
		}
	}

	int Run(int argc, char** argv)
	{
		const CommandLine args(argc, argv);
		if (args.HasFlag("help"))
		{
			PrintUsage();
			return 0;
		}

		Log::Init();
	CrashHandler::Install(FileSystem::GetExecutableDirectory() / "CrashDumps", "PenumbraRuntime");
		const std::filesystem::path projectDirectory = args.GetValue("project").value_or(FileSystem::GetExecutableDirectory().string());
		std::string error;
		if (!Project::Load(projectDirectory, error))
		{
			PN_CORE_CRITICAL("{}", error);
			if (!args.HasFlag("headless"))
				MessageBoxA(nullptr, error.c_str(), "Penumbra", MB_ICONERROR | MB_OK);
			return 1;
		}

		ApplicationSpecification spec;
		spec.Name = Project::GetConfig().Name;
		spec.Width = static_cast<uint32_t>(args.GetInt("width").value_or(static_cast<int>(Project::GetConfig().WindowWidth)));
		spec.Height = static_cast<uint32_t>(args.GetInt("height").value_or(static_cast<int>(Project::GetConfig().WindowHeight)));
		spec.Headless = args.HasFlag("headless");
		spec.UseWarp = args.HasFlag("warp");
		spec.VSync = !args.HasFlag("no-vsync");
		spec.EnableGraphicsDebugLayer = args.HasFlag("debug-layer");
		spec.MaxFrames = static_cast<uint64_t>(std::max(args.GetInt("frames").value_or(0), 0));
		spec.FixedTimestep = ParseFloat(args, "fixed-timestep", 0.0f);
		spec.Arguments = args;

		RuntimeOptions options;
		options.ScenePath = args.GetValue("scene").value_or("");
		options.Render = !args.HasFlag("no-render");
		options.ExitOnTestComplete = args.HasFlag("exit-on-test");
		options.TestTimeout = ParseFloat(args, "test-timeout", 0.0f);
		if (const std::optional<std::string> screenshot = args.GetValue("screenshot"))
			options.ScreenshotPath = *screenshot;
		options.Width = spec.Width;
		options.Height = spec.Height;

		int exitCode = 0;
		{
			Application app(spec);
			auto* layer = new RuntimeLayer(options);
			app.PushLayer(layer);
			app.Run();
			exitCode = app.GetExitCode();
			if (options.ExitOnTestComplete && exitCode == 0 && !layer->IsFinished())
			{
				PN_CORE_ERROR("Frame limit reached before the test scene completed");
				exitCode = 1;
			}
		}
		Log::Shutdown();
		return exitCode;
	}

}

int main(int argc, char** argv)
{
	return Run(argc, argv);
}

// Shipping builds use the Windows subsystem (no console window).
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	return Run(__argc, __argv);
}
