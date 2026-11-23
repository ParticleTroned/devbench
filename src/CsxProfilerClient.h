#pragma once

#include "CSX/CSprofilerapi.h"
#include "ToolRegistry.h"
#include <deque>

namespace dvb
{
	/** Main-thread-only adapter; owns command receipts, never a sampling loop. */
	class CsxProfilerClient
	{
	public:
		json Dispatch(const CSX::ProfilerAPI::Interface003& service, const json& args, const std::string& buildId);

	private:
		struct Receipt
		{
			std::string build, client, command;
			json        request, result;
		};
		std::deque<Receipt> receipts;
		std::string         owner, ownerBuild;
		std::uint64_t       ownedCapture = 0;
	};
}
