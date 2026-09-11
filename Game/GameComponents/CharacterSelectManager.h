#pragma once
#include <KujataEngine.h>

/// <summary>
/// キャラ選択シーンの進行役。各キャラの「選択」ボタン(Button.onClick)から
/// SelectPawn / SelectBishop を呼ばれると、選ばれた方をPartySelectionへ記録して
/// 次シーン(SampleScene)へ遷移する。もう片方はPartyManagerが自動的に味方NPCにする。
/// </summary>
class CharacterSelectManager : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "CharacterSelectManager"; }
	bool AllowMultiple() const override { return false; }

	void RegisterInvokableMethods(KujataEngine::InvokableMethodRegistry& registry) override;

private:
	/// <summary>leaderNameを操作キャラとして記録し、次シーンへ遷移する。</summary>
	void Select(const std::string& leaderName);

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_STRING_NAMED(pawnName_, "Pawn Name");
		KUJATA_REGISTER_STRING_NAMED(bishopName_, "Bishop Name");
		KUJATA_REGISTER_STRING_NAMED(nextSceneName_, "Next Scene");
	}

	// 次シーンでのキャラのGameObject名(PartyManagerのLeader/Ally Nameと一致させる)。
	KUJATA_FIELD_STRING(pawnName_, "Pawn");
	KUJATA_FIELD_STRING(bishopName_, "Bishop");
	// 選択後に遷移するシーン名。
	KUJATA_FIELD_STRING(nextSceneName_, "SampleScene");
};
