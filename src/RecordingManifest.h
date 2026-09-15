#pragma once

#include "Json.h"
#include <string>

namespace dvb::Recording
{
	/// Return the recording identity, including the idle state before start.
	inline std::string RecordingCorrelationId(const json& manifest)
	{
		// The recorder has no manifest until its first successful start.
		return manifest.is_null() ? std::string{} : manifest.value("correlationId", std::string{});
	}
}
