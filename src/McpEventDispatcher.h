#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <httplib.h>
#include <mutex>
#include <string>

namespace dvb
{
	/** Bounded FIFO for MCP notifications and legacy SSE responses, including sends before subscription. */
	class McpEventDispatcher
	{
	public:
		~McpEventDispatcher() { close(); }

		bool wait_event(httplib::DataSink* sink, const std::chrono::milliseconds& timeout = std::chrono::milliseconds(10000))
		{
			if (!sink)
				return false;
			std::string message;
			{
				std::unique_lock lock(mutex);
				if (!changed.wait_for(lock, timeout, [&] { return closed || !pending.empty(); }) || closed)
					return false;
				message = std::move(pending.front());
				pending.pop_front();
				pendingBytes -= message.size();
			}
			try {
				if (sink->write(message.data(), message.size()))
					return true;
			} catch (...) {
				// A failed sink invalidates this session; callers must establish a new one.
			}
			close();
			return false;
		}

		bool send_event(const std::string& message)
		{
			if (message.empty())
				return false;
			try {
				std::scoped_lock lock(mutex);
				if (closed)
					return false;
				if (pending.size() < maximumMessages && message.size() <= maximumBytes - pendingBytes) {
					pending.push_back(message);
					pendingBytes += message.size();
					changed.notify_one();
					return true;
				}
			} catch (...) {
				// Capacity and allocation failures both require a fresh catalog handshake.
			}
			close();
			return false;
		}

		void close()
		{
			{
				std::scoped_lock lock(mutex);
				closed = true;
				pending.clear();
				pendingBytes = 0;
			}
			changed.notify_all();
		}
		bool is_closed() const
		{
			std::scoped_lock lock(mutex);
			return closed;
		}
		std::chrono::steady_clock::time_point last_activity() const
		{
			std::scoped_lock lock(mutex);
			return lastActivity;
		}
		void update_activity()
		{
			std::scoped_lock lock(mutex);
			lastActivity = std::chrono::steady_clock::now();
		}

	private:
		static constexpr size_t               maximumMessages = 512, maximumBytes = 8 * 1024 * 1024;
		mutable std::mutex                    mutex;
		std::condition_variable               changed;
		std::deque<std::string>               pending;
		size_t                                pendingBytes = 0;
		bool                                  closed = false;
		std::chrono::steady_clock::time_point lastActivity = std::chrono::steady_clock::now();
	};
}
