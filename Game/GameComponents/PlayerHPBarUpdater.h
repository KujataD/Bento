#pragma once

#include <KujataEngine.h>
#include <string>

namespace KujataEngine {
class ImageComponent;
}

/// <summary>
/// 画面のPlayer用HPバー。自分(PlayerHPBarFill)のImageComponentのfillAmountを
/// 「現在のリーダー(PartyManagerが指す操作キャラ)」のHP率で毎フレーム更新する。
/// キャラ切替で操作キャラが変わればバーも追従する。PartyManagerが無いシーンではPlayer Nameで探す。
/// </summary>
class PlayerHPBarUpdater : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "PlayerHPBarUpdater"; }

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
