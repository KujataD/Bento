#include "CharacterSelectManager.h"
#include "GameSession.h"
#include "PartySelection.h"
#include "ScreenFader.h"

using namespace KujataEngine;

void CharacterSelectManager::RegisterInvokableMethods(KujataEngine::InvokableMethodRegistry& registry) {
	registry.Add("SelectPawn", [this]() { Select(pawnName_); });
	registry.Add("SelectBishop", [this]() { Select(bishopName_); });
}

void CharacterSelectManager::Select(const std::string& leaderName) {
	PartySelection::SetLeaderName(leaderName);
	// **シーンを跨いでも保つ方にも書く。** PartySelectionは1回で消費されるので、
	// チュートリアル→ボス戦やリトライまで選択を持ち越すにはこちらが要る。
	GameSession::LeaderNameRef() = leaderName;
	// 本編シーンは重いので必ずローディングを挟む(暗転中に読ませてカクつきを隠す)。
	ScreenFader::RequestTransition(owner_ ? owner_->GetScene() : nullptr, nextSceneName_, true);
}
