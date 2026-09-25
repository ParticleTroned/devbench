#include "test_framework.h"

#include "RecordingActivity.h"
#include "VRInputState.h"

#include <limits>

using dvb::json;
using dvb::Recording::ActivityCaptureContract;
using dvb::Recording::BuildVRTrackedSetReplay;
using dvb::Recording::CollapseConsoleTyping;
using dvb::Recording::InterleaveReplayableActivity;
using dvb::Recording::IsKeyEventFor;
using dvb::Recording::SummarizeActivity;
using dvb::Recording::ValidateRecordingReplayDuration;

TEST_CASE("replay duration accepts legacy metadata and the exact boundary")
{
	ValidateRecordingReplayDuration(json{ { "steps", json::array() } });
	ValidateRecordingReplayDuration(json{
		{ "meta", { { "recordedMs", dvb::kMaximumVRTrackedDurationMs },
					  { "elapsedMs", 9000000 }, { "unrecordedTailMs", 7200000 } } },
		{ "steps", json::array({ json{ { "atMs", dvb::kMaximumVRTrackedDurationMs } } }) },
	});
}

TEST_CASE("replay duration rejects malformed and overflowing metadata")
{
	for (const json& duration : json::array({ -1, 1800001, 1800000.5, "100", nullptr, true,
			 std::numeric_limits<std::uint64_t>::max() })) {
		CHECK_THROWS_AS(ValidateRecordingReplayDuration(json{ { "meta", { { "recordedMs", duration } } } }),
			std::invalid_argument);
	}
	CHECK_THROWS_AS(ValidateRecordingReplayDuration(json{ { "meta", nullptr } }), std::invalid_argument);
}

TEST_CASE("overlong observation streams cannot bypass replay by omitting duration")
{
	for (const char* stream : { "steps", "activityEvents", "trackingSamples" }) {
		const char* timestamp = std::string_view(stream) == "steps" ? "atMs" : "tMs";
		json        recording{ { stream, json::array({ json{ { timestamp, 1800001 } } }) } };
		CHECK_THROWS_AS(ValidateRecordingReplayDuration(recording), std::invalid_argument);
		recording["meta"] = { { "recordedMs", 1 } };
		CHECK_THROWS_AS(ValidateRecordingReplayDuration(recording), std::invalid_argument);
	}
	CHECK_THROWS_AS(ValidateRecordingReplayDuration(json{
						{ "meta", { { "checkpoints", json::array({ json{ { "atMs", 1800001 } } }) } } } }),
		std::invalid_argument);
}

TEST_CASE("replay rejects invalid and overlong waits before planning")
{
	for (const json& wait : json::array({ -1, 1800001, 0.5, "100", nullptr, true,
			 std::numeric_limits<std::int64_t>::max(), std::numeric_limits<std::uint64_t>::max() })) {
		const json recording{ { "steps", json::array({ json{ { "wait", wait } } }) } };
		CHECK_THROWS_AS(ValidateRecordingReplayDuration(recording), std::invalid_argument);
	}
}

TEST_CASE("replay combines cumulative waits with forward and backward offsets")
{
	const json timelines = json::array({
		json::array({ json{ { "wait", 900000 } }, json{ { "wait", 900001 } } }),
		json::array({ json{ { "atMs", 1500000 }, { "wait", 300001 } } }),
		json::array({ json{ { "wait", 1700000 } }, json{ { "atMs", 10 }, { "wait", 100001 } } }),
	});
	for (const auto& steps : timelines) {
		CHECK_THROWS_AS(ValidateRecordingReplayDuration(json{ { "steps", steps } }), std::invalid_argument);
		CHECK_THROWS_AS(ValidateRecordingReplayDuration(json{
							{ "meta", { { "recordedMs", 1 } } }, { "steps", steps } }),
			std::invalid_argument);
	}
}

