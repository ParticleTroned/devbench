#include "RecordingManifest.h"
#include "test_framework.h"

using dvb::json;
using dvb::Recording::RecordingCorrelationId;
using dvb::Recording::ValidateRecordingStopCorrelation;

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

TEST_CASE("record stop guard accepts matching captures and legacy callers")
{
	const json manifest{ { "correlationId", "owned-capture" } };
	CHECK_NOTHROW(ValidateRecordingStopCorrelation(json::object(), manifest));
	CHECK_NOTHROW(ValidateRecordingStopCorrelation(json::object(), nullptr));
	CHECK_NOTHROW(ValidateRecordingStopCorrelation({ { "expectedCorrelationId", "owned-capture" } }, manifest));
	const auto maximum = std::string(128, 'x');
	CHECK_NOTHROW(ValidateRecordingStopCorrelation({ { "expectedCorrelationId", maximum } }, { { "correlationId", maximum } }));
}

TEST_CASE("record stop guard rejects replaced or unidentified captures without mutation")
{
	for (const json manifest : { json(nullptr), json::object(), json{ { "correlationId", "replacement-capture" } } }) {
		const auto original = manifest;
		try {
			ValidateRecordingStopCorrelation({ { "expectedCorrelationId", "owned-capture" } }, manifest);
			CHECK_MESSAGE(false, "mismatched recording was accepted");
		} catch (const dvb::ToolError& error) {
			CHECK(error.code == 409);
		}
		CHECK(manifest == original);
	}
}

TEST_CASE("record stop guard rejects invalid identity arguments")
{
	const json manifest{ { "correlationId", "owned-capture" } };
	for (const json invalid : { json(nullptr), json(false), json(42), json::array(), json::object(), json(""), json(std::string(129, 'x')) }) {
		try {
			ValidateRecordingStopCorrelation({ { "expectedCorrelationId", invalid } }, manifest);
			CHECK_MESSAGE(false, "invalid guard was accepted");
		} catch (const dvb::ToolError& error) {
			CHECK(error.code == 400);
		}
	}
}
