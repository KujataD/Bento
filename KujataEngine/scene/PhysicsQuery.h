#pragma once

#include "../math/Vector3.h"
#include "../runtime/KujataApi.h"

namespace KujataEngine {

class Scene;
class GameObject;
class ColliderComponent;

/// <summary>SphereCast で最初に当たったもの。</summary>
struct SphereCastHit {
	ColliderComponent* collider = nullptr; // 当たった Collider
	GameObject* gameObject = nullptr;      // その Collider を持つオブジェクト
	Vector3 point = {0.0f, 0.0f, 0.0f};    // 当たったときの球の中心の位置
	float fraction = 0.0f;                 // 線分の何割のところで当たったか(0=始点、1=終点)
};

/// <summary>
/// 線分 from → to に沿って半径 radius の球を動かしたとき、**最初に当たる Collider** を探す(シーンの Collider を全部調べる)。
/// 弾・水弾のように速く動く物の当たり判定に使う(点で調べると、1フレームで薄い物をすり抜けるため、前の位置から今の位置までを調べる)。
///
/// Collider コンポーネント(球・箱・カプセル)の形はそのまま使う。箱は角を丸めずに半径ぶん広げるので、角の近くでは少しだけ当たりやすい。
/// ignore とその子孫は調べない(撃った本人に当たらないように)。includeTriggers が false ならトリガーの Collider は調べない。
/// 無効(enabled=false)の Collider と、非アクティブなオブジェクトは調べない。当たったら true。
/// </summary>
KUJATA_API bool SphereCast(Scene& scene, const Vector3& from, const Vector3& to, float radius, SphereCastHit& outHit, const GameObject* ignore = nullptr,
                           bool includeTriggers = true);

} // namespace KujataEngine
