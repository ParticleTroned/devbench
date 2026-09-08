#include "test_framework.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

// Compile the production controller against a small camera model. This checks
// ownership, lifecycle and mutations; it does not claim to qualify Skyrim's ABI
// or rendered stereo. Namespace substitutions isolate these fakes from other TUs.
namespace camera_test_RE
{
	template <class T>
	using BSTSmartPointer = std::shared_ptr<T>;

	struct NiPoint3
	{
		float x{}, y{}, z{};
	};
	struct NiQuaternion
	{
		float w{ 1 }, x{}, y{}, z{};
	};
	struct Point2
	{
		float x{}, y{};
	};
	enum class CameraState : std::uint32_t
	{
		kFirstPerson = 0,
		kFree = 3,
		kVR = 9,
		kVRTotal = 14
	};
	struct PlayerCamera;
	struct TESCameraState
	{
		virtual ~TESCameraState() = default;
		virtual void  GetRotation(NiQuaternion& a_out) { a_out = sourceRotation; }
		virtual void  GetTranslation(NiPoint3& a_out) { a_out = sourcePosition; }
		PlayerCamera* camera{};
		CameraState   id{};
		NiQuaternion  sourceRotation{ 1, 0.2f, 0, 0.4f };
		NiPoint3      sourcePosition{ 100, 200, 300 };
	};
	struct FreeCameraState : TESCameraState
	{
		NiPoint3     translation{};
		Point2       rotation{}, zUpDown{};
		std::int16_t verticalDirection{};
		bool         useRunSpeed{};
	};
	struct PlayerCamera
	{
		struct States
		{
			std::array<BSTSmartPointer<TESCameraState>, 14> values;
			auto&                                           operator[](CameraState a_id) { return values[static_cast<unsigned>(a_id)]; }
			auto                                            begin() const { return values.begin(); }
			auto                                            end() const { return values.end(); }
		};
		struct RuntimeData
		{
			States cameraStates;
		} data;
		inline static PlayerCamera*     singleton{};
		static PlayerCamera*            GetSingleton() { return singleton; }
		RuntimeData*                    GetVRRuntimeData() { return &data; }
		BSTSmartPointer<TESCameraState> currentState;
		bool                            cameraRoot{ true };
		bool                            rejectTransition{};
		int                             transitions{};
		void                            SetState(TESCameraState* a_state)
		{
			++transitions;
			if (rejectTransition)
				return;
			for (const auto& state : data.cameraStates) {
				if (state.get() == a_state) {
					currentState = state;
					return;
				}
			}
			CHECK_MESSAGE(false, "attempted to restore an unregistered camera state");
		}
	};
	struct PlayerCharacter
	{
		inline static PlayerCharacter* singleton{};
		static PlayerCharacter*        GetSingleton() { return singleton; }
		bool                           loaded{ true };
		bool                           Get3D() const { return loaded; }
		bool                           GetParentCell() const { return loaded; }
	};
	struct PlayerControls
	{
		static PlayerControls* GetSingleton()
		{
			static PlayerControls controls;
			return &controls;
		}
	};
}

namespace camera_test_REL
{
	struct Module
	{
		static bool IsVR() { return true; }
	};
	struct VariantID
	{
		VariantID(int, int, std::uintptr_t a_rva) { CHECK(a_rva == 0x873B50); }
	};
	template <class T>
	struct Relocation
	{
		Relocation(VariantID) {}
		void operator()(camera_test_RE::FreeCameraState* a_state, const camera_test_RE::NiQuaternion* a_rotation) const
		{
			a_state->rotation = { a_rotation->x, a_rotation->z };
		}
	};
	template <class T>
	void RelocateVirtual(int a_flatSlot, int a_vrSlot, camera_test_RE::TESCameraState* a_state, camera_test_RE::NiQuaternion& a_out)
	{
		CHECK(a_flatSlot == 4 && a_vrSlot == 5);
		a_state->GetRotation(a_out);
	}
	template <class T>
	void RelocateVirtual(int a_flatSlot, int a_vrSlot, camera_test_RE::TESCameraState* a_state, camera_test_RE::NiPoint3& a_out)
	{
		CHECK(a_flatSlot == 5 && a_vrSlot == 6);
		a_state->GetTranslation(a_out);
	}
}

