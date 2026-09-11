#include "ChangeSceneManager.h"

#include "ScreenFader.h"

using namespace KujataEngine;

void ChangeSceneManager::RegisterInvokableMethods(KujataEngine::InvokableMethodRegistry& registry) {
	registry.Add("OnClick", [this]() { OnClick(); });
	// タイトルの「終了」ボタン用。エンジンが次のUpdateで主ループを畳む。
	registry.Add("QuitGame", []() { KujataEngine::RequestQuitApplication(); });
}

void ChangeSceneManager::OnClick() {
	// 直接ChangeSceneせず、暗転しきってから切り替える(明るい画のまま固まるのを避ける)。
	ScreenFader::RequestTransition(owner_ ? owner_->GetScene() : nullptr, sceneName_, viaLoading_);
}