TEST_CASE("replay clock accepts the exact limit and agrees with activity planning")
{
	json recording{ { "steps", json::array({
								   json{ { "wait", 0 } },
								   json{ { "atMs", 100 }, { "wait", 10 } },
								   json{ { "atMs", 50 }, { "wait", 20 } },
								   json{ { "wait", 1799870 } },
								   json{ { "atMs", 1800000 } },
							   }) } };
	ValidateRecordingReplayDuration(recording);
	const auto   plan = InterleaveReplayableActivity(recording["steps"], json::array(), "recording:clock", true);
	std::int64_t durationMs = 0;
	for (const auto& step : plan["steps"])
		durationMs += step.value("wait", std::int64_t{ 0 });
	CHECK(durationMs == dvb::kMaximumVRTrackedDurationMs);
	recording["steps"].push_back(json{ { "wait", 1 } });
	CHECK_THROWS_AS(ValidateRecordingReplayDuration(recording), std::invalid_argument);
}

namespace
{
	json TrackingSample(std::int64_t a_ms)
	{
		const auto pose = [](int index) {
			return json{ { "available", true }, { "connected", true }, { "valid", true },
				{ "index", index }, { "trackingResult", 200 },
				{ "matrix", json::array({ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 }) },
				{ "velocity", json::array({ 0, 0, 0 }) },
				{ "angularVelocity", json::array({ 0, 0, 0 }) } };
		};
		return json{ { "tMs", a_ms }, { "originCode", 1 }, { "hmd", pose(0) },
			{ "left", pose(1) }, { "right", pose(2) } };
	}

	json Button(std::int64_t a_ms, std::uint64_t a_seq, const char* a_device,
		const char* a_state, int a_id)
	{
		return json{ { "kind", "input" }, { "eventType", "button" },
			{ "device", a_device }, { "state", a_state }, { "idCode", a_id },
			{ "tMs", a_ms }, { "seq", a_seq } };
	}

	json UserButton(std::int64_t a_ms, std::uint64_t a_seq, const char* a_state, int a_id, const char* a_userEvent)
	{
		json event = Button(a_ms, a_seq, "keyboard", a_state, a_id);
		event["userEvent"] = a_userEvent;
		return event;
	}

	json Char(std::int64_t a_ms, std::uint64_t a_seq, int a_id)
	{
		return json{ { "kind", "input" }, { "eventType", "char" }, { "device", "keyboard" },
			{ "idCode", a_id }, { "tMs", a_ms }, { "seq", a_seq } };
	}

	json ConsoleMenu(std::int64_t a_ms, std::uint64_t a_seq, bool a_opening)
	{
		return json{ { "kind", "menu" }, { "name", "Console" }, { "opening", a_opening },
			{ "tMs", a_ms }, { "seq", a_seq } };
	}

	// Mirrors a real capture: toggle down precedes the menu open, typing follows, toggle down
	// precedes the menu close.
	json ConsoleSession(std::int64_t a_startMs)
	{
		return json::array({
			UserButton(a_startMs, 10, "down", 41, "Console"),
			UserButton(a_startMs + 5, 11, "up", 41, "Console"),
			ConsoleMenu(a_startMs + 10, 12, true),
			Button(a_startMs + 20, 13, "keyboard", "down", 20),
			Char(a_startMs + 21, 14, 116),
			Button(a_startMs + 30, 15, "keyboard", "up", 20),
			json{ { "kind", "console" }, { "command", "tgm" }, { "tMs", a_startMs + 40 }, { "seq", 16 } },
			UserButton(a_startMs + 50, 17, "down", 41, "Console"),
			ConsoleMenu(a_startMs + 60, 18, false),
		});
	}
}

TEST_CASE("activity contract declares capture-all and partial replay honestly")
{
	const json contract = ActivityCaptureContract();
	CHECK(contract["version"]["major"] == 1);
	CHECK(contract["inputLayer"] == "Skyrim.BSInputDeviceManager");
	CHECK(contract["replay"]["keyboardButtonTransitions"] == true);
	CHECK(contract["replay"]["vrTrackedInputFrames"] == true);
	CHECK(contract["replay"]["controllerButtonTransitions"] == true);
}

TEST_CASE("activity summary separates replayable keyboard transitions from preserved input")
{
	const json events = json::array({
		Button(10, 1, "keyboard", "down", 17),
		Button(20, 2, "keyboard", "held", 17),
		Button(30, 3, "oculusSecondary", "down", 33),
		json{ { "kind", "menu" }, { "name", "InventoryMenu" }, { "opening", true } },
	});
	const json summary = SummarizeActivity(events);
	CHECK(summary["total"] == 4);
	CHECK(summary["input"] == 3);
	CHECK(summary["menu"] == 1);
	CHECK(summary["keyboardTransitions"] == 1);
	CHECK(summary["vrControllerEvents"] == 1);
	CHECK(summary["unsupportedInput"] == 1);
}

