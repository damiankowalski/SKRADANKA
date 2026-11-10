#include "pnpch.h"
#include "Penumbra/Core/CrashHandler.h"

#include <Windows.h>
#include <DbgHelp.h>

namespace Penumbra {

	namespace {

		std::filesystem::path s_DumpDirectory;

		// The logger takes a mutex that the crashing thread may already hold, so crash output
		// goes straight to stderr.
		void CrashLog(const std::string& text)
		{
			std::fputs(("[crash] " + text + '\n').c_str(), stderr);
			std::fflush(stderr);
		}

		std::string s_ApplicationName = "Penumbra";

		void LogStack(CONTEXT* context)
		{
			HANDLE process = GetCurrentProcess();
			HANDLE thread = GetCurrentThread();
			SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
			SymInitialize(process, nullptr, TRUE);

			STACKFRAME64 frame{};
			frame.AddrPC.Offset = context->Rip;
			frame.AddrPC.Mode = AddrModeFlat;
			frame.AddrFrame.Offset = context->Rbp;
			frame.AddrFrame.Mode = AddrModeFlat;
			frame.AddrStack.Offset = context->Rsp;
			frame.AddrStack.Mode = AddrModeFlat;

			alignas(SYMBOL_INFO) char symbolBuffer[sizeof(SYMBOL_INFO) + 256]{};
			auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
			symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
			symbol->MaxNameLen = 255;

			for (int depth = 0; depth < 48; depth++)
			{
				if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
					frame.AddrPC.Offset == 0)
					break;
				DWORD64 displacement = 0;
				IMAGEHLP_LINE64 line{};
				line.SizeOfStruct = sizeof(line);
				DWORD lineDisplacement = 0;
				const bool hasSymbol = SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol) != FALSE;
				const bool hasLine = SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineDisplacement, &line) != FALSE;
				CrashLog(std::format("  #{:02} {} {}", depth, hasSymbol ? symbol->Name : "<unknown>",
					hasLine ? std::format("({}:{})", line.FileName, line.LineNumber) : std::format("[0x{:X}]", frame.AddrPC.Offset)));
			}
			SymCleanup(process);
		}

		LONG WINAPI UnhandledExceptionFilterFunction(EXCEPTION_POINTERS* exception)
		{
			const DWORD code = exception->ExceptionRecord->ExceptionCode;
			CrashLog(std::format("Unhandled exception 0x{:08X} at 0x{:X}", code,
				reinterpret_cast<uintptr_t>(exception->ExceptionRecord->ExceptionAddress)));

			// Walk a copy: StackWalk64 mutates the context.
			CONTEXT context = *exception->ContextRecord;
			LogStack(&context);

			std::error_code error;
			std::filesystem::create_directories(s_DumpDirectory, error);
			const std::filesystem::path dumpPath = s_DumpDirectory / std::format("{}_{}.dmp", s_ApplicationName, GetTickCount64());
			HANDLE file = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file != INVALID_HANDLE_VALUE)
			{
				MINIDUMP_EXCEPTION_INFORMATION info{ GetCurrentThreadId(), exception, FALSE };
				MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpWithIndirectlyReferencedMemory, &info, nullptr, nullptr);
				CloseHandle(file);
				CrashLog(std::format("Minidump written to {}", dumpPath.string()));
			}
			return EXCEPTION_EXECUTE_HANDLER;
		}

	}

	void CrashHandler::Install(const std::filesystem::path& dumpDirectory, const std::string& applicationName)
	{
		s_DumpDirectory = dumpDirectory;
		s_ApplicationName = applicationName;
		SetUnhandledExceptionFilter(UnhandledExceptionFilterFunction);
	}

}
