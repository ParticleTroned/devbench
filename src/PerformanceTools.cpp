#include "PerformanceTools.h"
#include "BuildProfile.h"
#include "CSX/CSperformanceapi.h"
#include "CSX/CSscreenshotapi.h"
#include "CSX/CSserviceapi.h"
#include "CsxProfilerClient.h"
#include "CsxServiceSchemas.h"
#include "MainThread.h"
#include "PerformanceToolPolicy.h"
#include "Server.h"
#include <SKSE/SKSE.h>

namespace dvb
{
	namespace
	{
		using namespace CSX;
		const ServiceAPI::Registry001& Registry()
		{
			static const ServiceAPI::Registry001* registry = nullptr;
			if (!registry) {
				ServiceAPI::RegistryMessage001 message;
				auto*                          messaging = SKSE::GetMessagingInterface();
				if (!messaging || !messaging->Dispatch(ServiceAPI::RegistryMessageType, &message, sizeof(message), ServiceAPI::ProviderName) ||
					message.status != ServiceAPI::Status::kSuccess || !message.registry)
					throw ToolError(503, "CSX public API is unavailable");
				const auto* candidate = message.registry;
				if (candidate->structSize < sizeof(*candidate) || candidate->abiMajor != ServiceAPI::RegistryAbiMajor ||
					!candidate->QueryService || !candidate->GetProducerIdentity)
					throw ToolError(503, "Incompatible CSX registry ABI");
				registry = candidate;
			}
			return *registry;
		}

		template <class Interface>
		const Interface& Query(const char* name, std::uint32_t minor)
		{
			const auto&                 registry = Registry();
			ServiceAPI::ServiceQuery001 query;
			query.name = name;
			query.major = 1;
			query.minimumMinor = query.maximumMinor = minor;
			query.requiredCapabilities = ServiceAPI::kCapabilityInspection;
			ServiceAPI::ServiceDescriptor001 descriptor;
			const void*                      service = nullptr;
			if (registry.QueryService(registry.context, &query, &service, &descriptor) != ServiceAPI::Status::kSuccess || !service ||
				descriptor.major != 1 || descriptor.minor != minor || *static_cast<const std::uint32_t*>(service) < sizeof(Interface))
				throw ToolError(503, std::string("Compatible CSX service is unavailable: ") + name);
			return *static_cast<const Interface*>(service);
		}

		const PerformanceAPI::Interface001& ProductionApi()
		{
			const auto& api = Query<PerformanceAPI::Interface001>(PerformanceAPI::ServiceName, 0);
			if (api.major != 1 || api.minor != 0 || !api.Dispatch || api.instrumentationFlags != 0)
				throw ToolError(409, "Performance profile requires production CSX with DevBench diagnostics and Tracy compiled out");
			return api;
		}

		json Producer(const json& args)
		{
			ServiceAPI::ProducerIdentity001 identity;
			const auto&                     registry = Registry();
			if (registry.GetProducerIdentity(registry.context, &identity) != ServiceAPI::Status::kSuccess)
				throw ToolError(503, "CSX producer identity is unavailable");
			const auto copy = [](const char* text) { return std::string(text ? text : ""); };
			json       producer{ { "buildId", copy(identity.buildId) }, { "sourceCommit", copy(identity.sourceCommit) },
				{ "sourceDirty", identity.sourceDirty != 0 }, { "artifactSha256", copy(identity.artifactSha256) },
				{ "manifestVerified", identity.manifestVerified != 0 }, { "configuration", copy(identity.configuration) } };
			if (args.contains("expectedBuildId") && args["expectedBuildId"] != producer["buildId"])
				throw ToolError(409, "Loaded CSX does not match expectedBuildId");
			return producer;
		}

		template <class Request, class Response, class Interface>
		json Forward(const Interface& api, const json& args, std::uint32_t maximumBytes)
		{
			const auto bytes = args.dump();
			if (bytes.size() > maximumBytes)
				throw ToolError(413, "CSX API request exceeds its byte limit");
			Request request;
			request.jsonUtf8 = bytes.data();
			request.jsonBytes = static_cast<std::uint32_t>(bytes.size());
			Response response;
			if (api.Dispatch(api.context, &request, &response) != decltype(response.status)::kSuccess ||
				response.status != decltype(response.status)::kSuccess || !response.jsonUtf8 || response.jsonBytes == 0 || response.jsonBytes > 64u * 1024u * 1024u)
				throw ToolError(502, "CSX API transport failed");
			return json::parse(response.jsonUtf8, response.jsonUtf8 + response.jsonBytes);
		}