TEST_CASE("legacy controller events become atomic tracked-set frames without losing short presses")
{
	const json samples = json::array({
		TrackingSample(20),
		TrackingSample(100),
	});
	json       down = Button(40, 1, "oculusPrimary", "down", 33);
	down["wandIndex"] = 2;
	json up = Button(60, 2, "oculusPrimary", "up", 33);
	up["wandIndex"] = 2;
	const json plan = BuildVRTrackedSetReplay(samples, json::array({ down, up }),
		"recording:test", true);

	CHECK(plan["step"]["tool"] == "input");
	CHECK(plan["step"]["args"]["device"] == "vrTrackedSet");
	CHECK(plan["step"]["args"]["surviveLifecycle"] == true);
	const auto& frames = plan["step"]["args"]["frames"];
	CHECK(frames.size() == 5);
	CHECK(frames[0]["tMs"] == 0);
	CHECK(frames[2]["tMs"] == 40);
	CHECK(frames[2]["right"]["controller"]["pressed"].get<std::uint64_t>() == (std::uint64_t{ 1 } << 33));
	CHECK(frames[3]["tMs"] == 60);
	CHECK(frames[3]["right"]["controller"]["pressed"] == 0);
	CHECK(plan["durationMs"] == 150);
	CHECK(plan["step"]["args"]["tailMs"] == 50);
	CHECK(plan["report"]["convertedControllerEvents"] == 2);
	CHECK(plan["report"]["roleFallbackEvents"] == 0);

	const json fallbackPlan = BuildVRTrackedSetReplay(samples,
		json::array({ Button(40, 3, "oculusPrimary", "down", 33) }), "recording:test", true);
	CHECK(fallbackPlan["report"]["roleFallbackEvents"] == 1);
	CHECK(fallbackPlan["step"]["args"]["frames"][2]["right"]["controller"]["pressed"].get<std::uint64_t>() ==
		  (std::uint64_t{ 1 } << 33));
}

TEST_CASE("VR tracked-set replay rejects malformed source samples before returning steps")
{
	json malformed = json::array({ json{ { "tMs", 20 }, { "hmd", json::object() } } });
	CHECK_THROWS(BuildVRTrackedSetReplay(malformed, json::array(), "recording:test", true));

	json duplicate = json::array({ json{ { "tMs", 20 } }, json{ { "tMs", 20 } } });
	CHECK_THROWS(BuildVRTrackedSetReplay(duplicate, json::array(), "recording:test", true));

	json overLimit = json::array({ json{ { "tMs", dvb::kMaximumVRTrackedDurationMs + 1 } } });
	CHECK_THROWS_AS(BuildVRTrackedSetReplay(overLimit, json::array(), "recording:test", true),
		std::invalid_argument);

	json atLimit = json::array({ json{ { "tMs", dvb::kMaximumVRTrackedDurationMs } } });
	json collision = Button(dvb::kMaximumVRTrackedDurationMs, 1, "oculusPrimary", "down", 33);
	CHECK_THROWS_AS(BuildVRTrackedSetReplay(atLimit, json::array({ collision }),
						"recording:test", true),
		std::invalid_argument);
}

TEST_CASE("VR replay duration includes the full tail at the boundary")
{
	const auto limitMs = dvb::kMaximumVRTrackedDurationMs;
	const auto plan = BuildVRTrackedSetReplay(json::array({ TrackingSample(limitMs - 50) }),
		json::array(), "recording:test", true);
	CHECK(plan["durationMs"] == limitMs);
	CHECK(plan["report"]["durationMs"] == limitMs);
	CHECK(plan["step"]["args"]["tailMs"] == 50);

	for (const auto tMs : { limitMs - 49, limitMs }) {
		CHECK_THROWS_AS(BuildVRTrackedSetReplay(json::array({ TrackingSample(tMs) }),
							json::array(), "recording:test", true),
			std::invalid_argument);
	}
}

