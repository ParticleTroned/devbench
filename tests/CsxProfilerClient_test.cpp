#include "CsxProfilerClient.h"
#include "PerformanceToolPolicy.h"
#include "test_framework.h"

using namespace CSX::ProfilerAPI;
using dvb::json;
namespace
{
	struct Fake
	{
		Interface003       api;
		CaptureProgress001 progress;
		int                starts = 0;
		std::uint32_t      invalidGpu = 0;
		bool               qualityFails = false;
		bool               snapshotFails = false;
		int                enables = 0;
		Fake()
		{
			auto& paired = api.sources.paired;
			paired.structSize = sizeof(api);
			paired.minor = QualityServiceMinor;
			paired.context = this;
			paired.GetSnapshot = [](const void* context, Snapshot001* out) {
				if (static_cast<const Fake*>(context)->snapshotFails)
					return Status::kInternalError;
				out->available = out->enabled = 1;
				out->capturing = 1;
				return Status::kSuccess;
			};
			paired.SetEnabled = [](const void* context, std::uint32_t) {
				auto& self = *const_cast<Fake*>(static_cast<const Fake*>(context));
				++self.enables;
				self.snapshotFails = true;
				return Status::kSuccess;
			};
			paired.ClearHistory = [](const void*) { return Status::kSuccess; };
			paired.StartCapture = [](const void* context, const CaptureRequest001* request, CaptureProgress001* out) {
				auto& self = *const_cast<Fake*>(static_cast<const Fake*>(context));
				if (self.progress.state == CaptureState::kRunning)
					return Status::kBusy;
				++self.starts;
				self.progress.captureId = self.starts;
				self.progress.state = CaptureState::kRunning;
				self.progress.requestedFrames = request->frameCount;
				*out = self.progress;
				return Status::kSuccess;
			};
			paired.GetCaptureProgress = [](const void* context, std::uint64_t id, CaptureProgress001* out) {
				const auto& self = *static_cast<const Fake*>(context);
				if (id != self.progress.captureId)
					return Status::kCaptureNotFound;
				*out = self.progress;
				return Status::kSuccess;
			};
			paired.CancelCapture = [](const void* context, std::uint64_t, CaptureProgress001* out) {
				auto& self = *const_cast<Fake*>(static_cast<const Fake*>(context));
				self.progress.state = CaptureState::kCancelled;
				*out = self.progress;
				return Status::kSuccess;
			};
			paired.GetCaptureTimerCount = [](const void*, std::uint64_t) -> std::uint32_t { return 0; };
			paired.GetCaptureTimerDescriptor = [](const void*, std::uint64_t, std::uint32_t, TimerDescriptor001*) { return Status::kTimerNotFound; };
			paired.GetCaptureHistorySample = [](const void*, std::uint64_t, std::uint32_t, TimingDomain, std::uint32_t, float*) { return Status::kTimerNotFound; };
			api.GetCurrentCapture = [](const void* context, CaptureProgress001* out) {
				*out = static_cast<const Fake*>(context)->progress;
				return Status::kSuccess;
			};
			api.GetCaptureQuality = [](const void* context, std::uint64_t, CaptureQuality001* out) {
				if (static_cast<const Fake*>(context)->qualityFails)
					return Status::kInternalError;
				out->maximumTimersPerFrame = 128;
				out->invalidGpuFrames = static_cast<const Fake*>(context)->invalidGpu;
				return Status::kSuccess;
			};
		}
	};
	json Request(const char* action, const char* command = "1")
	{
		return { { "contractMajor", 1 }, { "clientId", "owner" }, { "commandId", command }, { "action", action } };
	}
}

TEST_CASE("profiler retries are idempotent and conflicting command IDs fail")
{
	Fake                   fake;
	dvb::CsxProfilerClient client;
	auto                   args = Request("start_capture");
	args["frameCount"] = 20;
	const auto first = client.Dispatch(fake.api, args, "build");
	CHECK(client.Dispatch(fake.api, args, "build") == first);
	CHECK(fake.starts == 1);
	args["frameCount"] = 30;
	CHECK_THROWS(client.Dispatch(fake.api, args, "build"));
	CHECK(fake.starts == 1);
	auto cancel = Request("cancel_capture", "2");
	cancel["captureId"] = 1;
	cancel["clientId"] = "other";
	CHECK_THROWS(client.Dispatch(fake.api, cancel, "build"));
	CHECK(fake.progress.state == CaptureState::kRunning);
	cancel["clientId"] = "owner";
	CHECK(client.Dispatch(fake.api, cancel, "build")["state"] == "cancelled");
}

