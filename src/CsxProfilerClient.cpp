#include "CsxProfilerClient.h"
#include "PerformanceToolPolicy.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace dvb
{
	namespace
	{
		using namespace CSX::ProfilerAPI;
		constexpr std::uint32_t kExportTimerLimit = 128;
		const char*             StatusName(Status a_status)
		{
			switch (a_status) {
			case Status::kSuccess:
				return "success";
			case Status::kInvalidArgument:
				return "invalid_argument";
			case Status::kStructureTooSmall:
				return "structure_too_small";
			case Status::kUnavailable:
				return "unavailable";
			case Status::kWrongThread:
				return "wrong_thread";
			case Status::kDisabled:
				return "disabled";
			case Status::kBusy:
				return "busy";
			case Status::kCaptureNotFound:
				return "capture_not_found";
			case Status::kTimerNotFound:
				return "timer_not_found";
			default:
				return "internal_error";
			}
		}

		const char* CaptureStateName(CaptureState a_state)
		{
			switch (a_state) {
			case CaptureState::kRunning:
				return "running";
			case CaptureState::kCompleted:
				return "completed";
			case CaptureState::kCancelled:
				return "cancelled";
			default:
				return "none";
			}
		}

		void Check(Status status)
		{
			if (status != Status::kSuccess)
				throw ToolError(status == Status::kBusy ? 409 : 422, StatusName(status));
		}
		json ProgressJson(const Interface003& api, const CaptureProgress001& progress)
		{
			CaptureQuality001 quality;
			const auto        qualityStatus = api.GetCaptureQuality(api.sources.paired.context, progress.captureId, &quality);
			if (qualityStatus != Status::kSuccess)
				return { { "captureId", progress.captureId }, { "state", CaptureStateName(progress.state) },
					{ "requestedFrames", progress.requestedFrames }, { "submittedFrames", progress.submittedFrames },
					{ "resolvedFrames", progress.resolvedFrames }, { "quality", { { "available", false }, { "error", StatusName(qualityStatus) }, { "gpuComplete", false }, { "cpuComplete", false } } } };
			const bool complete = progress.state == CaptureState::kCompleted && progress.requestedFrames != 0 &&
			                      progress.submittedFrames == progress.requestedFrames && progress.resolvedFrames == progress.requestedFrames;
			return { { "captureId", progress.captureId }, { "state", CaptureStateName(progress.state) },
				{ "requestedFrames", progress.requestedFrames }, { "submittedFrames", progress.submittedFrames },
				{ "resolvedFrames", progress.resolvedFrames }, { "quality", { { "available", true }, { "maxGpuScopesPerFrame", quality.maximumTimersPerFrame }, { "invalidGpuFrames", quality.invalidGpuFrames }, { "invalidCpuFrames", quality.invalidCpuFrames }, { "gpuComplete", complete && quality.invalidGpuFrames == 0 }, { "cpuComplete", complete && quality.invalidCpuFrames == 0 } } } };
		}
		json ApiFailure(Status status)
		{
			Check(status);
			return json::object();
		}
		json TimerJson(std::uint32_t a_index, const TimerDescriptor001& a_timer)
		{
			return { { "index", a_index }, { "name", a_timer.name ? a_timer.name : "" },
				{ "hasGpu", a_timer.hasGpu != 0 }, { "hasCpu", a_timer.hasCpu != 0 },
				{ "activeGpu", a_timer.activeGpu != 0 }, { "activeCpu", a_timer.activeCpu != 0 },
				{ "gpu", { { "ms", a_timer.gpuMs }, { "topLevelMs", a_timer.gpuTopLevelMs },
							 { "averageMs", a_timer.gpuAverageMs }, { "p95Ms", a_timer.gpuP95Ms },
							 { "p99Ms", a_timer.gpuP99Ms }, { "historyCount", a_timer.gpuHistoryCount } } },
				{ "cpu", { { "ms", a_timer.cpuMs }, { "averageMs", a_timer.cpuAverageMs },
							 { "p95Ms", a_timer.cpuP95Ms }, { "p99Ms", a_timer.cpuP99Ms },
							 { "historyCount", a_timer.cpuHistoryCount } } } };
		}

		json ExportCapture(const Interface003& qualityApi, const json& a_args)
		{
			const auto&        a_api = qualityApi.sources.paired;
			const auto         captureId = a_args.at("captureId").get<std::uint64_t>();
			CaptureProgress001 progress;
			const auto         status = a_api.GetCaptureProgress(a_api.context, captureId, &progress);
			if (status != Status::kSuccess)
				return ApiFailure(status);
			if (progress.state != CaptureState::kCompleted && progress.state != CaptureState::kCancelled)
				return ApiFailure(Status::kBusy);

			const auto count = a_api.GetCaptureTimerCount(a_api.context, captureId);
			const auto offset = a_args.value("offset", 0u);
			const auto limit = a_args.value("limit", kExportTimerLimit);
			if (offset > count)
				return ApiFailure(Status::kInvalidArgument);
			const auto end = offset + std::min(limit, count - offset);
			json       timers = json::array();
			for (auto index = offset; index < end; ++index) {
				TimerDescriptor001 timer;
				const auto         timerStatus = a_api.GetCaptureTimerDescriptor(a_api.context, captureId, index, &timer);
				if (timerStatus != Status::kSuccess)
					return ApiFailure(timerStatus);
				auto entry = TimerJson(index, timer);
				for (const auto domain : { TimingDomain::kCpu, TimingDomain::kGpu }) {
					const auto key = domain == TimingDomain::kCpu ? "cpu" : "gpu";
					const auto samples = domain == TimingDomain::kCpu ? timer.cpuHistoryCount : timer.gpuHistoryCount;
					if (samples > 300)
						return ApiFailure(Status::kInternalError);
					json history = json::array();
					for (std::uint32_t ordinal = 0; ordinal < samples; ++ordinal) {
						float      value = 0.0f;
						const auto sampleStatus = a_api.GetCaptureHistorySample(a_api.context, captureId, index, domain, ordinal, &value);
						if (sampleStatus != Status::kSuccess)
							return ApiFailure(sampleStatus);
						if (!std::isfinite(value) || value < 0.0f)
							return ApiFailure(Status::kInternalError);
						history.push_back(value);
					}
					entry[key]["available"] = samples != 0;
					entry[key]["samplesMs"] = std::move(history);
					if (samples == 0) {
						for (const auto metric : { "ms", "averageMs", "p95Ms", "p99Ms" })
							entry[key][metric] = nullptr;
						if (domain == TimingDomain::kGpu)
							entry[key]["topLevelMs"] = nullptr;
					}
				}
				timers.push_back(std::move(entry));
			}
			return { { "schema", "csx-profiler-capture-export-v1" }, { "captureId", captureId },
				{ "timingSemantics", "gpu_cpu_self_time" }, { "unit", "ms" },
				{ "historyOrder", "oldest_to_newest" }, { "historyHasEngineFrameIds", false },
				{ "progress", ProgressJson(qualityApi, progress) }, { "catalogCount", count },
				{ "offset", offset }, { "returnedCount", timers.size() },
				{ "nextOffset", end < count ? json(end) : json(nullptr) }, { "timers", std::move(timers) } };
		}

		json ReadSnapshot(const CSX::ProfilerAPI::Interface001& a_api)
		{
			Snapshot001 snapshot;
			const auto  status = a_api.GetSnapshot(a_api.context, &snapshot);
			if (status != Status::kSuccess && status != Status::kUnavailable)
				return ApiFailure(status);
			return {
				{ "status", StatusName(status) },
				{ "available", snapshot.available != 0 },
				{ "enabled", snapshot.enabled != 0 },
				{ "capturing", snapshot.capturing != 0 },
				{ "timerCount", snapshot.timerCount },
				{ "limits", { { "historyCapacity", snapshot.historyCapacity }, { "maximumTimers", snapshot.maximumTimers }, { "frameLatency", snapshot.frameLatency } } },
				{ "frame", { { "captured", snapshot.capturedFrameCount }, { "acquiredSlots", snapshot.acquiredSlots }, { "peakAcquiredSlots", snapshot.peakAcquiredSlots }, { "slotRefusals", snapshot.slotRefusals } } },
				{ "totalsMs", { { "gpu", snapshot.gpuTotalMs }, { "cpu", snapshot.cpuTotalMs }, { "resolvedGpu", snapshot.resolvedGpuTotalMs }, { "resolvedCpu", snapshot.resolvedCpuTotalMs } } },
				{ "capabilities", snapshot.capabilities },
				{ "buildId", snapshot.buildId ? snapshot.buildId : "" },
			};
		}

	}
	json CsxProfilerClient::Dispatch(const CSX::ProfilerAPI::Interface003& service, const json& args, const std::string& buildId)
	{
		PerformanceToolPolicy::ValidateProfiler(args);
		using namespace CSX::ProfilerAPI;
		const auto& api = service.sources.paired;
		if (api.structSize < sizeof(Interface003) || api.major != ServiceMajor || api.minor != QualityServiceMinor ||
			!api.GetSnapshot || !api.SetEnabled || !api.ClearHistory || !api.StartCapture || !api.GetCaptureProgress ||
			!api.CancelCapture || !api.GetCaptureTimerCount || !api.GetCaptureTimerDescriptor || !api.GetCaptureHistorySample || !service.GetCaptureQuality || !service.GetCurrentCapture)
			throw ToolError(503, "Incompatible CSX profiler interface");
		const auto        action = args.at("action").get<std::string>();
		const bool        mutation = action == "set_enabled" || action == "clear_history" || action == "start_capture" || action == "cancel_capture";
		const std::string client = args.at("clientId").get<std::string>();
		const std::string command = args.at("commandId").get<std::string>();
		for (const auto& receipt : receipts) {
			if (!mutation || receipt.build != buildId || receipt.client != client || receipt.command != command)
				continue;
			if (receipt.request != args)
				throw ToolError(409, "commandId was already used with different arguments");
			return receipt.result;
		}
		json result;
		if (action == "snapshot")
			result = ReadSnapshot(api);
		else if (action == "capture_export")
			result = ExportCapture(service, args);
		else if (action == "capture_status") {
			CaptureProgress001 progress;
			Check(api.GetCaptureProgress(api.context, args.at("captureId").get<std::uint64_t>(), &progress));
			result = ProgressJson(service, progress);
		} else if (mutation) {
			CaptureProgress001 current;
			Check(service.GetCurrentCapture(api.context, &current));
			if ((action == "set_enabled" || action == "clear_history") && current.state == CaptureState::kRunning)
				throw ToolError(409, "A profiler capture is active");
			if (action == "set_enabled") {
				Check(api.SetEnabled(api.context, args.at("enabled").get<bool>() ? 1 : 0));
				result = { { "enabled", args.at("enabled") } };
			} else if (action == "clear_history") {
				Check(api.ClearHistory(api.context));
				result = { { "cleared", true } };
			} else {
				CaptureProgress001 progress;
				if (action == "start_capture") {
					CaptureRequest001 request;
					request.frameCount = args.at("frameCount").get<std::uint32_t>();
					request.clearHistory = args.value("clearHistory", false) ? 1 : 0;
					Check(api.StartCapture(api.context, &request, &progress));
					owner = client;
					ownerBuild = buildId;
					ownedCapture = progress.captureId;
				} else {
					const auto id = args.at("captureId").get<std::uint64_t>();
					if (id != ownedCapture || client != owner || buildId != ownerBuild)
						throw ToolError(409, "Only the capture owner may cancel this capture");
					Check(api.CancelCapture(api.context, id, &progress));
				}
				result = ProgressJson(service, progress);
			}
		} else
			throw ToolError(400, "Unsupported profiler action");
		if (mutation) {
			if (receipts.size() == 64)
				receipts.pop_front();
			receipts.push_back({ buildId, client, command, args, result });
		}
		return result;
	}
}