#define RE camera_test_RE
#define REL camera_test_REL
#include "../src/VRFreeCamera.cpp"
#undef REL
#undef RE

namespace
{
	namespace Camera = dvb::VRFreeCamera;
	namespace Model = camera_test_RE;

	struct Scene
	{
		Model::PlayerCamera                     camera;
		Model::PlayerCharacter                  player;
		std::shared_ptr<Model::TESCameraState>  prior = std::make_shared<Model::TESCameraState>();
		std::shared_ptr<Model::TESCameraState>  other = std::make_shared<Model::TESCameraState>();
		std::shared_ptr<Model::FreeCameraState> free = std::make_shared<Model::FreeCameraState>();
		Scene()
		{
			Camera::EndLoad();
			Model::PlayerCamera::singleton = &camera;
			Model::PlayerCharacter::singleton = &player;
			prior->camera = other->camera = free->camera = &camera;
			prior->id = Model::CameraState::kVR;
			other->id = Model::CameraState::kFirstPerson;
			free->id = Model::CameraState::kFree;
			camera.data.cameraStates[prior->id] = prior;
			camera.data.cameraStates[other->id] = other;
			camera.data.cameraStates[free->id] = free;
			camera.currentState = prior;
		}
		~Scene()
		{
			Camera::EndLoad();
			Model::PlayerCamera::singleton = nullptr;
			Model::PlayerCharacter::singleton = nullptr;
		}
		void Enable(bool a_on = true) { Camera::SetEnabled(a_on, Camera::CurrentSession()); }
		void Drive() { Camera::Drive(10, 20, 30, 0.3f, 0.5f, Camera::CurrentSession()); }
	};

	template <class F>
	void ExpectError(int a_code, F a_fn)
	{
		try {
			a_fn();
			CHECK_MESSAGE(false, "expected ToolError");
		} catch (const dvb::ToolError& error) {
			CHECK(error.code == a_code);
		}
	}
}

TEST_CASE("VR camera repeated owned round trips retain the prior state and clear input latches")
{
	Scene scene;
	for (int cycle = 0; cycle < 3; ++cycle) {
		scene.free->zUpDown = { 1, -1 };
		scene.free->verticalDirection = 1;
		scene.free->useRunSpeed = true;
		scene.Enable();
		CHECK(Camera::IsOwned());
		CHECK(scene.free->translation.x == scene.prior->sourcePosition.x);
		CHECK(scene.free->translation.y == scene.prior->sourcePosition.y);
		CHECK(scene.free->translation.z == scene.prior->sourcePosition.z);
		CHECK(scene.free->rotation.x == scene.prior->sourceRotation.x);
		CHECK(scene.free->rotation.y == scene.prior->sourceRotation.z);
		CHECK(scene.free->zUpDown.x == 0 && scene.free->zUpDown.y == 0);
		CHECK(scene.free->verticalDirection == 0 && !scene.free->useRunSpeed);
		const auto entries = scene.camera.transitions;
		scene.Enable();
		CHECK(scene.camera.transitions == entries);
		scene.Drive();
		CHECK(scene.free->translation.x == 10 && scene.free->translation.y == 20 && scene.free->translation.z == 30);
		CHECK(scene.free->rotation.x == 0.3f && scene.free->rotation.y == 0.5f);
		scene.Enable(false);
		CHECK(scene.camera.currentState == scene.prior);
		CHECK(!Camera::IsOwned());
		scene.Enable(false);
		CHECK(scene.camera.transitions == entries + 1);
	}
}

TEST_CASE("VR camera rejects every mutation of an externally activated free camera")
{
	Scene scene;
	scene.camera.currentState = scene.free;
	scene.free->translation = { 90, 80, 70 };
	ExpectError(409, [&] { scene.Enable(); });
	ExpectError(409, [&] { scene.Enable(false); });
	ExpectError(409, [&] { scene.Drive(); });
	CHECK(scene.camera.transitions == 0);
	CHECK(scene.free->translation.x == 90);
	CHECK(!Camera::IsOwned());
}