TEST_CASE("profiler validates requests before invoking the native interface")
{
	Fake                   fake;
	dvb::CsxProfilerClient client;
	for (const auto value : { json(-1), json(0), json(301), json(1.5), json("20") }) {
		auto args = Request("start_capture");
		args["frameCount"] = value;
		CHECK_THROWS(client.Dispatch(fake.api, args, "build"));
	}
	auto args = Request("start_capture");
	CHECK_THROWS(client.Dispatch(fake.api, args, "build"));
	args["frameCount"] = 20;
	args["enabled"] = true;
	CHECK_THROWS(client.Dispatch(fake.api, args, "build"));
	CHECK(fake.starts == 0);
	args.erase("enabled");
	fake.api.GetCaptureQuality = nullptr;
	CHECK_THROWS(client.Dispatch(fake.api, args, "build"));
	CHECK(fake.starts == 0);
}

TEST_CASE("capture validity preserves invalid frames and blocks active export")
{
	Fake                   fake;
	dvb::CsxProfilerClient client;
	auto                   start = Request("start_capture");
	start["frameCount"] = 20;
	client.Dispatch(fake.api, start, "build");
	auto exportArgs = Request("capture_export", "2");
	exportArgs["captureId"] = 1;
	CHECK_THROWS(client.Dispatch(fake.api, exportArgs, "build"));
	auto disable = Request("set_enabled", "3");
	disable["enabled"] = false;
	CHECK_THROWS(client.Dispatch(fake.api, disable, "build"));
	fake.progress.state = CaptureState::kCompleted;
	fake.progress.submittedFrames = fake.progress.resolvedFrames = 20;
	fake.invalidGpu = 1;
	const auto result = client.Dispatch(fake.api, exportArgs, "build");
	CHECK(result["progress"]["quality"]["invalidGpuFrames"] == 1);
	CHECK(result["progress"]["quality"]["gpuComplete"] == false);
	CHECK(result["progress"]["quality"]["cpuComplete"] == true);
	CHECK(result["nextOffset"].is_null());
}

TEST_CASE("successful profiler mutations retain receipts when follow-up reads fail")
{
	Fake                   fake;
	dvb::CsxProfilerClient client;
	auto                   start = Request("start_capture");
	start["frameCount"] = 20;
	fake.qualityFails = true;
	const auto first = client.Dispatch(fake.api, start, "build");
	CHECK(first["quality"]["available"] == false);
	CHECK(first["quality"]["gpuComplete"] == false);
	CHECK(client.Dispatch(fake.api, start, "build") == first);
	CHECK(fake.starts == 1);
	auto cancel = Request("cancel_capture", "2");
	cancel["captureId"] = 1;
	CHECK(client.Dispatch(fake.api, cancel, "build")["state"] == "cancelled");
	auto disable = Request("set_enabled", "3");
	disable["enabled"] = false;
	const auto disabled = client.Dispatch(fake.api, disable, "build");
	CHECK(disabled["enabled"] == false);
	CHECK(client.Dispatch(fake.api, disable, "build") == disabled);
	CHECK(fake.enables == 1);
}

TEST_CASE("screenshot admission waits for encoding and publication and fails closed")
{
	const json idle{ { "ok", true }, { "result", { { "dispatcher", { { "pendingOperations", 0 }, { "queuedManualCaptures", 0 }, { "queuedSequenceFrames", 0 }, { "activeSequences", 0 }, { "activeAcquisitionRequestId", nullptr } } },
													 { "worker", { { "outstandingCaptureJobs", 0 } } },
													 { "ioWorkers", { { "destinationPreparationOutstanding", 0 }, { "manifestPublicationOutstanding", 0 }, { "retiredManifestSnapshots", 0 } } } } } };
	dvb::PerformanceToolPolicy::RequireScreenshotIdle(idle);
	for (const auto& [group, fields] : idle["result"].items()) {
		for (const auto& [field, value] : fields.items()) {
			auto busy = idle;
			busy["result"][group][field] = value.is_null() ? json("request") : json(1);
			CHECK_THROWS(dvb::PerformanceToolPolicy::RequireScreenshotIdle(busy));
			busy["result"][group].erase(field);
			CHECK_THROWS(dvb::PerformanceToolPolicy::RequireScreenshotIdle(busy));
		}
	}
	CHECK_THROWS(dvb::PerformanceToolPolicy::RequireScreenshotIdle(json::object()));
}