TEST_CASE("VR controller frame insertion and timestamp shifts share the tail budget")
{
	const auto limitMs = dvb::kMaximumVRTrackedDurationMs;
	json       event = Button(limitMs - 50, 1, "oculusPrimary", "down", 33);
	event["wandIndex"] = 2;
	const auto inserted = BuildVRTrackedSetReplay(json::array({ TrackingSample(0) }),
		json::array({ event }), "recording:test", true);
	CHECK(inserted["durationMs"] == limitMs);
	CHECK(inserted["report"]["convertedControllerEvents"] == 1);
	event["tMs"] = limitMs - 49;
	CHECK_THROWS_AS(BuildVRTrackedSetReplay(json::array({ TrackingSample(0) }),
						json::array({ event }), "recording:test", true),
		std::invalid_argument);

	event["tMs"] = limitMs - 51;
	const auto shifted = BuildVRTrackedSetReplay(json::array({ TrackingSample(limitMs - 51) }),
		json::array({ event }), "recording:test", true);
	CHECK(shifted["durationMs"] == limitMs);
	CHECK(shifted["report"]["timestampAdjustedControllerEvents"] == 1);
	event["tMs"] = limitMs - 50;
	CHECK_THROWS_AS(BuildVRTrackedSetReplay(json::array({ TrackingSample(limitMs - 50) }),
						json::array({ event }), "recording:test", true),
		std::invalid_argument);
}

TEST_CASE("disabled or empty VR replay does not allocate a tail")
{
	const auto disabled = BuildVRTrackedSetReplay(
		json::array({ TrackingSample(dvb::kMaximumVRTrackedDurationMs) }),
		json::array(), "recording:test", false);
	CHECK(disabled["step"].is_null());
	CHECK(disabled["durationMs"] == 0);
	CHECK(disabled["inputOwner"] == "");
	const auto empty = BuildVRTrackedSetReplay(json::array(), json::array(), "recording:test", true);
	CHECK(empty["step"].is_null());
	CHECK(empty["durationMs"] == 0);
	CHECK(empty["inputOwner"] == "");
}

TEST_CASE("canonical VR replay enforces its final frame budget")
{
	json samples = json::array();
	for (std::int64_t i = 1; i <= dvb::kMaximumVRTrackedFrames; ++i)
		samples.push_back(json{ { "tMs", i } });
	CHECK_THROWS_AS(BuildVRTrackedSetReplay(samples, json::array(),
						"recording:test", true),
		std::invalid_argument);

	// With time zero already present, one controller transition would become frame 60,001.
	samples[0]["tMs"] = 0;
	json controller = Button(1, 1, "oculusPrimary", "down", 33);
	controller["wandIndex"] = 2;
	CHECK_THROWS_AS(BuildVRTrackedSetReplay(samples, json::array({ controller }),
						"recording:test", true),
		std::invalid_argument);
}

TEST_CASE("keyboard replay planning rejects wrong-typed ordering fields")
{
	const json badTime = json::array({
		json{ { "kind", "input" }, { "eventType", "button" }, { "device", "keyboard" },
			{ "state", "down" }, { "idCode", 30 }, { "tMs", "now" }, { "seq", 1 } },
	});
	CHECK_THROWS_AS(InterleaveReplayableActivity(json::array(), badTime, "recording:test", true),
		json::type_error);

	const json badSequence = json::array({
		json{ { "kind", "input" }, { "eventType", "button" }, { "device", "keyboard" },
			{ "state", "down" }, { "idCode", 30 }, { "tMs", 1 }, { "seq", "first" } },
		json{ { "kind", "input" }, { "eventType", "button" }, { "device", "keyboard" },
			{ "state", "up" }, { "idCode", 30 }, { "tMs", 1 }, { "seq", 2 } },
	});
	CHECK_THROWS_AS(InterleaveReplayableActivity(json::array(), badSequence, "recording:test", true),
		json::type_error);
}

