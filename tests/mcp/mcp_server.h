#pragma once

#include <mcp_tool.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

namespace mcp
{
	// Capture the adapter's wire messages while retaining cpp-mcp's real request serialization.
	class server
	{
	public:
		using tool_handler = std::function<json(const json&, const std::string&)>;

		void register_tool(const tool& value, tool_handler)
		{
			std::scoped_lock lock(mutex);
			tools[value.name] = value.to_json();
		}

		void broadcast_notification(const request& value)
		{
			{
				std::scoped_lock lock(mutex);
				notifications.push_back(value.to_json());
			}
			changed.notify_all();
		}

		std::vector<json> WaitForNotifications(std::size_t count)
		{
			std::unique_lock lock(mutex);
			changed.wait_for(lock, std::chrono::seconds(2), [&] { return notifications.size() >= count; });
			return notifications;
		}

		json Describe(const std::string& name)
		{
			std::scoped_lock lock(mutex);
			return tools.at(name);
		}

	private:
		std::mutex                  mutex;
		std::condition_variable     changed;
		std::map<std::string, json> tools;
		std::vector<json>           notifications;
	};
}
