#pragma once
#include "ToolRegistry.h"

namespace dvb
{
	/** Registers only the on-demand production-CSX tools and restricted game controls. */
	void RegisterPerformanceTools(ToolRegistry& registry, ToolDescriptor game, ToolHandler gameHandler,
		ToolDescriptor inspect, ToolHandler inspectHandler);
}