TEST_CASE("same-millisecond controller transitions remain distinct atomic frames")
{
	const json samples = json::array({
		TrackingSample(10),
		TrackingSample(20),
	});
	json       down = Button(10, 1, "oculusPrimary", "down", 33);
	down["wandIndex"] = 2;
	json up = Button(10, 2, "oculusPrimary", "up", 33);
	up["wandIndex"] = 2;

	const json  plan = BuildVRTrackedSetReplay(samples, json::array({ down, up }),
		"recording:test", true);
	const auto& frames = plan["step"]["args"]["frames"];
	CHECK(frames[2]["tMs"] == 11);
	CHECK(frames[2]["right"]["controller"]["pressed"].get<std::uint64_t>() ==
		  (std::uint64_t{ 1 } << 33));
	CHECK(frames[3]["tMs"] == 12);
	CHECK(frames[3]["right"]["controller"]["pressed"] == 0);
	CHECK(plan["report"]["timestampAdjustedControllerEvents"] == 2);
}

TEST_CASE("trajectory atMs preserves an initial no-player recording delay")
{
	const json   steps = json::array({
		json{ { "atMs", 125 }, { "pose", json::array({ 1, 2, 3, 4, 5 }) }, { "wait", 25 } },
	});
	const json   plan = InterleaveReplayableActivity(steps, json::array(), "recording:test", true);
	std::int64_t totalWait = 0;
	for (const auto& step : plan["steps"])
		if (step.contains("wait"))
			totalWait += step["wait"].get<std::int64_t>();
	CHECK(totalWait == 150);
}

TEST_CASE("keyboard transitions interleave without changing the original trajectory clock")
{
	const json steps = json::array({
		json{ { "pose", json::array({ 1, 2, 3, 4, 5 }) }, { "wait", 100 } },
	});
	const json events = json::array({
		Button(25, 1, "keyboard", "down", 17),
		Button(50, 2, "keyboard", "held", 17),
		Button(60, 3, "oculusSecondary", "down", 33),
		Button(75, 4, "keyboard", "up", 17),
	});
	const json plan = InterleaveReplayableActivity(steps, events, "recording:test", true);
	CHECK(plan["report"]["replayedKeyboardTransitions"] == 2);
	CHECK(plan["report"]["skippedInput"] == 2);
	CHECK(plan["report"]["partial"] == true);

	std::int64_t totalWait = 0;
	json         transitions = json::array();
	for (const auto& step : plan["steps"]) {
		if (step.contains("wait"))
			totalWait += step["wait"].get<std::int64_t>();
		if (step.value("tool", std::string{}) == "input")
			transitions.push_back(step["args"]["action"]);
	}
	CHECK(totalWait == 100);
	CHECK(transitions == json::array({ "down", "up" }));
	CHECK(plan["inputOwner"] == "recording:test");
	const auto downStep = std::find_if(plan["steps"].begin(), plan["steps"].end(),
		[](const json& step) {
			return step.value("tool", std::string{}) == "input" &&
			       step["args"].value("action", std::string{}) == "down";
		});
	CHECK(downStep != plan["steps"].end());
	if (downStep != plan["steps"].end())
		CHECK((*downStep)["args"]["maxHoldMs"] == 2050);
}

TEST_CASE("keyboard replay rejects holds without a complete safety margin")
{
	const json atLimit = json::array({
		Button(0, 1, "keyboard", "down", 17),
		Button(58000, 2, "keyboard", "up", 17),
	});
	const json plan = InterleaveReplayableActivity(json::array(), atLimit,
		"recording:test", true);
	CHECK(plan["steps"][0]["args"]["maxHoldMs"] == 60000);

	const json overLimit = json::array({
		Button(0, 1, "keyboard", "down", 17),
		Button(58001, 2, "keyboard", "up", 17),
	});
	CHECK_THROWS_AS(InterleaveReplayableActivity(json::array(), overLimit,
						"recording:test", true),
		std::invalid_argument);
}

TEST_CASE("input replay can be disabled without altering legacy steps")
{
	const json steps = json::array({ json{ { "wait", 40 } } });
	const json events = json::array({ Button(10, 1, "keyboard", "down", 17) });
	const json plan = InterleaveReplayableActivity(steps, events, "recording:test", false);
	CHECK(plan["steps"] == steps);
	CHECK(plan["report"]["enabled"] == false);
	CHECK(plan["report"]["replayedKeyboardTransitions"] == 0);
	CHECK(plan["inputOwner"] == "");
}

