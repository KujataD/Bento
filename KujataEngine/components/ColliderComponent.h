#pragma once

#include "../math/Vector3.h"
#include "../runtime/KujataApi.h"
#include "../scene/Component.h"
#include "../shapes/AABB.h"
#include "../shapes/ShapeUtil.h"
#include <cstdint>

namespace KujataEngine {

enum class ColliderShapeType {
	Sphere,
	Box,
	Capsule,
};

/// <summary>
/// Collisionイベントで渡す接触情報。
/// </summary>
struct Collision {
	ColliderComponent* self = nullptr;
	ColliderComponent* other = nullptr;
	GameObject* selfObject = nullptr;
	GameObject* otherObject = nullptr;
	Vector3 point = {};
	Vector3 normal = {};
	float penetrationDepth = 0.0f;
};

/// <summary>
/// GameObjectに付けるUnity風Colliderの基底Component。
/// </summary>
class KUJATA_API ColliderComponent : public Component {
public:
	ColliderComponent();

	const char* GetTypeName() const override { return "ColliderComponent"; }

	ColliderComponent* AsColliderComponent() override { return this; }

	virtual ColliderShapeType GetShapeType() const = 0;

	bool IsTrigger() const { return isTrigger_; }

	/// <summary>
	/// **このコライダーは動くか。** 地面・足場として扱ってよいかを見る側が参照する
	/// (歩行の接地点、デカールの貼り付け先など)。
	///
	/// Rigidbodyの有無から推測しないのは、**推測が当たらない構成が普通にあるため**。
	/// 例: ガーディアンは脚の当たり判定を部位ごとに分けており、部位側はRigidbodyを持たない。
	/// ルートのRigidbodyはコードで動かす都合でIs Staticなので、遡って見ても「動かない地面」と出る。
	/// 動くかどうかは置いた本人にしか分からないので、明示して持たせる。
	/// </summary>
	bool IsMovingCollider() const { return movingCollider_; }
	void SetMovingCollider(bool moving) { movingCollider_ = moving; }

	void SetTrigger(bool isTrigger) { isTrigger_ = isTrigger; }

	/// <summary>
	/// Collider中心のローカルオフセットを取得します。
	/// </summary>
	const Vector3& GetCenter() const { return center_; }

	void SetCenter(const Vector3& center) { center_ = center; }

	/// <summary>
	/// 接触対象Layerのビットマスクを取得します。
	/// </summary>
	uint32_t GetCollisionMask() const { return collisionMask_; }

	void SetCollisionMask(uint32_t collisionMask) { collisionMask_ = collisionMask; }

	/// <summary>
	/// Scene内のCollider識別子を取得します。
	/// </summary>
	uint64_t GetRuntimeId() const { return runtimeId_; }

	/// <summary>
	/// OwnerのLayerを衝突属性ビットとして取得します。
	/// </summary>
	uint32_t GetLayerMaskBit() const;

	/// <summary>
	/// 指定ColliderとLayer/Mask上接触可能かを返します。
	/// </summary>
	bool CanCollideWith(const ColliderComponent& other) const;

	/// <summary>
	/// Collider中心をワールド座標で取得します。
	/// </summary>
	Vector3 GetWorldCenter() const;

	virtual Sphere GetWorldSphere() const;

	virtual AABB GetWorldAABB() const;

	/// <summary>
	/// 指定Colliderと交差しているかを返します。
	/// </summary>
	bool Intersects(const ColliderComponent& other) const;

	/// <summary>
	/// 指定Colliderとの接触情報(法線・めり込み量・接触点)を計算します。
	/// 交差していれば true を返し out を埋めます。normal は自分→otherへの分離方向。
	/// レイヤーマスクは判定しません(呼び出し側の責務)。
	/// </summary>
	bool ComputeContact(const ColliderComponent& other, Contact& out) const;

	void DrawInspector() override;

	/// <summary>
	/// Collider共通情報をJSONへ書きます。
	/// </summary>
	void WriteJson(nlohmann::json& json) const override;

