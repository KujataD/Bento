#include "UiSoundPlayer.h"

#include "GameAudio.h"

#include <runtime/UIInput.h>

using namespace KujataEngine;

namespace {

/// <summary>決定の入力。**UINavigationSystemと同じ組み合わせ**にしておくこと
/// (ここだけ増やすと、音は鳴るのに決定されないボタンができる)。</summary>
bool IsSubmitTriggered() {
	return Input::GetControllerButtonTrigger(XINPUT_GAMEPAD_A) || Input::GetKeyTrigger(DIK_RETURN) || Input::GetKeyTrigger(DIK_SPACE);
}

bool IsCancelTriggered() { return Input::GetControllerButtonTrigger(XINPUT_GAMEPAD_B) || Input::GetKeyTrigger(DIK_ESCAPE); }

} // namespace

void UiSoundPlayer::OnPlayStart() {
	lastSelected_ = nullptr;
	primed_ = false;
}

void UiSoundPlayer::Update() {
	GameObject* selected = GetUISelected();

	// 最初の1回は「初期選択が入った」だけなので鳴らさない。
	// ここで鳴らすと、シーンを開いた瞬間に毎回カーソル音が鳴ってしまう。
	if (!primed_) {
		lastSelected_ = selected;
		primed_ = true;
		return;
	}

	if (playMove_ && selected && selected != lastSelected_) {
		GameAudio::PlaySe(GameAudio::Se::UiMove);
	}
	lastSelected_ = selected;

	if (!playDecide_) {
		return;
	}
	// **選択中のボタンが無いときは鳴らさない。** 戦闘中のAボタン(回避)で決定音が鳴ってしまう。
	if (selected && IsSubmitTriggered()) {
		GameAudio::PlaySe(GameAudio::Se::UiDecide);
	} else if (selected && IsCancelTriggered()) {
		GameAudio::PlaySe(GameAudio::Se::UiCancel);
	}
}
