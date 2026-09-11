#include "KujataEngine.h"
#include "scene/SampleScene.h"
#include "../GameComponents/ActionFeedback.h"
#include "../GameComponents/AllyAIBrain.h"
#include "../GameComponents/BossIntroCutscene.h"
#include "../GameComponents/CharacterMotor.h"
#include "../GameComponents/CharacterSelectManager.h"
#include "../GameComponents/EnemyComponent.h"
#include "../GameComponents/MagicAbilitySet.h"
#include "../GameComponents/MagicProjectile.h"
#include "../GameComponents/MeleeAbilitySet.h"
#include "../GameComponents/PartyManager.h"
#include "../GameComponents/Player.h"
#include "../GameComponents/PlayerAnimator.h"
#include "../GameComponents/PlayerMoveComponent.h"
#include "../GameComponents/WeaponComponent.h"
#include "../GameComponents/EnemyHealth.h"
#include "../GameComponents/PlayerHealth.h"
#include "../GameComponents/EnemyWeapon.h"
#include "../GameComponents/HPBarUpdater.h"
#include "../GameComponents/HateTable.h"
#include "../GameComponents/GruntEnemyComponent.h"
#include "../GameComponents/ChangeSceneManager.h"
#include "../GameComponents/GameFlowManager.h"
#include "../GameComponents/LoadingScreen.h"
#include "../GameComponents/BgmPlayer.h"
#include "../GameComponents/UiSoundPlayer.h"
#include "../GameComponents/PauseMenu.h"
#include "../GameComponents/SettingsMenu.h"
#include "../GameComponents/FogGate.h"
#include "../GameComponents/MistBackdrop.h"
#include "../GameComponents/TorchFlicker.h"
#include "../GameComponents/TutorialManager.h"
#include "../GameComponents/TutorialTrigger.h"
#include "../GameComponents/ScreenFader.h"
#include "../GameComponents/HammerEnemyComponent.h"
#include "../GameComponents/AllyBarUpdater.h"
#include "../GameComponents/PlayerHPBarUpdater.h"
#include "../GameComponents/PlayerStaminaBarUpdater.h"
#include "../GameComponents/BossHPBarUpdater.h"
#include "../GameComponents/ReviveGaugeUpdater.h"
#include "../GameComponents/EnemyPart.h"
#include "../GameComponents/PrefabSpawner.h"
#include "../GameComponents/Phase2Cutscene.h"
#include "../GameComponents/LockOnController.h"
#include "../GameComponents/SwordGuard.h"
#include "../GameComponents/BarrierGuard.h"
#include "../GameComponents/CriticalStrikeComponent.h"
#include "../GameComponents/StaminaComponent.h"
#include "../GameComponents/GuardianBody.h"
#include "../GameComponents/GuardianGait.h"
#include "../GameComponents/GuardianSplineRig.h"
#include "../GameComponents/GuardianBossComponent.h"
#include <memory>

namespace {

constexpr const char* kGameModuleName = "GameModule";

class GameModuleScene : public KujataEngine::SampleScene {
public:
	~GameModuleScene() override = default;
};

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
	factory.RegisterComponent<MoveForwardComponent>(kGameModuleName);
	factory.RegisterComponent<BlinkComponent>(kGameModuleName);
	factory.RegisterComponent<EnemyComponent>(kGameModuleName);
	factory.RegisterComponent<CharacterMotor>(kGameModuleName);
	factory.RegisterComponent<Player>(kGameModuleName);
	factory.RegisterComponent<AllyAIBrain>(kGameModuleName);
	factory.RegisterComponent<PartyManager>(kGameModuleName);
	factory.RegisterComponent<CharacterSelectManager>(kGameModuleName);
	factory.RegisterComponent<MeleeAbilitySet>(kGameModuleName);
	factory.RegisterComponent<MagicAbilitySet>(kGameModuleName);
	factory.RegisterComponent<MagicProjectile>(kGameModuleName);
	factory.RegisterComponent<PlayerAnimator>(kGameModuleName);
	factory.RegisterComponent<PlayerMoveComponent>(kGameModuleName);
	factory.RegisterComponent<WeaponComponent>(kGameModuleName);
	factory.RegisterComponent<EnemyHealth>(kGameModuleName);
	factory.RegisterComponent<PlayerHealth>(kGameModuleName);
	factory.RegisterComponent<HPBarUpdater>(kGameModuleName);
	factory.RegisterComponent<EnemyWeapon>(kGameModuleName);
	factory.RegisterComponent<ChangeSceneManager>(kGameModuleName);

	// ゲーム進行(タイトル→ゲーム→リトライ/クリア)まわり。
	factory.RegisterComponent<ScreenFader>(kGameModuleName);
	factory.RegisterComponent<LoadingScreen>(kGameModuleName);
	factory.RegisterComponent<GameFlowManager>(kGameModuleName);
	factory.RegisterComponent<BossIntroCutscene>(kGameModuleName);
	factory.RegisterComponent<MistBackdrop>(kGameModuleName);
	factory.RegisterComponent<FogGate>(kGameModuleName);
	factory.RegisterComponent<BgmPlayer>(kGameModuleName);
	factory.RegisterComponent<UiSoundPlayer>(kGameModuleName);
	factory.RegisterComponent<PauseMenu>(kGameModuleName);
	factory.RegisterComponent<SettingsMenu>(kGameModuleName);
	factory.RegisterComponent<TorchFlicker>(kGameModuleName);
	factory.RegisterComponent<ActionFeedback>(kGameModuleName);
	factory.RegisterComponent<TutorialManager>(kGameModuleName);
	factory.RegisterComponent<TutorialTrigger>(kGameModuleName);
	factory.RegisterComponent<HammerEnemyComponent>(kGameModuleName);
	factory.RegisterComponent<AllyBarUpdater>(kGameModuleName);
	factory.RegisterComponent<PlayerHPBarUpdater>(kGameModuleName);
	factory.RegisterComponent<PlayerStaminaBarUpdater>(kGameModuleName);
	factory.RegisterComponent<BossHPBarUpdater>(kGameModuleName);
	factory.RegisterComponent<ReviveGaugeUpdater>(kGameModuleName);
	factory.RegisterComponent<EnemyPart>(kGameModuleName);
	factory.RegisterComponent<PrefabSpawner>(kGameModuleName);
	factory.RegisterComponent<Phase2Cutscene>(kGameModuleName);
	factory.RegisterComponent<StaminaComponent>(kGameModuleName);
	factory.RegisterComponent<HateTable>(kGameModuleName);
	factory.RegisterComponent<GruntEnemyComponent>(kGameModuleName);
	factory.RegisterComponent<LockOnController>(kGameModuleName);
	factory.RegisterComponent<SwordGuard>(kGameModuleName);
	factory.RegisterComponent<BarrierGuard>(kGameModuleName);
	factory.RegisterComponent<CriticalStrikeComponent>(kGameModuleName);

	// ガーディアン風ボス。GameObjectへ追加する順は Gait → Body → LegRig にすること
	// (Component::Updateは追加順に走るため、足先目標の決定 → 胴体の配置 → IK解決 の順になる)。
	factory.RegisterComponent<GuardianGait>(kGameModuleName);
	factory.RegisterComponent<GuardianBody>(kGameModuleName);
	factory.RegisterComponent<GuardianSplineRig>(kGameModuleName);
	factory.RegisterComponent<GuardianBossComponent>(kGameModuleName);
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
