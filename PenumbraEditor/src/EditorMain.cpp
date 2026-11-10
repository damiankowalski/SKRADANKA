#include "EditorLayer.h"

#include "Penumbra/Automation/CommandProcessor.h"
#include "Penumbra/Core/Application.h"
#include "Penumbra/Core/CrashHandler.h"
#include "Penumbra/Core/FileSystem.h"
#include "Penumbra/Project/Project.h"

#include <cstdio>

using namespace Penumbra;

namespace {

	// Headless automation: run a JSON / JSON Lines command file without a window, print the
	// responses and exit 0 only if every command succeeded.
	int RunHeadless(const CommandLine& args)
	{
		const std::optional<std::string> file = args.GetValue("exec");
		if (!file)
		{
			PN_CORE_CRITICAL("--headless requires --exec <commands.json>");
			return 2;
		}
		const std::optional<std::string> text = FileSystem::ReadTextFile(*file);
		if (!text)
		{
			PN_CORE_CRITICAL("Cannot read {}", *file);
			return 2;
		}
		HeadlessAutomationHost host;
		host.SetUseWarp(args.HasFlag("warp"));
		CommandProcessor processor(host);
		if (const std::optional<std::string> project = args.GetValue("project"))
		{
			const nlohmann::json opened = processor.Execute({ { "cmd", "project.open" }, { "path", *project } });
			if (!opened.value("ok", false))
			{
				std::puts(opened.dump().c_str());
				return 1;
			}
		}
		const nlohmann::json responses = processor.ExecuteBatch(*text, args.HasFlag("continue-on-error"));
		bool ok = true;
		for (const nlohmann::json& response : responses)
		{
			std::puts(response.dump().c_str());
			ok &= response.value("ok", false);
		}
		return ok ? 0 : 1;
	}

}

int main(int argc, char** argv)
{
	const CommandLine args(argc, argv);
	Log::Init("PenumbraEditor.log");
	CrashHandler::Install(FileSystem::GetExecutableDirectory() / "CrashDumps", "PenumbraEditor");

	int exitCode = 0;
	if (args.HasFlag("headless"))
	{
		exitCode = RunHeadless(args);
	}
	else
	{
		ApplicationSpecification spec;
		spec.Name = "Penumbra Editor";
		spec.Maximized = true;
		spec.UseWarp = args.HasFlag("warp");
		spec.EnableGraphicsDebugLayer = args.HasFlag("debug-layer");
		spec.MaxFrames = static_cast<uint64_t>(std::max(args.GetInt("frames").value_or(0), 0));
		spec.Arguments = args;

		EditorOptions options;
		options.ProjectPath = args.GetValue("project").value_or("");
		options.CommandPort = static_cast<uint16_t>(args.GetInt("port").value_or(7777));
		options.EnableCommandServer = !args.HasFlag("no-server");

		Application app(spec);
		app.PushLayer(new EditorLayer(options));
		app.Run();
		exitCode = app.GetExitCode();
	}
	Log::Shutdown();
	return exitCode;
}
