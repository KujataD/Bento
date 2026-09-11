#include "KujataEngine.h"
#include "scene/SampleScene.h"
#include <memory>

namespace {

constexpr const char* kGameModuleName = "GameModule";

// 起動時に作るシーン。エディタ用カメラやScene/Game 2画面の描画はSampleSceneが持つ。
class GameModuleScene : public KujataEngine::SampleScene {
public:
	~GameModuleScene() override = default;
};

// ---- サンプルComponent(新しいゲームを作ったら消してよい) ----

class MoveForwardComponent : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "MoveForwardComponent"; }

	void Update() override {
		KujataEngine::GameObject* owner = GetOwner();
		if (!owner) {
			return;
		}

		owner->GetTransform().translation_.z += speed_;
	}

	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_FLOAT(speed_, 0.001f, 0.0f, 0.0f);
	}

private:
	KUJATA_FIELD_FLOAT(speed_, 0.03f);
};

class BlinkComponent : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "BlinkComponent"; }

	void Initialize() override {
		SaveOriginalScale();
	}

	void Update() override {
		KujataEngine::GameObject* owner = GetOwner();
		if (!owner) {
			return;
		}

		++frameCount_;
		if (frameCount_ < intervalFrame_) {
			return;
		}

		frameCount_ = 0;
		visible_ = !visible_;

		KujataEngine::WorldTransform& transform = owner->GetTransform();
		if (visible_) {
			transform.scale_ = originalScale_;
		} else {
			transform.scale_ = originalScale_ * 0.25f;
		}
	}

	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_INT(intervalFrame_, 1.0f, 1, 600);
	}

public:
	void OnAfterReadJson() override {
		SaveOriginalScale();
	}

private:
	void SaveOriginalScale() {
		KujataEngine::GameObject* owner = GetOwner();
		if (!owner) {
			return;
		}

		originalScale_ = owner->GetTransform().scale_;
	}

private:
	KUJATA_FIELD_INT(intervalFrame_, 30);
	int frameCount_ = 0;
	bool visible_ = true;
	KujataEngine::Vector3 originalScale_ = {1.0f, 1.0f, 1.0f};
};

} // namespace

extern "C" __declspec(dllexport) void RegisterGameComponents(KujataEngine::ComponentFactory& factory) {
	// GameModuleはゲーム固有Componentだけを登録する。
	// TransformやModelRendererなどの標準ComponentはEngine初期化時に登録される。
	// Game/GameComponents/ に書いたComponentもここで登録する。
	factory.RegisterComponent<MoveForwardComponent>(kGameModuleName);
	factory.RegisterComponent<BlinkComponent>(kGameModuleName);
}

extern "C" __declspec(dllexport) void UnregisterGameComponents(KujataEngine::ComponentFactory& factory) {
	factory.UnregisterByModule(kGameModuleName);
}

extern "C" __declspec(dllexport) KujataEngine::Scene* CreateGameScene() {
	return new GameModuleScene();
}

extern "C" __declspec(dllexport) void DestroyGameScene(KujataEngine::Scene* scene) {
	delete scene;
}
