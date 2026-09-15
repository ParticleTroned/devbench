#include "RecordingWindow.h"
#include "VRInputState.h"
#include "test_framework.h"

using namespace dvb;
using namespace dvb::Recording;

TEST_CASE("observation capture crosses replay deadline without extending replay")
{
	RecordingWindow window;
	CHECK(window.RecordedMs(100000) == 0);
	CHECK(window.RemainingMs(100000) == 0);
	window.Start(kMaximumRecordingDurationMs);
	const auto longSession = kMaximumVRTrackedDurationMs + 380422;
	window.Sample(longSession);
	CHECK(window.lastSampleMs == longSession);
	CHECK(window.RecordedMs(longSession) == longSession);
	CHECK(window.RemainingMs(longSession) > 0);
	CHECK(kMaximumVRTrackedDurationMs == 1800000);
	CHECK(kMaximumVRTrackedFrames == 60000);
}

TEST_CASE("capture limit and delayed stop preserve the uncovered tail")
{
	RecordingWindow window;
	window.Start(1800000);
	window.Sample(1799978);
	window.End(1800050);
	window.End(2180400);
	window.Sample(2180400);
	CHECK(window.RecordedMs(2180400) == 1800000);
	CHECK(window.lastSampleMs == 1799978);
	CHECK(window.RemainingMs(2180400) == 0);
	CHECK(2180400 - window.RecordedMs(2180400) == 380400);
}

TEST_CASE("manual and capacity stops freeze time before finalization")
{
	RecordingWindow window;
	window.Start(kMaximumRecordingDurationMs);
	window.Sample(400);
	window.End(450);
	CHECK(window.RecordedMs(1500) == 450);
	CHECK(window.lastSampleMs == 400);
	window.Start(1000);
	CHECK(window.lastSampleMs == 0);
	CHECK(window.RecordedMs(250) == 250);
	CHECK(window.RemainingMs(250) == 750);
}