		void RequireIdle(const PerformanceAPI::Interface001& api)
		{
			const json status{ { "contractMajor", 1 }, { "clientId", "devbench.performance" }, { "commandId", "status" }, { "action", "status" } };
			const auto response = Forward<PerformanceAPI::Request001, PerformanceAPI::Response001>(api, status, PerformanceAPI::MaximumRequestBytes);
			if (!response.value("ok", false) || !response.contains("result") || response["result"].value("active", true))
				throw ToolError(409, "CSX measurement is active or not ready");
		}

		const ScreenshotAPI::Interface001& ScreenshotApi()
		{
			const auto& api = Query<ScreenshotAPI::Interface001>(ScreenshotAPI::ServiceName, 0);
			if (api.major != 1 || api.minor != 0 || !api.Dispatch)
				throw ToolError(503, "Incompatible screenshot API");
			return api;
		}

		void RequireProfilerIdle()
		{
			const auto&                     api = Query<ProfilerAPI::Interface003>(ProfilerAPI::ServiceName, ProfilerAPI::QualityServiceMinor);
			ProfilerAPI::CaptureProgress001 progress;
			if (!api.GetCurrentCapture || api.GetCurrentCapture(api.sources.paired.context, &progress) != ProfilerAPI::Status::kSuccess || progress.state == ProfilerAPI::CaptureState::kRunning)
				throw ToolError(409, "CSX profiler capture is active or not ready");
		}

		void RequireScreenshotIdle()
		{
			const json status{ { "contractMajor", 1 }, { "clientId", "devbench.performance" }, { "commandId", "status" }, { "action", "status" } };
			PerformanceToolPolicy::RequireScreenshotIdle(Forward<ScreenshotAPI::Request001, ScreenshotAPI::Response001>(ScreenshotApi(), status, ScreenshotAPI::MaximumRequestBytes));
		}

		void RegisterRestricted(ToolRegistry& registry, ToolDescriptor descriptor, ToolHandler handler)
		{
			const auto schema = descriptor.inputSchema;
			registry.Register(std::move(descriptor), [schema, handler = std::move(handler)](const json& args, const ToolContext& context) {
				PerformanceToolPolicy::Validate(schema, args);
				return handler(args, context);
			});
		}
	}

	void LoadPerformanceSave(const std::string& name)
	{
		MainThread::RunAndWait([name] {
			RequireIdle(ProductionApi());
			RequireProfilerIdle();
			RequireScreenshotIdle();
			auto* manager = RE::BGSSaveLoadManager::GetSingleton();
			if (!manager)
				throw ToolError(503, "Save/load manager is unavailable");
			manager->Load(name.c_str(), false);
			return json{};
		});
	}

