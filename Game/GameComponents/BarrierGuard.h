#pragma once
#include "IGuard.h"
#include <string>

class CharacterMotor;
class StaminaComponent;
class IAbilitySet;
class MagicAbilitySet;

/// <summary>
/// 術師(Bishop)のガード。L2を押している間、自分を中心に球体バリアを展開する。
///
///   魔法攻撃: 完全に防ぐ。
///   物理攻撃: ダメージ半減(Physical Damage Scale)。ノックバックは受ける。
///   範囲防御: バリア球(Radius)の中にいる相方も同じ効果で守る(PlayerHealth::ReceiveHitがCoversPositionで問い合わせる)。
///   スタミナ: 展開中は Drain Per Second[%/秒]で継続消費し、空になると自動で閉じる。剣士と違いスタンは無い。
///   ジャストガード: 展開し始めてから Just Guard Window 秒以内の魔法攻撃は、魔法のつぶて(MagicAbilitySet::FirePebble)で
///             攻撃元へ自動反撃する(自分が受けた分も相方が受けた分も)。
///
/// 見た目は Barrier Prefab(両面描画の半透明球)をランタイム生成し、展開中だけアクティブにして自分の位置へ置く。
/// 構え中は移動が鈍足(Player側で0.5倍)、攻撃は出せない。回避は可(展開は閉じる)。
/// </summary>
class BarrierGuard : public IGuard {
public:
	const char* GetTypeName() const override { return "BarrierGuard"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void Update() override;

	void SetGuardInput(bool pressed) override;
	bool IsGuarding() const override { return active_; }
	GuardResult Mitigate(const HitInfo& hit) override;
	bool CoversPosition(const KujataEngine::Vector3& worldPosition) const override;
	GuardResult MitigateForAlly(const HitInfo& hit) override;

	float GetRadius() const { return radius_; }

	/// <summary>
	/// **バリアを「自分」ではなく「守る相手」の位置へ張る。** nullptrで自分中心へ戻る。
	///
	/// 術師は本来ボスから離れた間合いで戦うので、相方(剣士)を守るために前へ出ると
	/// 自分が的になってしまう。中心を切り離せば、離れたまま剣士だけを球で包める。
	/// 頭脳(AllyAIBrain / Player)が毎フレーム指定する運用で、Play中の一時的な状態として持つ。
	/// </summary>
	void SetProtectTarget(KujataEngine::GameObject* target) { protectTarget_ = target; }
	KujataEngine::GameObject* GetProtectTarget() const { return protectTarget_; }

private:
	void Open();
	void Close();
	/// <summary>バリア球の中心(ワールド)。</summary>
	KujataEngine::Vector3 GetCenter() const;
	/// <summary>自分と相方で共通の軽減処理。</summary>
	GuardResult MitigateCommon(const HitInfo& hit);
	/// <summary>バリアの見た目を出す/隠す/動かす。</summary>
	void UpdateVisual();

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_STRING_NAMED_TIP(barrierPrefabPath_, "Barrier Prefab",
		    "バリア球のPrefab(プロジェクトルート相対)。単位球(半径1)で作っておくとRadiusがそのままスケールになる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(radius_, "Radius", 0.05f, 0.5f, 20.0f, "バリア球の半径。相方がこの中にいれば守られる。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(centerHeight_, "Center Height", 0.05f, -5.0f, 10.0f, "バリア球の中心の高さ(自分の位置からの上方向オフセット)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(drainPerSecond_, "Drain Per Second", 0.5f, 0.0f, 100.0f, "展開中のスタミナ消費[最大値比%/秒]。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(physicalDamageScale_, "Physical Damage Scale", 0.05f, 0.0f, 1.0f, "物理攻撃のダメージ倍率(0.5=半減)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(justGuardWindow_, "Just Guard Window", 0.01f, 0.0f, 1.0f, "展開し始めからこの秒数以内の魔法ヒットがジャストガード(つぶて反撃)になる。");
	}

	// バリアPrefab。
	KUJATA_FIELD_STRING(barrierPrefabPath_, "Prefabs/MagicBarrier.prefab.json");
	// 半径。
	KUJATA_FIELD_FLOAT(radius_, 2.5f);
	// 中心の高さ。
	KUJATA_FIELD_FLOAT(centerHeight_, 1.0f);
	// 継続消費[%/s]。
	KUJATA_FIELD_FLOAT(drainPerSecond_, 15.0f);
	// 物理の軽減率。
	KUJATA_FIELD_FLOAT(physicalDamageScale_, 0.5f);
	// ジャストガード猶予[s]。
	KUJATA_FIELD_FLOAT(justGuardWindow_, 0.2f);

	// 同じGameObjectの体・スタミナ・技。
	CharacterMotor* motor_ = nullptr;
	StaminaComponent* stamina_ = nullptr;
	IAbilitySet* abilitySet_ = nullptr;
	MagicAbilitySet* magic_ = nullptr;

	// --- 実行状態 ---
	// 展開中か。
	bool active_ = false;
	// 展開し始めてからの時間[s]。
	float activeTime_ = 0.0f;
	// 一度ボタンを離すまで展開し直せない(中断後にジャストガードを連発させない)。
	bool needRelease_ = false;
	// バリアの見た目(Prefabから初回だけ生成)。
	KujataEngine::GameObject* visual_ = nullptr;
	bool visualTried_ = false;
	// 球の中心を預ける相手(nullptr=自分)。シリアライズしないPlay中だけの状態。
	KujataEngine::GameObject* protectTarget_ = nullptr;
};
