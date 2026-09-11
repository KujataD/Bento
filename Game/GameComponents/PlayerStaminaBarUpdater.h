#pragma once

#include <KujataEngine.h>
#include <string>

namespace KujataEngine {
class ImageComponent;
}

/// <summary>
/// 画面のPlayer用スタミナバー。自分のImageComponentのfillAmountを
/// 「現在のリーダー(PartyManagerが指す操作キャラ)」のStaminaComponentの残量率で毎フレーム更新する。
/// 構成はPlayerHPBarUpdaterと同じ(背景Imageの子にFill Imageを置き、FillにこのComponentを付ける)。
/// </summary>
class PlayerStaminaBarUpdater : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "PlayerStaminaBarUpdater"; }

	void Update() override;

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_STRING_NAMED_TIP(playerName_, "Player Name",
		    "PartyManagerがシーンに無いときだけ使う保険の名前。あるときは現在のリーダーを自動で追う。");
	}

	// PartyManager不在時のフォールバック名。
	KUJATA_FIELD_STRING(playerName_, "Pawn");

	KujataEngine::ImageComponent* fillImage_ = nullptr;
};
