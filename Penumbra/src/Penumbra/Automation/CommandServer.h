#pragma once

#include "Penumbra/Core/Base.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
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
		std::atomic<uintptr_t> m_ClientSocket = ~uintptr_t(0);
		std::thread m_Thread;

		std::mutex m_Mutex;
		std::condition_variable m_Completed;
		std::deque<Request*> m_Queue;
	};

	// Client side of the command server protocol: one connection per request, so a client never
	// holds the editor's single connection slot while idle and survives editor restarts.
	class CommandClient
	{
	public:
		CommandClient(std::string host, uint16_t port);
		~CommandClient();

		CommandClient(const CommandClient&) = delete;
		CommandClient& operator=(const CommandClient&) = delete;

		const std::string& GetHost() const { return m_Host; }
		uint16_t GetPort() const { return m_Port; }

		// True if a server accepts connections within the timeout.
		bool IsReachable(std::chrono::milliseconds timeout = std::chrono::milliseconds(500));

		// Sends one request and waits for its response line. Returns nullopt with a message if the
		// server cannot be reached or the connection breaks.
		std::optional<nlohmann::json> Send(const nlohmann::json& request, std::string& error,
			std::chrono::milliseconds connectTimeout = std::chrono::milliseconds(1000));

	private:
		uintptr_t Connect(std::chrono::milliseconds timeout, std::string& error);

	private:
		std::string m_Host;
		uint16_t m_Port;
		bool m_WinsockReady = false;
	};

}
