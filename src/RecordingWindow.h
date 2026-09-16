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
		std::int64_t finalizedElapsedMs = -1;

		/// Start a fresh capture with a positive, prevalidated maximum duration.
		void Start(std::int64_t maximum) { *this = { maximum, -1, 0, true }; }
		/// Return zero before start, live elapsed time, or the first stop's frozen time.
		[[nodiscard]] std::int64_t ElapsedMs(std::int64_t elapsed) const
		{
			return started ? (finalizedElapsedMs < 0 ? elapsed : finalizedElapsedMs) : 0;
		}
		/// Test the capture deadline only while the window has not ended.
		[[nodiscard]] bool DurationLimitReached(std::int64_t elapsed) const
		{
			return started && endedMs < 0 && elapsed >= maximumMs;
		}
		/// Return captured duration, bounded by the configured maximum and first end.
		[[nodiscard]] std::int64_t RecordedMs(std::int64_t elapsed) const
		{
			return started ? std::clamp(endedMs < 0 ? elapsed : endedMs, std::int64_t{ 0 }, maximumMs) : 0;
		}
		/// Return the unused duration budget, or zero outside an active capture.
		[[nodiscard]] std::int64_t RemainingMs(std::int64_t elapsed) const
		{
			return started && endedMs < 0 ? maximumMs - RecordedMs(elapsed) : 0;
		}
		/// Freeze captured duration once; later finalization cannot extend it.
		void End(std::int64_t elapsed)
		{
			if (started && endedMs < 0)
				endedMs = RecordedMs(elapsed);
		}
		/// Freeze the first stop request so persistence retries retain its timing.
		void Finalize(std::int64_t elapsed)
		{
			End(elapsed);
			if (started && finalizedElapsedMs < 0)
				finalizedElapsedMs = std::max(elapsed, std::int64_t{ 0 });
		}
		/// Retain the latest in-window sample timestamp while capture remains active.
		void Sample(std::int64_t elapsed)
		{
			if (started && endedMs < 0 && elapsed >= 0 && elapsed <= maximumMs)
				lastSampleMs = std::max(lastSampleMs, elapsed);
		}
	};
}
