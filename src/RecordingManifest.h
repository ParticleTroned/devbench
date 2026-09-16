#pragma once

#include "Json.h"
#include "ToolRegistry.h"
#include <string>

namespace dvb::Recording
{
	/// Return the recording identity, including the idle state before start.
	inline std::string RecordingCorrelationId(const json& manifest)
	{
		// The recorder has no manifest until its first successful start.
		return manifest.is_null() ? std::string{} : manifest.value("correlationId", std::string{});
	}

	/// Validate an optional stop guard while holding the recorder mutex, before mutation.
	/// Omission preserves legacy stops; invalid UTF-8 byte length/type throws 400 and
	/// a missing or different recording identity throws 409 without changing the manifest.
	inline void ValidateRecordingStopCorrelation(const json& args, const json& manifest)
	{
		const auto expected = args.find("expectedCorrelationId");
		if (expected == args.end())
			return;
		if (!expected->is_string() || expected->get_ref<const std::string&>().empty() ||
			expected->get_ref<const std::string&>().size() > 128)
			throw ToolError(400, "expectedCorrelationId must be a nonempty string of at most 128 UTF-8 bytes");
		const auto actual = RecordingCorrelationId(manifest);
		if (expected->get_ref<const std::string&>() != actual)
			throw ToolError(409, "recording correlation mismatch: expected " + expected->dump() + ", observed " + json(actual).dump());
	}
}
