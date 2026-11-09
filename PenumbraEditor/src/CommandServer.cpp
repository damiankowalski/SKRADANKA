#include "CommandServer.h"

#include "Penumbra/Core/Log.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <format>

namespace Penumbra {

	namespace {
		constexpr size_t MaxRequestBytes = 16 * 1024 * 1024;

		bool SendAll(SOCKET socket, const std::string& data)
		{
			size_t sent = 0;
			while (sent < data.size())
			{
				const int result = send(socket, data.data() + sent, static_cast<int>(std::min<size_t>(data.size() - sent, 1 << 20)), 0);
				if (result == SOCKET_ERROR)
					return false;
				sent += static_cast<size_t>(result);
			}
			return true;
		}
	}

	CommandServer::~CommandServer() { Stop(); }

	bool CommandServer::Start(uint16_t port)
	{
		WSADATA data{};
		if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
		{
			PN_CORE_ERROR("Command server: WSAStartup failed");
			return false;
		}

		SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (listener == INVALID_SOCKET)
		{
			WSACleanup();
			return false;
		}
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_port = htons(port);
		inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
		BOOL exclusive = TRUE;
		setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
		if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR || listen(listener, 4) == SOCKET_ERROR)
		{
			PN_CORE_ERROR("Command server: cannot listen on 127.0.0.1:{} (port in use?)", port);
			closesocket(listener);
			WSACleanup();
			return false;
		}

		m_ListenSocket = static_cast<uintptr_t>(listener);
		m_Port = port;
		m_Running = true;
		m_Thread = std::thread([this] { AcceptLoop(); });
		PN_CORE_INFO("Command server listening on 127.0.0.1:{}", port);
		return true;
	}

	void CommandServer::Stop()
	{
		if (!m_Running.exchange(false))
			return;
		closesocket(static_cast<SOCKET>(m_ListenSocket));
		{
			// Unblock any client thread waiting for the main thread.
			std::scoped_lock lock(m_Mutex);
			for (Request* request : m_Queue)
			{
				request->Response = { { "ok", false }, { "error", "editor is shutting down" } };
				request->Done = true;
			}
			m_Queue.clear();
		}
		m_Completed.notify_all();
		if (m_Thread.joinable())
			m_Thread.join();
		WSACleanup();
	}

	void CommandServer::AcceptLoop()
	{
		while (m_Running)
		{
			const SOCKET client = accept(static_cast<SOCKET>(m_ListenSocket), nullptr, nullptr);
			if (client == INVALID_SOCKET)
				continue; // closed by Stop() or transient error
			// One client at a time keeps command ordering obvious for agents.
			ServeClient(static_cast<uintptr_t>(client));
			closesocket(client);
		}
	}

	void CommandServer::ServeClient(uintptr_t socketHandle)
	{
		const SOCKET client = static_cast<SOCKET>(socketHandle);
		std::string buffer;
		char chunk[8192];
		while (m_Running)
		{
			const int received = recv(client, chunk, sizeof(chunk), 0);
			if (received <= 0)
				return;
			buffer.append(chunk, static_cast<size_t>(received));
			if (buffer.size() > MaxRequestBytes)
			{
				SendAll(client, R"({"ok":false,"error":"request too large"})" "\n");
				return;
			}

			size_t newline;
			while ((newline = buffer.find('\n')) != std::string::npos)
			{
				std::string line = buffer.substr(0, newline);
				buffer.erase(0, newline + 1);
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				if (line.empty())
					continue;

				Request request;
				request.Payload = nlohmann::json::parse(line, nullptr, false);
				if (request.Payload.is_discarded())
				{
					request.Response = { { "ok", false }, { "error", "invalid JSON" } };
				}
				else
				{
					std::unique_lock lock(m_Mutex);
					m_Queue.push_back(&request);
					m_Completed.wait(lock, [&] { return request.Done || !m_Running; });
					if (!request.Done)
						return;
				}
				if (!SendAll(client, request.Response.dump() + "\n"))
					return;
			}
		}
	}

	void CommandServer::ProcessPending(const Handler& handler)
	{
		std::deque<Request*> pending;
		{
			std::scoped_lock lock(m_Mutex);
			pending.swap(m_Queue);
		}
		if (pending.empty())
			return;
		for (Request* request : pending)
		{
			nlohmann::json response = handler(request->Payload);
			std::scoped_lock lock(m_Mutex);
			request->Response = std::move(response);
			request->Done = true;
		}
		m_Completed.notify_all();
	}

}
