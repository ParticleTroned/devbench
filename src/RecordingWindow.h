#pragma once

#include <algorithm>
#include <cstdint>

namespace dvb::Recording
{
	inline constexpr std::int64_t kMaximumRecordingDurationMs = 4 * 60 * 60 * 1000;

	/// Capture duration is independent of replay limits and later finalization.
	struct RecordingWindow
	{
		std::int64_t maximumMs = kMaximumRecordingDurationMs;
		std::int64_t endedMs = -1;
		std::int64_t lastSampleMs = 0;
		bool         started = false;

		void                       Start(std::int64_t maximum) { *this = { maximum, -1, 0, true }; }
		[[nodiscard]] std::int64_t RecordedMs(std::int64_t elapsed) const
		{
			return started ? std::clamp(endedMs < 0 ? elapsed : endedMs, std::int64_t{ 0 }, maximumMs) : 0;
		}
		[[nodiscard]] std::int64_t RemainingMs(std::int64_t elapsed) const
		{
			return started && endedMs < 0 ? maximumMs - RecordedMs(elapsed) : 0;
		}
		void End(std::int64_t elapsed)
		{
			if (started && endedMs < 0)
				endedMs = RecordedMs(elapsed);
		}
		void Sample(std::int64_t elapsed)
		{
			if (started && endedMs < 0 && elapsed >= 0 && elapsed <= maximumMs)
				lastSampleMs = std::max(lastSampleMs, elapsed);
		}
	};
}
