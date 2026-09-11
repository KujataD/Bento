#include "SettingsMenu.h"

#include "GameAudio.h"
#include "GameSettings.h"

#include <components/OrbitCameraComponent.h>
#include <components/TextComponent.h>
#include <runtime/UIInput.h>

#include <cmath>
#include <cstdio>

using namespace KujataEngine;

namespace {

constexpr float kStickThreshold = 0.5f;

/// <summary>左右入力を-1/0/+1で返す。スティックと十字キーとキーボードを同じ扱いにする。</summary>
float ReadHorizontal() {
	const Vector2 stick = Input::GetLeftStick();
	if (stick.x <= -kStickThreshold || Input::GetControllerButton(XINPUT_GAMEPAD_DPAD_LEFT) || Input::GetKey(DIK_LEFT) || Input::GetKey(DIK_A)) {
		return -1.0f;
	}
	if (stick.x >= kStickThreshold || Input::GetControllerButton(XINPUT_GAMEPAD_DPAD_RIGHT) || Input::GetKey(DIK_RIGHT) || Input::GetKey(DIK_D)) {
		return 1.0f;
	}
	return 0.0f;
}

} // namespace

void SettingsMenu::RegisterInvokableMethods(InvokableMethodRegistry& registry) {
	registry.Add("Open", [this]() { Open(); });
	registry.Add("Close", [this]() { Close(); });
}

SettingsMenu* SettingsMenu::FindInScene(Scene* scene) {
	if (!scene) {
		return nullptr;
	}
	for (const std::unique_ptr<GameObject>& object : scene->GetGameObjects()) {
		if (!object) {
			continue;
		}
		if (SettingsMenu* menu = object->GetComponent<SettingsMenu>()) {
			return menu;
		}
	}
	return nullptr;
}

void SettingsMenu::Open() {
	open_ = true;
	SetObjectActive(returnMenuName_, false);
	SetObjectActive(menuObjectName_, true);
	ApplyToEngine();
	RefreshTexts();
}

void SettingsMenu::Close() {
	open_ = false;
	SetObjectActive(menuObjectName_, false);
	SetObjectActive(returnMenuName_, true);
	// **閉じたところで書き出す。** 触るたびに書くとディスクを叩きすぎるので、
	// 出入り口の1か所だけで保存する。失敗しても遊べなくはならない([[GameSettings]])。
	GameSettings::Save();
}

GameObject* SettingsMenu::SetObjectActive(const std::string& name, bool active) {
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

void SettingsMenu::OnPlayStart() {
	open_ = false;
	// **開いた状態で保存されていても必ず伏せてから始める。**
	SetObjectActive(menuObjectName_, false);
	heldDirection_ = 0.0f;
	heldSeconds_ = 0.0f;
	repeatTimer_ = 0.0f;
	// カメラ設定はエンジン側のstaticに置いてあり、既定値のまま始まることがある。
	// 開いた時点で現在の設定を押し込んでおけば、値と実挙動が必ず一致する。
	ApplyToEngine();
	RefreshTexts();
}

void SettingsMenu::ApplyToEngine() {
	OrbitCameraComponent::SetUserSensitivityScale(GameSettings::CameraSensitivityRef());
	OrbitCameraComponent::SetUserInvertY(GameSettings::InvertCameraYRef());
}

void SettingsMenu::Update() {
	// **常に書き直す。** 開いた瞬間や、別の場所(タイトル/ポーズ)で変えた値も必ず合う。
	RefreshTexts();

	const Row focused = FindFocusedRow();
	if (focused == Row::Count) {
		heldDirection_ = 0.0f;
		return;
	}

	const float direction = ReadHorizontal();
	if (direction == 0.0f) {
		heldDirection_ = 0.0f;
		heldSeconds_ = 0.0f;
		repeatTimer_ = 0.0f;
		return;
	}

	// 設定画面はtimeScale=0のポーズ中にも開くので、必ず実時間で数える。
	const float deltaTime = Time::GetUnscaledDeltaTime();

	if (direction != heldDirection_) {
		// 押した瞬間は必ず1回効かせる(押し直しで微調整できるように)。
		heldDirection_ = direction;
		heldSeconds_ = 0.0f;
		repeatTimer_ = 0.0f;
		Adjust(focused, direction);
		return;
	}

	// 押しっぱなし: Repeat Delayを過ぎてからRepeat Intervalごとに効かせる。
	heldSeconds_ += deltaTime;
	if (heldSeconds_ < repeatDelay_) {
		return;
	}
	repeatTimer_ += deltaTime;
	if (repeatTimer_ < repeatInterval_) {
		return;
	}
	repeatTimer_ = 0.0f;
	Adjust(focused, direction);
}

SettingsMenu::Row SettingsMenu::FindFocusedRow() const {
	GameObject* selected = GetUISelected();
	if (!selected) {
		return Row::Count;
	}
	const std::string& name = selected->GetName();
	if (name == bgmRowName_) {
		return Row::BgmVolume;
	}
	if (name == seRowName_) {
		return Row::SeVolume;
	}
	if (name == sensitivityRowName_) {
		return Row::Sensitivity;
	}
	if (name == invertRowName_) {
		return Row::InvertY;
	}
	return Row::Count;
}

void SettingsMenu::Adjust(Row row, float direction) {
	switch (row) {
	case Row::BgmVolume:
		GameSettings::BgmVolumeRef() += volumeStep_ * direction;
		break;
	case Row::SeVolume:
		GameSettings::SeVolumeRef() += volumeStep_ * direction;
		break;
	case Row::Sensitivity:
		GameSettings::CameraSensitivityRef() += sensitivityStep_ * direction;
		break;
	case Row::InvertY:
		// トグルなので方向は見ない(左右どちらでも切り替わる)。
		GameSettings::InvertCameraYRef() = !GameSettings::InvertCameraYRef();
		break;
	default:
		return;
	}
	GameSettings::Clamp();
	ApplyToEngine();

	// BGMは鳴りっぱなしなので、再生中のボイスへ即座に反映する。
	GameAudio::RefreshVolumes();
	// **効果音の音量を変えたら鳴らして聞かせる。** 数字だけ動いても大きさは分からない。
	if (row == Row::SeVolume || row == Row::InvertY) {
		GameAudio::PlaySe(GameAudio::Se::UiMove);
	}
}

void SettingsMenu::RefreshTexts() {
	SetText(bgmValueName_, FormatPercent(GameSettings::BgmVolumeRef()));
	SetText(seValueName_, FormatPercent(GameSettings::SeVolumeRef()));

	char buffer[32]{};
	std::snprintf(buffer, sizeof(buffer), "%.1f", GameSettings::CameraSensitivityRef());
	SetText(sensitivityValueName_, buffer);

	SetText(invertValueName_, GameSettings::InvertCameraYRef() ? "ON" : "OFF");
}

void SettingsMenu::SetText(const std::string& objectName, const std::string& text) {
	Scene* scene = owner_ ? owner_->GetScene() : nullptr;
	if (!scene || objectName.empty()) {
		return;
	}
	GameObject* object = scene->FindGameObjectByName(objectName);
	if (!object) {
		return;
	}
	if (TextComponent* label = object->GetComponent<TextComponent>()) {
		label->SetText(text);
	}
}

std::string SettingsMenu::FormatPercent(float value01) {
	char buffer[32]{};
	std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(std::lround(value01 * 100.0f)));
	return buffer;
}
