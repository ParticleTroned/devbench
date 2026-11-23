#pragma once

#include "ToolRegistry.h"
#include <deque>
#include <optional>

namespace dvb
{
	/** Main-thread-only, bounded mutation receipts shared by the CSX adapters. */
	class CsxCommandJournal
	{
	public:
		std::optional<json> Find(const std::string& build, const json& request) const
		{
			for (const auto& receipt : receipts) {
				if (receipt.build != build || receipt.request.at("clientId") != request.at("clientId") ||
					receipt.request.at("commandId") != request.at("commandId"))
					continue;
				if (receipt.request != request)
					throw ToolError(409, "commandId was already used with different arguments");
				return receipt.result;
			}
			return std::nullopt;
		}

		void Remember(const std::string& build, const json& request, const json& result)
		{
			if (receipts.size() == 64)
				receipts.pop_front();
			receipts.push_back({ build, request, result });
		}

		template <class Run>
		json Dispatch(const std::string& build, const json& request, bool mutation, Run&& run)
		{
			if (mutation)
				if (const auto receipt = Find(build, request))
					return *receipt;
			auto result = run();
			const bool retryableFailure = result.is_object() && result.contains("error") && result["error"].is_object() &&
				!result.value("ok", true) && result["error"].value("retryable", false);
			if (mutation && !retryableFailure)
				Remember(build, request, result);
			return result;
		}

	private:
		struct Receipt
		{
			std::string build;
			json request, result;
		};
		std::deque<Receipt> receipts;
	};
}