	void RegisterPerformanceTools(ToolRegistry& registry, ToolDescriptor game, ToolHandler gameHandler,
		ToolDescriptor inspect, ToolHandler inspectHandler)
	{
		game.description = "Performance profile: list saves, load a named save or loadLast. Loads are asynchronous; inspect state and scene for completion. Loads reject active measurements, profiler captures and screenshot work. Other game actions are unavailable.";
		auto& gameSchema = game.inputSchema;
		gameSchema["properties"]["limit"]["minimum"] = 1;
		gameSchema["properties"]["limit"]["maximum"] = 1000;
		gameSchema["additionalProperties"] = false;
		gameSchema["required"] = { "action" };
		gameSchema["properties"]["action"]["enum"] = { "list", "load", "loadLast" };
		gameSchema["properties"]["action"]["description"] = "list | load | loadLast";
		for (const auto* key : { "hours", "scale", "holdMs", "freeze", "allowHigh", "allowTimeScale" }) gameSchema["properties"].erase(key);
		RegisterRestricted(registry, std::move(game), [gameHandler](const json& args, const ToolContext& context) {
			if (args.at("action") != "list" && (args.contains("dir") || args.contains("filter") || args.contains("limit") || args.contains("detail")))
				throw ToolError(400, "dir, filter, limit and detail apply only to save listing");
			if (args.at("action") != "load" && args.contains("name"))
				throw ToolError(400, "name applies only to load");
			return gameHandler(args, context);
		});
		inspect.description = "On-demand state, scene, health and registrants for the minimal performance profile. Only SKSE load lifecycle messages are forwarded; no game event sinks or periodic sampling are installed.";
		inspect.inputSchema = { { "type", "object" }, { "additionalProperties", false }, { "properties", { { "kind", { { "type", "string" }, { "enum", { "state", "scene", "health", "registrants" } } } } } } };
		RegisterRestricted(registry, std::move(inspect), [inspectHandler](const json& args, const ToolContext& context) {
			auto result = inspectHandler(args, context);
			result["profile"] = BuildProfileName;
			return result;
		});
		registry.Register({ "ping", "Read the active DevBench build profile.", DefaultInputSchema(), true },
			[](const json&, const ToolContext&) { return json{ { "ok", true }, { "profile", BuildProfileName } }; });
		registry.Register({ "mcp_bridge_setup", "Read connection settings for the existing restart-tolerant DevBench bridge.", DefaultInputSchema(), true },
			[](const json&, const ToolContext&) { return BridgeDiscoveryInfo(); });

		const auto profilerSchema = PerformanceToolPolicy::ProfilerSchema();
		registry.Register({ "communityshaders.profiler_api", "Production CSX profiler API: explicitly set_enabled, start_capture (1-300 frames), poll capture_status and page capture_export (up to 128 timers). Only the capture owner can cancel. Reuse commandId only for identical mutation retries; 64 receipts retained. Quality reports invalid CPU/GPU frames. No automatic background polling.", profilerSchema, false },
			[](const json& args, const ToolContext&) {
				PerformanceToolPolicy::ValidateProfiler(args);
				return MainThread::RunAndWait([args] {
					const auto& performance = ProductionApi();
					auto        producer = Producer(args);
					const auto  action = args.at("action").get<std::string_view>();

					const auto&              api = Query<ProfilerAPI::Interface003>(ProfilerAPI::ServiceName, ProfilerAPI::QualityServiceMinor);
					static CsxProfilerClient client;
					auto                     result = client.Dispatch(api, args, producer.at("buildId").get<std::string>(), [&] {
					if (action == "set_enabled" || action == "clear_history" || action == "start_capture") {
						RequireIdle(performance);
						RequireScreenshotIdle();
					}
					});
					return json{ { "ok", true }, { "producer", std::move(producer) }, { "result", std::move(result) } };
				});
			});
		// The native services enforce their domain schemas and retain mutation receipts.
		registry.Register({ "communityshaders.performance_tuning", "Production CSX performance controls and saved results. Uses the CSX API contract; requests require contractMajor=1, clientId, commandId and action. Scan batches run three scans with ten-second gaps and automatic export; recordFpsVR is optional. Export actions require an explicit directory.", PerformanceSchema(), false },
			[](const json& args, const ToolContext&) {
				PerformanceToolPolicy::Validate(PerformanceSchema(), args);
				return MainThread::RunAndWait([args] {
					const auto& api = ProductionApi();
					const auto producer = Producer(args);
					const auto action = args.at("action").get<std::string_view>();
					static CsxCommandJournal journal;
					return journal.Dispatch(producer.at("buildId").get<std::string>(), args, action != "status" && action != "results_status", [&] {
						if (action.starts_with("start_") || action == "set_feature_enabled")
							RequireScreenshotIdle();
						return Forward<PerformanceAPI::Request001, PerformanceAPI::Response001>(api, args, PerformanceAPI::MaximumRequestBytes);
					});
				});
			});
		registry.Register({ "communityshaders.screenshot", "Production CSX asynchronous screenshot API. Rejects capture and sequence_start during measurements or profiler captures; poll request_get for terminal artifact status. Native CSX validates requests and owns encoding, cancellation and receipts.", ScreenshotSchema(), false },
			[](const json& args, const ToolContext&) {
				if (!args.is_object() || !args.contains("action") || !args["action"].is_string())
					throw ToolError(400, "Invalid screenshot request");
				for (const auto* key : { "clientId", "commandId" })
					if (!args.contains(key) || !args[key].is_string() || args[key].get_ref<const std::string&>().empty() || args[key].get_ref<const std::string&>().size() > 128)
						throw ToolError(400, "Bounded clientId and commandId strings are required");
				return MainThread::RunAndWait([args] {
					const auto& performance = ProductionApi();
					const auto producer = Producer(args);
					const auto action = args.at("action").get<std::string_view>();
					const bool mutation = action != "capabilities" && action != "status" && action != "settings_get" && action != "settings_validate" &&
						action != "request_get" && action != "request_list" && action != "events_poll";
					static CsxCommandJournal journal;
					return journal.Dispatch(producer.at("buildId").get<std::string>(), args, mutation, [&] {
						if (action == "capture" || action == "sequence_start") {
							RequireIdle(performance);
							RequireProfilerIdle();
						}
						return Forward<ScreenshotAPI::Request001, ScreenshotAPI::Response001>(ScreenshotApi(), args, ScreenshotAPI::MaximumRequestBytes);
					});
				});
			});
	}
}
