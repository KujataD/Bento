#pragma once
#include <KujataEngine.h>

/// <summary>ダメージの種別。ガードの得意/不得意(剣士=物理、術師=魔法)を分けるために使う。</summary>
enum class DamageType {
	Physical = 0,
	Magic = 1,
};

/// <summary>
/// 敵→味方の1回のヒット情報。EnemyWeaponが組み立て、PlayerHealth::ReceiveHitが受け取る。
/// ガード(IGuard)はこれを見て「防げるか・どれだけ軽減するか」を判断する。
/// </summary>
struct HitInfo {
	// 生ダメージ(軽減前)。
	float damage = 0.0f;
	// 物理/魔法。
	DamageType type = DamageType::Physical;
	// ノックバックの初速(ワールド)。ゼロならノックバックしない。
	KujataEngine::Vector3 knockbackVelocity = {0.0f, 0.0f, 0.0f};
	// 被弾硬直の秒数。
	float stunDuration = 0.0f;
	// 攻撃元(敵本体のルート)。ジャストガードの反撃先やのけぞりの通知に使う。
	KujataEngine::GameObject* attacker = nullptr;
};

/// <summary>ガード判定の結果。IGuard::Mitigateが返す。</summary>
struct GuardResult {
	// ダメージ倍率(1=素通し、0.5=半減、0=無効化)。
	float damageScale = 1.0f;
	// ノックバック/硬直を打ち消すか(ガード成立時はtrue)。
	bool negateKnockback = false;
	// ガードが成立したか(演出・ログ用。damageScale<1と同義ではない: 半減も成立扱い)。
	bool blocked = false;
	// ジャストガードが成立したか。
	bool justGuard = false;
};
