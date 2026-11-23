#pragma once
#include "ToolRegistry.h"
#include <cmath>

namespace dvb::PerformanceToolPolicy
{
	/** Enforce flat tool schemas on both REST and MCP before dispatch. */
	inline void Validate(const json& schema, const json& args)
	{
		if (!args.is_object())
			throw ToolError(400, "Arguments must be an object");
		for (const auto& key : schema.value("required", json::array()))
			if (!args.contains(key.get<std::string>()))
				throw ToolError(400, "Missing required argument: " + key.get<std::string>());
		const auto& properties = schema.at("properties");
		for (const auto& [key, value] : args.items()) {
			if (!properties.contains(key))
				throw ToolError(400, "Unsupported argument: " + key);
			const auto& rule = properties[key];
			const auto  type = rule.value("type", std::string{});
			if ((type == "string" && !value.is_string()) || (type == "boolean" && !value.is_boolean()) ||
				(type == "integer" && !value.is_number_integer()))
				throw ToolError(400, "Invalid argument type: " + key);
			if (rule.contains("const") && rule["const"] != value)
				throw ToolError(400, "Invalid constant: " + key);
			if (rule.contains("enum") && std::find(rule["enum"].begin(), rule["enum"].end(), value) == rule["enum"].end())
				throw ToolError(400, "Unsupported value: " + key);
			if (value.is_string()) {
				const auto& text = value.get_ref<const std::string&>();
				if (text.size() < rule.value("minLength", 0u) || text.size() > rule.value("maxLength", 4096u) || text.find('\0') != std::string::npos)
					throw ToolError(400, "Invalid string length or embedded NUL: " + key);
			}
			if (value.is_number()) {
				const double number = value.get<double>();
				if (!std::isfinite(number) || (rule.contains("minimum") && number < rule["minimum"].get<double>()) ||
					(rule.contains("maximum") && number > rule["maximum"].get<double>()))
					throw ToolError(400, "Argument out of range: " + key);
			}
		}
	}

	inline json EnvelopeProperties(const json& actions)
	{
		const json id{ { "type", "string" }, { "minLength", 1 }, { "maxLength", 128 } };
		return { { "contractMajor", { { "type", "integer" }, { "const", 1 } } },
			{ "clientId", id }, { "commandId", id }, { "expectedBuildId", id },
			{ "action", { { "type", "string" }, { "enum", actions } } } };
	}

	inline json Schema(json properties)
	{
		return { { "type", "object" }, { "additionalProperties", false },
			{ "required", { "contractMajor", "clientId", "commandId", "action" } }, { "properties", std::move(properties) } };
	}

	inline json ProfilerSchema()
	{
		auto properties = EnvelopeProperties({ "snapshot", "set_enabled", "clear_history", "start_capture", "capture_status", "cancel_capture", "capture_export" });
		properties["enabled"] = properties["clearHistory"] = { { "type", "boolean" } };
		properties["frameCount"] = { { "type", "integer" }, { "minimum", 1 }, { "maximum", 300 } };
		properties["captureId"] = { { "type", "integer" }, { "minimum", 1 }, { "maximum", 9007199254740991ull } };
		properties["offset"] = { { "type", "integer" }, { "minimum", 0 }, { "maximum", 4294967295ull } };
		properties["limit"] = { { "type", "integer" }, { "minimum", 1 }, { "maximum", 128 } };
		return Schema(std::move(properties));
	}

	inline void ValidateProfiler(const json& args)
	{
		Validate(ProfilerSchema(), args);
		const auto action = args.at("action").get<std::string_view>();
		const bool capture = action == "capture_status" || action == "cancel_capture" || action == "capture_export";
		if ((capture && !args.contains("captureId")) || (action == "start_capture" && !args.contains("frameCount")) ||
			(action == "set_enabled" && !args.contains("enabled")))
			throw ToolError(400, "Missing action parameter");
		for (const auto& [key, value] : args.items()) {
			if (key == "contractMajor" || key == "clientId" || key == "commandId" || key == "action" || key == "expectedBuildId")
				continue;
			if ((capture && key == "captureId") || (action == "start_capture" && (key == "frameCount" || key == "clearHistory")) ||
				(action == "set_enabled" && key == "enabled") || (action == "capture_export" && (key == "offset" || key == "limit")))
				continue;
			throw ToolError(400, "Argument does not apply to this action: " + key);
		}
	}
}
