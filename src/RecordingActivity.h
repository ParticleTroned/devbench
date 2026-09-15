#pragma once

#include "Json.h"

#include <string>

namespace dvb::Recording
{
	// Host-independent contract and replay planner for the activity stream stored beside a
	// trajectory. The game-facing recorder creates the events; this module decides which of
	// them can be reproduced by the currently advertised input contract and interleaves those
	// transitions without changing the trajectory's recorded clock.
	json ActivityCaptureContract();
	json SummarizeActivity(const json& a_events);

	/// Reject invalid timestamps, waits, or an overlong combined replay clock.
	/// Missing legacy metadata is allowed; retained stream timestamps are still checked.
	void ValidateRecordingReplayDuration(const json& a_recording);

	/// Build an atomic tracked-set sequence, upgrading legacy controller events with held poses.
	/// Reject a generated duration, including the replay tail, beyond the replay limit.
	/// Return { step|null, report, inputOwner, durationMs }; disabled/empty input has no tail.
	json BuildVRTrackedSetReplay(const json& a_trackingSamples, const json& a_events,
		const std::string& a_inputOwner, bool a_replayInputs);

	// Returns { steps, report, inputOwner }. Keyboard button down/up transitions are interleaved
	// here. The synchronized VR tracked-set stream is assembled separately by
	// BuildVRTrackedSetReplay so both device families retain their own atomic timing contract.
	json InterleaveReplayableActivity(const json& a_steps, const json& a_events,
		const std::string& a_inputOwner, bool a_replayInputs);
}
