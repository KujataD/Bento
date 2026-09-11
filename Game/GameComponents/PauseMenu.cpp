#include "PauseMenu.h"

#include "GameAudio.h"
#include "GameFlowManager.h"
#include "BossIntroCutscene.h"
#include "SettingsMenu.h"
#include "TutorialManager.h"

using namespace KujataEngine;

namespace {

/// <summary>ポーズの開閉入力。パッドはSTART、キーボードはEsc。</summary>
bool IsPauseTriggered() { return Input::GetControllerButtonTrigger(XINPUT_GAMEPAD_START) || Input::GetKeyTrigger(DIK_ESCAPE); }

} // namespace

void PauseMenu::OnPlayStart() {
	paused_ = false;
	settingsOpen_ = false;
	controlsOpen_ = false;
	SetObjectActive(pauseMenuName_, false);
	SetObjectActive(settingsMenuName_, false);
	SetObjectActive(controlsMenuName_, false);
}

void PauseMenu::OnPlayStop() {
	// **止めたまま終わらない。** 戻さないと次のPlayが停止状態で始まる。
	if (paused_) {
		Time::SetTimeScale(1.0f);
	}
	paused_ = false;
	settingsOpen_ = false;
	controlsOpen_ = false;
}

PauseMenu* PauseMenu::FindInScene(Scene* scene) {
	if (!scene) {
		return nullptr;
	}
	for (const std::unique_ptr<GameObject>& object : scene->GetGameObjects()) {
		if (!object) {
			continue;
		}
		if (PauseMenu* menu = object->GetComponent<PauseMenu>()) {
			return menu;
		}
	}
	return nullptr;
}

bool PauseMenu::IsScenePaused(Scene* scene) {
	PauseMenu* menu = FindInScene(scene);
	return menu && menu->IsPaused();
}

void PauseMenu::RegisterInvokableMethods(InvokableMethodRegistry& registry) {
	registry.Add("Resume", [this]() { Resume(); });
	registry.Add("OpenSettings", [this]() { OpenSettings(); });
	registry.Add("CloseSettings", [this]() { CloseSettings(); });
	registry.Add("OpenControls", [this]() { OpenControls(); });
	registry.Add("CloseControls", [this]() { CloseControls(); });
	registry.Add("ReturnToTitle", [this]() { ReturnToTitle(); });
}

void PauseMenu::Update() {
	GameFlowManager* flow = GameFlowManager::FindInScene(owner_ ? owner_->GetScene() : nullptr);

	// 勝敗が決まったら、開いていても畳んでポーズを禁止する。
	// 死亡演出やリトライメニューと重なると、どちらの操作中か分からなくなる。
	if (flow && !flow->IsInGameplay()) {
		if (paused_) {
			SetPaused(false);
		}
		return;
	}

	// **チュートリアルの説明を読んでいる間はポーズさせない。**
	// どちらも `Time::SetTimeScale(0)` で止める作りなので、重ねると解除の順番が噛み合わず、
	// 説明を出したままゲームが動き出す(ポーズを解いた側が1.0へ戻してしまう)。
	if (TutorialManager* tutorial = TutorialManager::FindInScene(owner_ ? owner_->GetScene() : nullptr)) {
		if (tutorial->IsShowing()) {
			if (paused_) {
				SetPaused(false);
			}
			return;
		}
	}

	// **開幕演出の最中はポーズさせない。** カメラを預かっている最中に時間を止めると、
	// 演出が実時間で進んだまま画だけが固まり、明けた先で辻褄が合わなくなる。
	// (演出はSTARTでも飛ばせるので、押した操作が無視されるわけではない)
	if (BossIntroCutscene::IsSceneIntroPlaying(owner_ ? owner_->GetScene() : nullptr)) {
		if (paused_) {
			SetPaused(false);
		}
		return;
	}

	if (paused_) {
		// **毎フレーム入れ直す。** 他のComponent(ヒットストップ等)がtimeScaleを戻す経路がある。
		Time::SetTimeScale(0.0f);
	}

	if (!IsPauseTriggered()) {
		return;
	}

	if (!paused_) {
		SetPaused(true);
		return;
	}
	// 重ねて開いている画面があるなら、まずそれを閉じる(いきなりゲームへ戻さない)。
	if (settingsOpen_) {
		CloseSettings();
		return;
	}
	if (controlsOpen_) {
		CloseControls();
		return;
	}
	if (allowToggleClose_) {
		Resume();
	}
}

void PauseMenu::SetPaused(bool paused) {
	paused_ = paused;
	Time::SetTimeScale(paused ? 0.0f : 1.0f);
	SetObjectActive(pauseMenuName_, paused);
	if (!paused) {
		settingsOpen_ = false;
		controlsOpen_ = false;
		SetObjectActive(settingsMenuName_, false);
		SetObjectActive(controlsMenuName_, false);
	}
	GameAudio::PlaySe(paused ? GameAudio::Se::UiDecide : GameAudio::Se::UiCancel);
}

void PauseMenu::Resume() {
	if (!paused_) {
		return;
	}
	SetPaused(false);
}

void PauseMenu::OpenSettings() {
	// 開閉の実務(どちらを伏せてどちらを出すか)は設定画面側が持っている。
	// ここで二重に面倒を見ると、タイトルから開いたときと挙動がずれる。
	SettingsMenu* settings = SettingsMenu::FindInScene(owner_ ? owner_->GetScene() : nullptr);
	if (!settings) {
		return;
	}
	settingsOpen_ = true;
	settings->Open();
}

void PauseMenu::CloseSettings() {
	settingsOpen_ = false;
	if (SettingsMenu* settings = SettingsMenu::FindInScene(owner_ ? owner_->GetScene() : nullptr)) {
		settings->Close();
	}
}

void PauseMenu::OpenControls() {
	if (controlsMenuName_.empty()) {
		return;
	}
	// **ポーズメニューは伏せる。** 出しっぱなしだと裏のボタンが選択表示のまま残り、
	// パッドのフォーカスがどちらにあるのか分からなくなる(設定画面と同じ約束)。
	controlsOpen_ = true;
	SetObjectActive(pauseMenuName_, false);
	SetObjectActive(controlsMenuName_, true);
	GameAudio::PlaySe(GameAudio::Se::UiDecide);
}

void PauseMenu::CloseControls() {
	controlsOpen_ = false;
	SetObjectActive(controlsMenuName_, false);
	SetObjectActive(pauseMenuName_, paused_);
	GameAudio::PlaySe(GameAudio::Se::UiCancel);
}

void PauseMenu::ReturnToTitle() {
	// 進行の後始末(timeScale・GameSession・暗転)はGameFlowManagerが持っているので任せる。
	Time::SetTimeScale(1.0f);
	paused_ = false;
	if (GameFlowManager* flow = GameFlowManager::FindInScene(owner_ ? owner_->GetScene() : nullptr)) {
		flow->ReturnToTitle();
	}
}

GameObject* PauseMenu::SetObjectActive(const std::string& name, bool active) {
	Scene* scene = owner_ ? owner_->GetScene() : nullptr;
	if (!scene || name.empty()) {
		return nullptr;
	}
	GameObject* object = scene->FindGameObjectByName(name);
	if (object) {
		object->SetActive(active);
	}
	return object;
}
