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

/// Omitted guards preserve legacy behavior; a matching guard accepts the byte limit.
TEST_CASE("record stop guard accepts matching captures and legacy callers")
{
	const json manifest{ { "correlationId", "owned-capture" } };
	CHECK_NOTHROW(ValidateRecordingStopCorrelation(json::object(), manifest));
	CHECK_NOTHROW(ValidateRecordingStopCorrelation(json::object(), nullptr));
	CHECK_NOTHROW(ValidateRecordingStopCorrelation({ { "expectedCorrelationId", "owned-capture" } }, manifest));
	const auto maximum = std::string(128, 'x');
	CHECK_NOTHROW(ValidateRecordingStopCorrelation({ { "expectedCorrelationId", maximum } }, { { "correlationId", maximum } }));
}

/// Replaced captures and captures without caller identities fail closed.
TEST_CASE("record stop guard rejects replaced or unidentified captures without mutation")
{
	for (const json manifest : { json(nullptr), json::object(), json{ { "correlationId", "" } }, json{ { "correlationId", "replacement-capture" } } }) {
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

/// Malformed guards must report a client error rather than attempt finalization.
TEST_CASE("record stop guard rejects invalid identity arguments")
{
	const json manifest{ { "correlationId", "owned-capture" } };
	for (const json invalid : { json(nullptr), json(false), json(42), json(42.5), json::array(), json::object(), json(""), json(std::string(129, 'x')) }) {
		try {
			ValidateRecordingStopCorrelation({ { "expectedCorrelationId", invalid } }, manifest);
			CHECK_MESSAGE(false, "invalid guard was accepted");
		} catch (const dvb::ToolError& error) {
			CHECK(error.code == 400);
		}
	}
}

/// String limits count UTF-8 bytes and identities compare without normalization.
TEST_CASE("record stop guard preserves exact UTF-8 and embedded-null identities")
{
	const auto  letter = json::parse(R"("\u00e9")").get<std::string>();
	std::string maximum;
	for (int i = 0; i < 64; ++i)
		maximum += letter;
	CHECK(maximum.size() == 128);
	CHECK_NOTHROW(ValidateRecordingStopCorrelation({ { "expectedCorrelationId", maximum } }, { { "correlationId", maximum } }));
	const auto embeddedNull = json::parse(R"("owned\u0000capture")").get<std::string>();
	CHECK_NOTHROW(ValidateRecordingStopCorrelation({ { "expectedCorrelationId", embeddedNull } }, { { "correlationId", embeddedNull } }));

	dvb::ToolRegistry   registry;
	dvb::ToolDescriptor descriptor;
	descriptor.name = "guard";
	registry.Register(descriptor, [](const json& args, const dvb::ToolContext&) {
		ValidateRecordingStopCorrelation(args, { { "correlationId", "owned" } });
		return json{ { "accepted", true } };
	});
	for (const auto& identity : { maximum + letter, embeddedNull, std::string("Owned"), std::string("owned ") }) {
		const auto result = registry.Invoke("guard", { { "expectedCorrelationId", identity } }, {});
		CHECK(!result.ok);
		CHECK(result.errorCode == (identity.size() > 128 ? 400 : 409));
		CHECK(!result.errorMessage.empty());
	}
	CHECK(registry.Invoke("guard", { { "expectedCorrelationId", "owned" } }, {}).ok);
	CHECK(registry.Invoke("guard", json::object(), {}).ok);
}
