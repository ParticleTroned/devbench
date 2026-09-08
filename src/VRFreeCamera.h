#pragma once

#include <cstdint>

namespace dvb::VRFreeCamera
{
	using SessionToken = std::uint64_t;

	/// Capture on the caller thread before queuing a camera mutation.
	SessionToken CurrentSession();

	/// Main-thread operations. Leave the engine's freeze-time flag unchanged.
	void SetEnabled(bool a_enabled, SessionToken a_session);
	void Drive(float a_x, float a_y, float a_z, float a_pitch, float a_yaw, SessionToken a_session);
	/// Reconcile ownership with the current registered camera state on the main thread.
	bool IsOwned();

	/// Main-thread lifecycle: restore before loading; discard previous-session state afterward.
	/// Both boundaries invalidate queued camera mutations from the preceding scene.
	void BeginLoad();
	void EndLoad();
}
