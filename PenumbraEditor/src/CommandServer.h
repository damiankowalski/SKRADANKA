#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace Penumbra {

	// Line-based JSON command server on 127.0.0.1 (loopback only). Each line received is a
	// request; the reply is one JSON line. Requests are executed on the main thread through
	// ProcessPending(), so commands never race with the editor's own frame.
	class CommandServer
	{
	public:
		using Handler = std::function<nlohmann::json(const nlohmann::json&)>;

		CommandServer() = default;
		~CommandServer();

		CommandServer(const CommandServer&) = delete;
		CommandServer& operator=(const CommandServer&) = delete;

		bool Start(uint16_t port);
		void Stop();
		bool IsRunning() const { return m_Running; }
		uint16_t GetPort() const { return m_Port; }

		// Runs queued requests on the calling (main) thread.
		void ProcessPending(const Handler& handler);

	private:
		struct Request
		{
			nlohmann::json Payload;
			nlohmann::json Response;
			bool Done = false;
		};

		void AcceptLoop();
		void ServeClient(uintptr_t socket);

	private:
		std::atomic<bool> m_Running = false;
		uint16_t m_Port = 0;
		uintptr_t m_ListenSocket = ~uintptr_t(0);
		std::thread m_Thread;

		std::mutex m_Mutex;
		std::condition_variable m_Completed;
		std::deque<Request*> m_Queue;
	};

}
