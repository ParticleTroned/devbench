#include "test_framework.h"

#include "EventBus.h"
#include "McpAdapter.h"
#include "ToolRegistry.h"

#include <mcp_server.h>

namespace
{
	void CheckNotification(const mcp::json& message, const char* method)
	{
		CHECK(message.at("jsonrpc") == "2.0");
		CHECK(message.at("method") == method);
		CHECK(!message.contains("id"));
		CHECK(!message.contains("params") || message.at("params").is_object());
	}
}

TEST_CASE("MCP announces added and replaced schemas using the standard notification")
{
	dvb::ToolRegistry registry;
	dvb::EventBus     events;
	mcp::server       server;
	dvb::McpAdapter   adapter(registry, events, server);
	registry.Register({ "initial", "Initial tool" }, [](const auto&, const auto&) { return dvb::json::object(); });
	adapter.Wire();
	CHECK(server.WaitForNotifications(0).empty());

	dvb::ToolDescriptor descriptor{ "feature", "Feature settings" };
	registry.Register(descriptor, [](const auto&, const auto&) { return dvb::json::object(); });
	auto messages = server.WaitForNotifications(1);
	CHECK(messages.size() == 1);
	CheckNotification(messages.at(0), "notifications/tools/list_changed");
	CHECK(!messages.at(0).contains("params") || messages.at(0).at("params").empty());

	descriptor.inputSchema["properties"]["terrain"] = { { "type", "boolean" } };
	registry.Register(descriptor, [](const auto&, const auto&) { return dvb::json::object(); });
	messages = server.WaitForNotifications(2);
	CHECK(messages.size() == 2);
	CheckNotification(messages.at(1), "notifications/tools/list_changed");
	CHECK(server.Describe("feature").at("inputSchema") == descriptor.inputSchema);
}

TEST_CASE("MCP forwards event messages with one notification prefix")
{
	dvb::ToolRegistry registry;
	dvb::EventBus     events;
	mcp::server       server;
	dvb::McpAdapter   adapter(registry, events, server);
	adapter.Wire();
	events.Publish("scene.ready", { { "loaded", true } });
	const auto messages = server.WaitForNotifications(1);
	CHECK(messages.size() == 1);
	CheckNotification(messages.at(0), "notifications/message");
	CHECK(messages.at(0).at("params").at("level") == "info");
	CHECK(messages.at(0).at("params").at("logger") == "devbench");
	CHECK(messages.at(0).at("params").at("topic") == "scene.ready");
	CHECK(messages.at(0).at("params").at("seq") == 1);
	CHECK(messages.at(0).at("params").at("data").at("loaded") == true);
}