TEST_CASE("VR camera observation releases ownership after an external state change")
{
	Scene scene;
	scene.Enable();
	scene.camera.currentState = scene.other;
	CHECK(!Camera::IsOwned());
	CHECK(scene.prior.use_count() == 2);  // fixture + registry; controller released it
	scene.camera.currentState = scene.free;
	ExpectError(409, [&] { scene.Enable(false); });
	scene.camera.currentState = scene.other;
	scene.Enable();
	scene.Enable(false);
	CHECK(scene.camera.currentState == scene.other);
}

TEST_CASE("VR camera refuses replaced free and return states")
{
	Scene scene;
	scene.Enable();
	scene.camera.data.cameraStates[scene.prior->id] = scene.other;
	ExpectError(409, [&] { scene.Enable(false); });
	CHECK(scene.camera.currentState == scene.free);
	CHECK(!Camera::IsOwned());
	scene.camera.currentState = scene.other;
	scene.Enable();
	auto replacement = std::make_shared<Model::FreeCameraState>();
	replacement->camera = &scene.camera;
	replacement->id = Model::CameraState::kFree;
	scene.camera.data.cameraStates[Model::CameraState::kFree] = replacement;
	ExpectError(409, [&] { scene.Drive(); });
	CHECK(!Camera::IsOwned());
	CHECK(replacement->translation.x == 0);
}

TEST_CASE("VR camera invalidates mutations at both save-load boundaries")
{
	Scene      scene;
	const auto oldSession = Camera::CurrentSession();
	scene.Enable();
	Camera::BeginLoad();
	CHECK(scene.camera.currentState == scene.prior);
	CHECK(!Camera::IsOwned());
	const auto loadingSession = Camera::CurrentSession();
	ExpectError(409, [&] { Camera::SetEnabled(true, oldSession); });
	ExpectError(409, [&] { Camera::SetEnabled(true, loadingSession); });
	ExpectError(409, [&] { scene.Drive(); });
	Camera::EndLoad();
	ExpectError(409, [&] { Camera::SetEnabled(true, oldSession); });
	ExpectError(409, [&] { Camera::SetEnabled(true, loadingSession); });
	scene.Enable();
	ExpectError(409, [&] { Camera::Drive(999, 999, 999, 1, 1, oldSession); });
	CHECK(scene.free->translation.x == scene.prior->sourcePosition.x);
	// NewGame uses EndLoad without a preceding PreLoad notification.
	const auto beforeNewGame = Camera::CurrentSession();
	scene.camera.currentState = scene.other;
	Camera::EndLoad();
	ExpectError(409, [&] { Camera::SetEnabled(true, beforeNewGame); });
	CHECK(!Camera::IsOwned());
}

TEST_CASE("VR camera transition failures do not claim success or restore across a load")
{
	Scene scene;
	scene.camera.rejectTransition = true;
	ExpectError(500, [&] { scene.Enable(); });
	CHECK(!Camera::IsOwned());
	scene.camera.rejectTransition = false;
	scene.Enable();
	scene.camera.rejectTransition = true;
	ExpectError(500, [&] { scene.Enable(false); });
	CHECK(Camera::IsOwned());  // retry still has the valid return state
	Camera::BeginLoad();
	CHECK(!Camera::IsOwned());
	Camera::EndLoad();
	ExpectError(409, [&] { scene.Enable(false); });
}

TEST_CASE("VR camera requires a loaded player and a registered source state")
{
	Scene scene;
	scene.player.loaded = false;
	ExpectError(422, [&] { scene.Enable(); });
	scene.player.loaded = true;
	scene.camera.data.cameraStates[scene.prior->id].reset();
	ExpectError(422, [&] { scene.Enable(); });
	CHECK(scene.camera.transitions == 0);
	CHECK(!Camera::IsOwned());
}
