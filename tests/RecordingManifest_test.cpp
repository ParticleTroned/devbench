#include "RecordingManifest.h"
#include "test_framework.h"

using dvb::json;
using dvb::Recording::RecordingCorrelationId;

TEST_CASE("recording identity is readable before start and after stop")
{
	json manifest;
	CHECK(RecordingCorrelationId(manifest).empty());
	manifest = { { "correlationId", "nr-colour-session" } };
	CHECK(RecordingCorrelationId(manifest) == "nr-colour-session");
	CHECK(manifest["correlationId"] == "nr-colour-session");
	manifest = json::object();
	CHECK(RecordingCorrelationId(manifest).empty());
	manifest = { { "correlationId", 42 } };
	CHECK_THROWS(RecordingCorrelationId(manifest));
}
