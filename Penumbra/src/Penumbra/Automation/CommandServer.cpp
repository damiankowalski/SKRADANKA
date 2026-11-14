#include "pnpch.h"
#include "Penumbra/Automation/CommandServer.h"

#ifdef _WIN32
	#include <WinSock2.h>
	#include <WS2tcpip.h>
#else
	#include <arpa/inet.h>
	#include <cerrno>
	#include <fcntl.h>
	#include <netinet/in.h>
	#include <netinet/tcp.h>
	#include <sys/select.h>
	#include <sys/socket.h>
	#include <unistd.h>
#endif

namespace Penumbra {

	namespace {
		constexpr size_t MaxRequestBytes = 16 * 1024 * 1024;

#ifdef _WIN32
		bool NetworkStartup()
		{
			WSADATA data{};
			return WSAStartup(MAKEWORD(2, 2), &data) == 0;
		}
		void NetworkCleanup() { WSACleanup(); }
		void SetNonBlocking(SOCKET socket, bool enabled)
		{
			u_long value = enabled ? 1 : 0;
			ioctlsocket(socket, FIONBIO, &value);
		}
		bool ConnectInProgress() { return WSAGetLastError() == WSAEWOULDBLOCK; }
#else
		using SOCKET = int;
		constexpr SOCKET INVALID_SOCKET = -1;
		constexpr int SOCKET_ERROR = -1;
		constexpr int SD_BOTH = SHUT_RDWR;
		int closesocket(SOCKET socket) { return close(socket); }
		bool NetworkStartup() { return true; }
		void NetworkCleanup() {}
		void SetNonBlocking(SOCKET socket, bool enabled)
		{
			const int flags = fcntl(socket, F_GETFL, 0);
			fcntl(socket, F_SETFL, enabled ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
		}
		bool ConnectInProgress() { return errno == EINPROGRESS; }
#endif

		// A peer that disconnects must not kill the process with SIGPIPE.
		void DisableSigPipe([[maybe_unused]] SOCKET socket)
		{
#ifdef SO_NOSIGPIPE
			int enabled = 1;
			setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
		}

		bool SendAll(SOCKET socket, const std::string& data)
		{
#ifdef MSG_NOSIGNAL
			constexpr int flags = MSG_NOSIGNAL;
#else
			constexpr int flags = 0;
#endif
			size_t sent = 0;
			while (sent < data.size())
			{
				const auto result = send(socket, data.data() + sent, static_cast<int>(std::min<size_t>(data.size() - sent, 1 << 20)), flags);
				if (result == SOCKET_ERROR)
					return false;
				sent += static_cast<size_t>(result);
			}
			return true;
		}

		int Receive(SOCKET socket, char* buffer, size_t size)
		{
			return static_cast<int>(recv(socket, buffer, static_cast<int>(size), 0));
		}
	}

	CommandServer::~CommandServer() { Stop(); }

	bool CommandServer::Start(uint16_t port)
	{
		if (!NetworkStartup())
		{
			PN_CORE_ERROR("Command server: cannot initialize networking");
			return false;
		}

		SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (listener == INVALID_SOCKET)
		{
			NetworkCleanup();
			return false;
		}
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_port = htons(port);
		inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
#ifdef _WIN32
		// Windows lets a second listener steal the port unless it is exclusive; POSIX refuses by default.
		BOOL exclusive = TRUE;
		setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
#endif
		if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR || listen(listener, 4) == SOCKET_ERROR)
		{
			PN_CORE_ERROR("Command server: cannot listen on 127.0.0.1:{} (port in use?)", port);
			closesocket(listener);
			NetworkCleanup();
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
		// The accept loop notices m_Running within its select() timeout; the listener is closed after it ends.
		// Wake a client thread blocked in recv() on an idle connection.
		if (const uintptr_t client = m_ClientSocket.exchange(~uintptr_t(0)); client != ~uintptr_t(0))
			shutdown(static_cast<SOCKET>(client), SD_BOTH);
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
		closesocket(static_cast<SOCKET>(m_ListenSocket));
		m_ListenSocket = ~uintptr_t(0);
		NetworkCleanup();
	}

