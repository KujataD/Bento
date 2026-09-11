#pragma once

#include <KujataEngine.h>
#include <string>

namespace KujataEngine {
class ImageComponent;
}

/// <summary>
/// **相方(操作していない方)のHPと、倒れているときの蘇生までの進み具合を画面へ出す。**
///
/// 付ける先はHPバーの「塗り」側(ImageComponentを持つオブジェクト)。
/// [[PlayerHPBarUpdater]] と同じ作りで、違うのは「リーダーではなく相方を見る」ことと、
/// **倒れている間だけ蘇生バーを出す**ことの2点。
///
/// 蘇生バーは常に出しっぱなしにしない。生きている間は満タンのバーが2本並ぶことになり、
/// 「今どちらが減っているのか」が読み取りにくくなるため。
///
/// 相方が居ないシーン(1人のとき)は両方畳む。
/// </summary>
class AllyBarUpdater : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "AllyBarUpdater"; }
	bool AllowMultiple() const override { return false; }

	void Update() override;

private:
	/// <summary>名前でシーン内のオブジェクトを引く(見つからなければnullptr)。</summary>
	KujataEngine::GameObject* FindByName(const std::string& name) const;

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_STRING_NAMED_TIP(reviveBarName_, "Revive Bar",
		    "蘇生バーの入れ物(背景)の名前。**倒れている間だけ表示**し、それ以外は畳む。\n"
		    "空にすると蘇生バーを扱わない(HPバーだけになる)。");
		KUJATA_REGISTER_STRING_NAMED_TIP(reviveFillName_, "Revive Fill",
		    "蘇生バーの塗りの名前。0→1で「あと何割で起き上がるか」を表す。");
		KUJATA_REGISTER_STRING_NAMED_TIP(hpBarName_, "HP Bar",
		    "HPバーの入れ物(背景)の名前。相方が居ないときはこれごと畳む。空なら畳まない。");
	}

	KUJATA_FIELD_STRING(reviveBarName_, "AllyReviveBarBG");
	KUJATA_FIELD_STRING(reviveFillName_, "AllyReviveBarFill");
	KUJATA_FIELD_STRING(hpBarName_, "AllyHPBarBG");

	// 自分(HPの塗り)。初回のUpdateで拾う。
	KujataEngine::ImageComponent* fillImage_ = nullptr;
};
