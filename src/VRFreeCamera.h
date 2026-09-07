#pragma once

namespace dvb::VRFreeCamera
{
	/// Activate Skyrim VR's existing free-camera state, or restore the prior state.
	/// Call on the main thread. Leaves the engine's freeze-time flag unchanged.
	void SetEnabled(bool a_enabled);

	/// Restore before loading a save; otherwise release state from the old session.
	void Reset(bool a_restore);
}