	void CommandServer::AcceptLoop()
	{
		const SOCKET listener = static_cast<SOCKET>(m_ListenSocket);
		while (m_Running)
		{
			// Wait with a timeout instead of blocking in accept(): closing the listener from Stop() does
			// not wake a blocked accept() on every platform.
			fd_set readable;
			FD_ZERO(&readable);
			FD_SET(listener, &readable);
			timeval wait{ 0, 100 * 1000 };
			if (select(static_cast<int>(listener) + 1, &readable, nullptr, nullptr, &wait) <= 0)
				continue;
			const SOCKET client = accept(listener, nullptr, nullptr);
			if (client == INVALID_SOCKET)
				continue; // closed by Stop() or transient error
			DisableSigPipe(client);
			// One client at a time keeps command ordering obvious for agents.
			m_ClientSocket = static_cast<uintptr_t>(client);
			if (m_Running)
				ServeClient(static_cast<uintptr_t>(client));
			m_ClientSocket = ~uintptr_t(0);
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
			const int received = Receive(client, chunk, sizeof(chunk));
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

	// ------------------------------------------------------------------------------------------
	// Client
	// ------------------------------------------------------------------------------------------

	CommandClient::CommandClient(std::string host, uint16_t port)
		: m_Host(std::move(host)), m_Port(port)
	{
		m_WinsockReady = NetworkStartup();
	}

	CommandClient::~CommandClient()
	{
		if (m_WinsockReady)
			NetworkCleanup();
	}

	uintptr_t CommandClient::Connect(std::chrono::milliseconds timeout, std::string& error)
	{
		const uintptr_t invalid = ~uintptr_t(0);
		if (!m_WinsockReady)
		{
			error = "cannot initialize networking";
			return invalid;
		}
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_port = htons(m_Port);
		if (inet_pton(AF_INET, m_Host.c_str(), &address.sin_addr) != 1)
		{
			error = std::format("invalid IPv4 address '{}'", m_Host);
			return invalid;
		}
		SOCKET socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (socketHandle == INVALID_SOCKET)
		{
			error = "cannot create a socket";
			return invalid;
		}

		// Non-blocking connect with a timeout: a refused loopback connection otherwise takes
		// seconds on Windows because of SYN retransmissions.
		SetNonBlocking(socketHandle, true);
		bool connected = connect(socketHandle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
		if (!connected && ConnectInProgress())
		{
			fd_set writable, failed;
			FD_ZERO(&writable);
			FD_ZERO(&failed);
			FD_SET(socketHandle, &writable);
			FD_SET(socketHandle, &failed);
			timeval wait{};
			wait.tv_sec = static_cast<long>(timeout.count() / 1000);
			wait.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);
			connected = select(static_cast<int>(socketHandle) + 1, nullptr, &writable, &failed, &wait) > 0 && FD_ISSET(socketHandle, &writable);
#ifndef _WIN32
			// POSIX reports a refused connection as writable: the socket error tells which it was.
			int socketError = 0;
			socklen_t length = sizeof(socketError);
			if (connected && (getsockopt(socketHandle, SOL_SOCKET, SO_ERROR, &socketError, &length) != 0 || socketError != 0))
				connected = false;
#endif
		}
		if (!connected)
		{
			closesocket(socketHandle);
			error = std::format("no command server at {}:{} (is the editor running?)", m_Host, m_Port);
			return invalid;
		}
		SetNonBlocking(socketHandle, false);
		DisableSigPipe(socketHandle);
		int noDelay = 1;
		setsockopt(socketHandle, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
		return static_cast<uintptr_t>(socketHandle);
	}

	bool CommandClient::IsReachable(std::chrono::milliseconds timeout)
	{
		std::string error;
		const uintptr_t socketHandle = Connect(timeout, error);
		if (socketHandle == ~uintptr_t(0))
			return false;
		closesocket(static_cast<SOCKET>(socketHandle));
		return true;
	}

	std::optional<nlohmann::json> CommandClient::Send(const nlohmann::json& request, std::string& error, std::chrono::milliseconds connectTimeout)
	{
		const uintptr_t handle = Connect(connectTimeout, error);
		if (handle == ~uintptr_t(0))
			return std::nullopt;
		const SOCKET socketHandle = static_cast<SOCKET>(handle);

		std::optional<nlohmann::json> response;
		if (!SendAll(socketHandle, request.dump() + "\n"))
		{
			error = std::format("cannot send to {}:{}", m_Host, m_Port);
		}
		else
		{
			std::string buffer;
			char chunk[8192];
			size_t newline = std::string::npos;
			while (newline == std::string::npos)
			{
				const int received = Receive(socketHandle, chunk, sizeof(chunk));
				if (received <= 0)
					break;
				const size_t searchFrom = buffer.size();
				buffer.append(chunk, static_cast<size_t>(received));
				newline = buffer.find('\n', searchFrom);
			}
			if (newline == std::string::npos)
			{
				error = std::format("the connection to {}:{} closed before a response arrived", m_Host, m_Port);
			}
			else
			{
				nlohmann::json parsed = nlohmann::json::parse(buffer.substr(0, newline), nullptr, false);
				if (parsed.is_discarded())
					error = "the command server sent invalid JSON";
				else
					response = std::move(parsed);
			}
		}
		shutdown(socketHandle, SD_BOTH);
		closesocket(socketHandle);
		return response;
	}

}
