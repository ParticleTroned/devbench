#pragma once
#include "ToolRegistry.h"

namespace dvb
{
	/** Admit and dispatch a save load in one main-thread task. */
	void LoadPerformanceSave(const std::string& name);
	/** Registers only the on-demand production-CSX tools and restricted game controls. */
	void RegisterPerformanceTools(ToolRegistry& registry, ToolDescriptor game, ToolHandler gameHandler,
		ToolDescriptor inspect, ToolHandler inspectHandler);
}