TEST_CASE("key event matching only applies to keyboard button and char events")
{
	CHECK(IsKeyEventFor(Button(1, 1, "keyboard", "down", 65), { 65, 66 }));
	CHECK(IsKeyEventFor(Char(1, 1, 66), { 65, 66 }));
	CHECK(!IsKeyEventFor(Button(1, 1, "keyboard", "down", 17), { 65, 66 }));
	CHECK(!IsKeyEventFor(Button(1, 1, "oculusPrimary", "down", 65), { 65 }));
	CHECK(!IsKeyEventFor(Button(1, 1, "keyboard", "down", 65), {}));
	CHECK(!IsKeyEventFor(ConsoleMenu(1, 1, true), { 65 }));
}

TEST_CASE("console typing collapses to the captured console command")
{
	json events = json::array({ Button(1, 1, "keyboard", "down", 17) });
	for (const auto& event : ConsoleSession(100))
		events.push_back(event);
	events.push_back(Button(300, 20, "keyboard", "up", 17));

	const json               collapsed = CollapseConsoleTyping(events);
	std::vector<std::string> kinds;
	int                      keyboard = 0;
	for (const auto& event : collapsed) {
		kinds.push_back(event["kind"]);
		keyboard += event["kind"] == "input" ? 1 : 0;
	}
	CHECK(keyboard == 2);  // only the surrounding movement key survives
	CHECK(std::count(kinds.begin(), kinds.end(), "console") == 1);
	CHECK(collapsed.front()["idCode"] == 17);
	CHECK(collapsed.back()["idCode"] == 17);
}

TEST_CASE("unclosed console window drops typing through the end of the capture")
{
	const json events = json::array({
		ConsoleMenu(10, 1, true),
		Button(20, 2, "keyboard", "down", 34),
		Button(30, 3, "oculusPrimary", "down", 33),
	});
	const json collapsed = CollapseConsoleTyping(events);
	CHECK(collapsed.size() == 2);  // menu event + the non-keyboard input
	CHECK(collapsed.back()["device"] == "oculusPrimary");
}

TEST_CASE("collapse leaves recordings without a console session untouched")
{
	const json events = json::array({
		Button(10, 1, "keyboard", "down", 17),
		Button(20, 2, "keyboard", "up", 17),
	});
	CHECK(CollapseConsoleTyping(events) == events);
	CHECK(CollapseConsoleTyping(json::array()) == json::array());
}

TEST_CASE("replay never injects the record or replay hotkeys")
{
	const json steps = json::array({ json{ { "pose", json::array({ 1, 2, 3, 4, 5 }) }, { "wait", 100 } } });
	const json events = json::array({
		Button(10, 1, "keyboard", "up", 65),
		Button(20, 2, "keyboard", "down", 17),
		Button(30, 3, "keyboard", "up", 17),
		Button(90, 4, "keyboard", "down", 66),
		Button(99, 5, "keyboard", "down", 65),
	});
	const json plan = InterleaveReplayableActivity(steps, events, "recording:test", true, { 65, 66 });
	CHECK(plan["report"]["suppressedHotkeyTransitions"] == 3);
	CHECK(plan["report"]["replayedKeyboardTransitions"] == 2);
	for (const auto& step : plan["steps"])
		if (step.value("tool", std::string{}) == "input")
			CHECK(step["args"]["key"] == 17);
}

TEST_CASE("replay without reserved keys still injects every keyboard transition")
{
	const json events = json::array({
		Button(10, 1, "keyboard", "down", 65),
		Button(20, 2, "keyboard", "up", 65),
	});
	const json plan = InterleaveReplayableActivity(json::array(), events, "recording:test", true);
	CHECK(plan["report"]["suppressedHotkeyTransitions"] == 0);
	CHECK(plan["report"]["replayedKeyboardTransitions"] == 2);
}

TEST_CASE("replaying an older capture skips its console typing")
{
	json events = ConsoleSession(100);
	events.push_back(Button(300, 20, "keyboard", "down", 17));
	events.push_back(Button(310, 21, "keyboard", "up", 17));
	const json plan = InterleaveReplayableActivity(json::array(), events, "recording:test", true);
	CHECK(plan["report"]["replayedKeyboardTransitions"] == 2);
	for (const auto& step : plan["steps"])
		if (step.value("tool", std::string{}) == "input")
			CHECK(step["args"]["key"] == 17);
}
