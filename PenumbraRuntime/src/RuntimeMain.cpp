#include "RuntimeLayer.h"

#include "Penumbra/Core/Log.h"

#include "Penumbra/Core/Application.h"
#include "Penumbra/Core/CrashHandler.h"
#include "Penumbra/Core/FileSystem.h"
#include "Penumbra/Project/Project.h"

#ifdef _WIN32
	#include <Windows.h>
#endif

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
			"  --seed <n>               random seed for scripts and AI (default: fixed when headless or\n"
			"                           with --fixed-timestep, otherwise new for every scene load)\n"
			"  --set <k=v,k=v>          initial game values for scripts (game.get), e.g. difficulty=Hard\n"
			"  --exit-on-test           exit with 0/1 when the scene's scripts call test.complete()\n"
			"  --test-timeout <s>       fail the test run after s seconds of simulated time\n"
			"  --screenshot <file.png>  save the last frame before exiting\n"
			"  --playtest-out <file>    write scripts' playtest.record lines there (JSONL; docs/Playtest.md)\n"
			"  --width <px> --height <px>  resolution\n"
			"  --render-scale <0.25-1>  the 3D picture's resolution as a part of the window's pixels (default:\n"
			"                           the user's setting; automatic = one 3D pixel per window unit)\n"
			"  --window-screenshot <file.png>  with --frames: save the window's last frame, UI included\n"
			"  --no-vsync               present without waiting for vertical blank\n"
			"  --debug-layer            enable the D3D12/NVRHI validation layers\n"
			"  --dev                    testing tools in a shipping build (F3 performance, F4 debug panel,\n"
			"                           F5 freelook, F6 pause); development builds always have them\n"
			"  --debug-ui <list>        open testing tools at start: performance,panel,freelook; notarget:\n"
			"                           the guards ignore the thief\n"
			"  --perf-out <file>        frame statistics as JSON lines, twice a second (Tools/Benchmark.py);\n"
			"                           needs a window and the testing tools\n");
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
		const std::filesystem::path projectDirectory =
			args.GetValue("project") ? Project::Resolve(*args.GetValue("project")) : FileSystem::GetExecutableDirectory();
		std::string error;
		if (!Project::Load(projectDirectory, error))
		{
			PN_CORE_CRITICAL("{}", error);
#ifdef _WIN32
			if (!args.HasFlag("headless"))
				MessageBoxA(nullptr, error.c_str(), "Penumbra", MB_ICONERROR | MB_OK);
#endif
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
		if (const std::optional<std::string> playtestOut = args.GetValue("playtest-out"))
			options.PlaytestOut = *playtestOut;
		if (const std::optional<std::string> perfOut = args.GetValue("perf-out"))
			options.PerfOut = *perfOut;
		options.Width = spec.Width;
		options.Height = spec.Height;
		if (const std::optional<int> seed = args.GetInt("seed"))
			options.Seed = static_cast<uint64_t>(static_cast<uint32_t>(*seed));
		options.RandomizeSeed = !spec.Headless && spec.FixedTimestep <= 0.0f;
#ifdef PN_DIST
		options.DebugTools = args.HasFlag("dev");
#endif
		options.DebugToolsOpen = args.GetValue("debug-ui").value_or("");
		if (args.GetValue("render-scale"))
			options.RenderScale = std::clamp(ParseFloat(args, "render-scale", 1.0f), UserSettings::MinRenderScale, UserSettings::MaxRenderScale);
		if (const std::optional<std::string> values = args.GetValue("set"))
		{
			if (!ParseGameValues(*values, options.GameValues, error))
			{
				PN_CORE_CRITICAL("--set: {}", error);
				Log::Shutdown();
				return 1;
			}
		}

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

#ifdef _WIN32
// Shipping builds use the Windows subsystem (no console window).
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	return Run(__argc, __argv);
}
#endif
