#include "EventBus.h"
#include "McpAdapter.h"
#include "ToolRegistry.h"
#include "test_framework.h"
#include <chrono>
#include <mcp_server.h>
#include <thread>

namespace
{
	using mcp::json;

	int FreePort()
	{
		WSADATA data{};
		if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
			throw std::runtime_error("Winsock initialization failed");
		const auto  socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		int        size = sizeof(address);
		const bool bound = socket != INVALID_SOCKET && ::bind(socket, reinterpret_cast<sockaddr*>(&address), size) == 0 &&
		                   getsockname(socket, reinterpret_cast<sockaddr*>(&address), &size) == 0;
		if (socket != INVALID_SOCKET)
			closesocket(socket);
		WSACleanup();
		if (!bound)
			throw std::runtime_error("Could not reserve an isolated test port");
		const int port = ntohs(address.sin_port);
		return port;
	}

	struct Fixture
	{
		int                      port = FreePort();
		dvb::ToolRegistry        registry;
		dvb::EventBus            events;
		mcp::server              server{ mcp::server::configuration{ .host = "127.0.0.1", .port = port } };
		dvb::McpAdapter          adapter{ registry, events, server };
		httplib::Client          client{ "127.0.0.1", port };
		std::vector<std::string> sessions;
		int                      requestId = 0;

		Fixture()
		{
			server.set_capabilities({ { "tools", { { "listChanged", true } } } });
			adapter.Wire();
			client.set_connection_timeout(2);
			client.set_read_timeout(2);
			if (!server.start(false))
				throw std::runtime_error("Could not start isolated MCP server");
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
			while (!server.http()->is_running() && std::chrono::steady_clock::now() < deadline)
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			if (!server.http()->is_running())
				throw std::runtime_error("MCP server did not become ready");
		}
		~Fixture()
		{
			for (const auto& session : sessions)
				client.Delete("/mcp", Headers(session));
			server.stop();
		}
		static httplib::Headers Headers(const std::string& session)
		{
			return { { "Mcp-Session-Id", session }, { "Accept", "application/json, text/event-stream" } };
		}
		std::string Connect()
		{
			const json request{ { "jsonrpc", "2.0" }, { "id", ++requestId }, { "method", "initialize" },
				{ "params", { { "protocolVersion", "2025-03-26" }, { "capabilities", json::object() }, { "clientInfo", { { "name", "catalog-regression" }, { "version", "1" } } } } } };
			const auto response = client.Post("/mcp", request.dump(), "application/json");
			if (!response || response->status != 200)
				throw std::runtime_error("MCP initialization failed");
			const auto session = response->get_header_value("Mcp-Session-Id");
			if (session.empty())
				throw std::runtime_error("MCP initialization omitted its session");
			sessions.push_back(session);
			const json initialized{ { "jsonrpc", "2.0" }, { "method", "notifications/initialized" } };
			const auto ready = client.Post("/mcp", Headers(session), initialized.dump(), "application/json");
			if (!ready || ready->status != 202)
				throw std::runtime_error("MCP initialized notification failed");
			return session;
		}
		json Notifications(const std::string& session, size_t count = 1)
		{
			std::string bytes;
			json        messages = json::array();
			client.Get("/mcp", Headers(session), [&](const char* data, size_t size) {
				bytes.append(data, size);
				for (auto end = bytes.find("\r\n\r\n"); end != std::string::npos; end = bytes.find("\r\n\r\n")) {
					const auto event = bytes.substr(0, end);
					bytes.erase(0, end + 4);
					if (const auto begin = event.find("data: "); begin != std::string::npos)
						messages.push_back(json::parse(event.substr(begin + 6)));
				}
				return messages.size() < count;
			});
			return messages;
		}
		json List(const std::string& session)
		{
			const json request{ { "jsonrpc", "2.0" }, { "id", ++requestId }, { "method", "tools/list" } };
			const auto response = client.Post("/mcp", Headers(session), request.dump(), "application/json");
			if (!response || response->status != 200)
				throw std::runtime_error("MCP tools/list failed");
			return json::parse(response->body).at("result").at("tools");
		}
		void Register(const char* field)
		{
			dvb::ToolDescriptor tool{ "streaming", "Streaming controls" };
			tool.inputSchema = { { "type", "object" }, { "properties", { { field, { { "type", "boolean" } } } } } };
			registry.Register(std::move(tool), [](const auto&, const auto&) { return json::object(); });
		}
	};

