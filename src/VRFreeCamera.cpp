#include "VRFreeCamera.h"

#include "ToolRegistry.h"

namespace dvb::VRFreeCamera
{
	namespace
	{
		RE::BSTSmartPointer<RE::TESCameraState> g_previousState;
		RE::PlayerCamera*                       g_owner = nullptr;

		void Release()
		{
			g_previousState.reset();
			g_owner = nullptr;
		}
	}

	void SetEnabled(bool a_enabled)
	{
		auto* camera = RE::PlayerCamera::GetSingleton();
		auto* data = camera ? camera->GetVRRuntimeData() : nullptr;
		if (!data)
			throw ToolError(422, "VR player camera is unavailable");
		auto* freeState = static_cast<RE::FreeCameraState*>(data->cameraStates[RE::CameraState::kFree].get());
		if (!freeState || freeState->camera != camera || freeState->id != RE::CameraState::kFree)
			throw ToolError(422, "VR free-camera state is unavailable");

		const bool active = camera->currentState.get() == freeState;
		if (active == a_enabled) {
			if (!active)
				Release();
			return;
		}

		if (!a_enabled) {
			if (g_owner != camera || !g_previousState || g_previousState->camera != camera)
				throw ToolError(409, "VR free camera was not activated by devbench; no prior camera state to restore");
			camera->SetState(g_previousState.get());
			if (camera->currentState != g_previousState)
				throw ToolError(500, "VR camera state restoration failed");
			Release();
			return;
		}

		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!camera->currentState || !camera->cameraRoot || !player || !player->Get3D() || !player->GetParentCell() ||
			!RE::PlayerControls::GetSingleton())
			throw ToolError(422, "VR free camera requires a loaded scene and player controls");

		RE::NiQuaternion rotation{};
		RE::NiPoint3     translation{};
		// VR inserts a virtual slot before Update; universal builds must dispatch explicitly.
		REL::RelocateVirtual<decltype(&RE::TESCameraState::GetRotation)>(0x04, 0x05, camera->currentState.get(), rotation);
		REL::RelocateVirtual<decltype(&RE::TESCameraState::GetTranslation)>(0x05, 0x06, camera->currentState.get(), translation);
		freeState->translation = translation;

		// Preserve the native free-camera pitch/yaw convention instead of generic XYZ Euler angles.
		using SetRotation = void (*)(RE::FreeCameraState*, const RE::NiQuaternion*);
		static const REL::Relocation<SetRotation> setRotation{ REL::VariantID(0, 0, 0x873B50) };
		setRotation(freeState, &rotation);

		// VR's native toggle dereferences a null state and never performs this transition.
		g_previousState = camera->currentState;
		g_owner = camera;
		camera->SetState(freeState);
		if (camera->currentState.get() != freeState) {
			Release();
			throw ToolError(500, "VR free-camera activation failed");
		}
	}

	void Reset(bool a_restore)
	{
		if (!REL::Module::IsVR())
			return;
		auto* camera = RE::PlayerCamera::GetSingleton();
		if (a_restore && camera && camera == g_owner && g_previousState &&
			g_previousState->camera == camera && camera->IsInFreeCameraMode())
			camera->SetState(g_previousState.get());
		Release();
	}
}
