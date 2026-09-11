#pragma once

#include <KujataEngine.h>
#include <string>

namespace KujataEngine {
class ImageComponent;
}
class EnemyHealth;

/// <summary>
/// 画面下中央に出すボス専用のHPバー(エルデンリング風)。
/// **バー一式をまとめる親オブジェクトに付ける**(Fill側ではない)。
///
/// 雑魚の頭上バー(HPBarUpdater)と違い、対象は名前で1体だけ指すのでバーの持ち主で迷わない。
/// 見せ方も別物で、頭上バーが「そこに敵がいる」ことを示すのに対し、
/// こちらは**「この一戦の相手はこいつだ」**という宣言なので、画面に固定して名前と並べる。
///
/// - 対象が見つからない/倒れている間は**子をまとめて隠す**。
///   自分自身を隠すとUpdateごと止まり、二度と出せなくなるので、消すのは必ず子だけ。
/// - 減りは Fill の Image の fillAmount へ流し込む(PlayerHPBarUpdaterと同じ方式)。
/// - 名前の文字は子のTextオブジェクトが静的に持つ。ここでは触らない
///   (TextComponentはKUJATA_APIが無くGameModuleから書き換えられない)。
/// </summary>
class BossHPBarUpdater : public KujataEngine::Component {
public:
	const char* GetTypeName() const override { return "BossHPBarUpdater"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void Update() override;

private:
	/// <summary>対象のボスを名前で探す(見つからなければnullptr)。</summary>
	EnemyHealth* FindBoss() const;
	/// <summary>子孫から名前でオブジェクトを探す。</summary>
	KujataEngine::GameObject* FindDescendant(const std::string& name) const;
	/// <summary>子をまとめて出し入れする。**自分は触らない。**</summary>
	void SetChildrenVisible(bool visible);

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_STRING_NAMED_TIP(bossName_, "Boss Name",
		    "HPを表示するボスのGameObject名。HealthComponent(EnemyHealth)を持っている必要がある。");
		KUJATA_REGISTER_STRING_NAMED_TIP(fillObjectName_, "Fill Object",
		    "減っていく帯の子オブジェクト名(Imageを持つもの)。");
		KUJATA_REGISTER_BOOL_NAMED_TIP(hideWhenAbsent_, "Hide When Absent",
		    "対象が見つからない/倒れているときにバー一式を隠すか。\n"
		    "**隠さないと、撃破後も空のバーが画面に残る。**");
	}

	// 対象のボスの名前。
	KUJATA_FIELD_STRING(bossName_, "GuardianSpline");
	// 帯の子オブジェクト名。
	KUJATA_FIELD_STRING(fillObjectName_, "BossHPBarFill");
	// 不在時に隠すか。
	KUJATA_FIELD_BOOL(hideWhenAbsent_, true);

	KujataEngine::ImageComponent* fillImage_ = nullptr;
};