	void CheckAnnouncement(const json& message)
	{
		CHECK(message.is_object());
		if (!message.is_object())
			return;
		CHECK(message.at("method") == "notifications/tools/list_changed");
		CHECK(!message.contains("id"));
	}
}

TEST_CASE("MCP announces the current catalog when a client connects after registration")
{
	Fixture fixture;
	fixture.Register("terrain");
	const auto session = fixture.Connect();
	const auto messages = fixture.Notifications(session);
	CHECK(messages.size() == 1);
	if (!messages.empty())
		CheckAnnouncement(messages.at(0));
	CHECK(fixture.List(session).at(0).at("inputSchema").at("properties").contains("terrain"));
}

TEST_CASE("MCP replaces schemas and refreshes newly initialized client sessions")
{
	Fixture fixture;
	fixture.Register("enabled");
	const auto first = fixture.Connect();
	fixture.Register("reserve");
	const auto messages = fixture.Notifications(first, 2);
	CHECK(messages.size() == 2);
	for (const auto& message : messages)
		CheckAnnouncement(message);
	CHECK(fixture.List(first).at(0).at("inputSchema").at("properties").contains("reserve"));
	const auto second = fixture.Connect();
	const auto reconnect = fixture.Notifications(second);
	CHECK(reconnect.size() == 1);
	if (!reconnect.empty())
		CheckAnnouncement(reconnect.at(0));
	CHECK(fixture.List(second) == fixture.List(first));
}

TEST_CASE("MCP queued events are delivered in order without another producer")
{
	mcp::event_dispatcher dispatcher;
	CHECK(dispatcher.send_event("first"));
	CHECK(dispatcher.send_event("second"));
	std::string       received;
	httplib::DataSink sink;
	sink.write = [&](const char* bytes, size_t count) { received.append(bytes, count); return true; };
	CHECK(dispatcher.wait_event(&sink, std::chrono::milliseconds(1)));
	CHECK(received == "first");
	CHECK(dispatcher.wait_event(&sink, std::chrono::milliseconds(1)));
	CHECK(received == "firstsecond");
	CHECK(!dispatcher.wait_event(&sink, std::chrono::milliseconds(1)));
}

TEST_CASE("MCP slow consumers close on bounded overflow instead of losing a catalog event")
{
	mcp::event_dispatcher dispatcher;
	bool                  accepted = true;
	for (unsigned i = 0; i < 10000 && accepted; ++i)
		accepted = dispatcher.send_event(std::string(16384, 'x'));
	CHECK(!accepted);
	CHECK(dispatcher.is_closed());
	CHECK(!dispatcher.send_event("late"));
}

TEST_CASE("MCP failed sinks and explicit close reject further delivery")
{
	mcp::event_dispatcher dispatcher;
	CHECK(dispatcher.send_event("catalog"));
	httplib::DataSink sink;
	sink.write = [](const char*, size_t) { return false; };
	CHECK(!dispatcher.wait_event(&sink, std::chrono::milliseconds(1)));
	CHECK(dispatcher.is_closed());
	CHECK(!dispatcher.send_event("late"));
	dispatcher.close();
	CHECK(!dispatcher.wait_event(&sink, std::chrono::milliseconds(1)));
}

TEST_CASE("MCP overflow expires the old HTTP session and allows a fresh catalog handshake")
{
	Fixture fixture;
	fixture.Register("enabled");
	const auto old = fixture.Connect();
	for (unsigned i = 0; i < 1024 && !fixture.server.get_active_sessions().empty(); ++i)
		fixture.Register("reserve");
	CHECK(fixture.server.get_active_sessions().empty());
	const json request{ { "jsonrpc", "2.0" }, { "id", 10000 }, { "method", "tools/list" } };
	const auto expired = fixture.client.Post("/mcp", Fixture::Headers(old), request.dump(), "application/json");
	CHECK(expired && expired->status == 404);
	const auto current = fixture.Connect();
	const auto messages = fixture.Notifications(current);
	CHECK(messages.size() == 1);
	if (!messages.empty())
		CheckAnnouncement(messages.at(0));
	CHECK(fixture.List(current).at(0).at("inputSchema").at("properties").contains("reserve"));
}
