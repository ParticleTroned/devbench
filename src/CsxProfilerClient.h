#pragma once

#include "CSX/CSprofilerapi.h"
#include "ToolRegistry.h"
#include "CsxCommandJournal.h"
#include <functional>

namespace dvb
{
	/** Main-thread-only adapter; owns command receipts, never a sampling loop. */
	class CsxProfilerClient
	{
	public:
		json Dispatch(const CSX::ProfilerAPI::Interface003& service, const json& args, const std::string& buildId, const std::function<void()>& admit = {});

	private:
		CsxCommandJournal journal;
		std::string         owner, ownerBuild;
		std::uint64_t       ownedCapture = 0;
	};
}