	/// <summary>
	/// Collider共通情報をJSONから読みます。
	/// </summary>
	void ReadJson(const nlohmann::json& json) override;

protected:
	/// <summary>
	/// 派生Collider固有のInspectorを描画します。
	/// </summary>
	virtual void DrawShapeInspector() {}

	/// <summary>
	/// 派生Collider固有の情報をJSONへ書きます。
	/// </summary>
	virtual void WriteShapeJson(nlohmann::json& json) const { (void)json; }

	/// <summary>
	/// 派生Collider固有の情報をJSONから読みます。
	/// </summary>
	virtual void ReadShapeJson(const nlohmann::json& json) { (void)json; }

	bool isTrigger_ = false;
	// 動くコライダーか(地面/足場として扱わせない印)。Rigidbodyの有無とは独立に指定する。
	bool movingCollider_ = false;
	Vector3 center_ = {0.0f, 0.0f, 0.0f};
	uint32_t collisionMask_ = 0xffffffff;

private:
	uint64_t runtimeId_ = 0;
};

class KUJATA_API SphereColliderComponent : public ColliderComponent {
public:
	const char* GetTypeName() const override { return "SphereColliderComponent"; }
	ColliderShapeType GetShapeType() const override { return ColliderShapeType::Sphere; }

	float GetRadius() const { return radius_; }
	void SetRadius(float radius);

	Sphere GetWorldSphere() const override;
	AABB GetWorldAABB() const override;

protected:
	void DrawShapeInspector() override;
	void WriteShapeJson(nlohmann::json& json) const override;
	void ReadShapeJson(const nlohmann::json& json) override;

private:
	float radius_ = 0.5f;
};

/// <summary>
/// 箱Collider。WorldRotationがゼロならAABB、回転があればOBBとして扱います。
/// </summary>
class KUJATA_API BoxColliderComponent : public ColliderComponent {
public:
	const char* GetTypeName() const override { return "BoxColliderComponent"; }
	ColliderShapeType GetShapeType() const override { return ColliderShapeType::Box; }

	const Vector3& GetSize() const { return size_; }
	void SetSize(const Vector3& size);

	Sphere GetWorldSphere() const override;
	AABB GetWorldAABB() const override;

	/// <summary>
	/// WorldRotationがあるためOBBとして扱うかを返します。
	/// </summary>
	bool UsesWorldOBB() const;

	OBB GetWorldOBB() const;

protected:
	void DrawShapeInspector() override;
	void WriteShapeJson(nlohmann::json& json) const override;
	void ReadShapeJson(const nlohmann::json& json) override;

private:
	Vector3 size_ = {1.0f, 1.0f, 1.0f};
};

/// <summary>
/// カプセルCollider。Unity準拠でradius/height/direction(0=X,1=Y,2=Z)を持ちます。
/// heightは両端の半球を含む全長で、cylinder部分の長さは height - 2*radius です。
/// </summary>
class KUJATA_API CapsuleColliderComponent : public ColliderComponent {
public:
	const char* GetTypeName() const override { return "CapsuleColliderComponent"; }
	ColliderShapeType GetShapeType() const override { return ColliderShapeType::Capsule; }

	float GetRadius() const { return radius_; }
	void SetRadius(float radius);

	float GetHeight() const { return height_; }
	void SetHeight(float height);

	/// <summary>
	/// カプセル軸方向(0=X,1=Y,2=Z)。
	/// </summary>
	int GetDirection() const { return direction_; }
	void SetDirection(int direction);

	/// <summary>
	/// ワールド空間のカプセル(spine端点+半径)を返します。
	/// </summary>
	Capsule GetWorldCapsule() const;

	Sphere GetWorldSphere() const override;
	AABB GetWorldAABB() const override;

protected:
	void DrawShapeInspector() override;
	void WriteShapeJson(nlohmann::json& json) const override;
	void ReadShapeJson(const nlohmann::json& json) override;

private:
	float radius_ = 0.5f;
	float height_ = 2.0f;
	int direction_ = 1; // 既定はY軸(Unity準拠)
};

} // namespace KujataEngine
