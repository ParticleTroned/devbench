#pragma once

namespace dvb
{
#ifdef DEVBENCH_PERFORMANCE_PROFILE
	inline constexpr bool        PerformanceProfile = true;
	inline constexpr const char* BuildProfileName = "performance";
#else
	inline constexpr bool        PerformanceProfile = false;
	inline constexpr const char* BuildProfileName = "full";
#endif
}
