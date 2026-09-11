#pragma once

#include "../runtime/KujataApi.h"
#include "../math/Vector3.h"
#include "AABB.h"
#include "Rect.h"
#include "ShapeUtil.h"

namespace KujataEngine {

// プレイヤー陣営
const uint32_t kCollisionAttributePlayer = 0b1;
// 敵陣営
const uint32_t kCollisionAttributeEnemy = 0b1 << 1;

class KUJATA_API Collider {
public:
	// 半径を取得
	float GetRadius() const { return radius_; }

	// 半径を設定
	void SetRadius(float radius) { radius_ = radius; }

	// 衝突時に呼ばれる関数
	virtual void OnCollision() = 0;

	// --- getter ---

	/// <summary>
	/// WorldPositionを取得します。
	/// </summary>
	virtual Vector3 GetWorldPosition() const = 0;
	Sphere GetSphere() const { return Sphere{GetWorldPosition(), GetRadius()}; }
	// 衝突マスク（相手）
	uint32_t GetCollisionMask() { return collisionMask_; }
	// 衝突属性（自分）
	uint32_t GetCollisionAttribute() { return collisionAttribute_; }

	// --- setter ---

	// 衝突マスク（相手）
	void SetCollisionMask(uint32_t collisionMask) { collisionMask_ = collisionMask; }
	// 衝突属性（自分）
	void SetCollisionAttribute(uint32_t collisionMask) { collisionAttribute_ = collisionMask; }


private:
	// 衝突判定
	float radius_ = 1.0f;

	// 衝突属性（自分）
	uint32_t collisionAttribute_ = 0xffffffff;

	// 衝突マスク（相手）
	uint32_t collisionMask_ = 0xffffffff;
};

} // namespace KujataEngine
