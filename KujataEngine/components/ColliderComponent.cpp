#include "ColliderComponent.h"
#include "../runtime/InspectorUI.h"
#include "../math/MathUtil.h"
#include "../scene/GameObject.h"
#include "../shapes/ShapeUtil.h"
#include <algorithm>
#include <atomic>
#include <cmath>

namespace KujataEngine {

namespace {

constexpr float kWorldRotationEpsilon = 0.0001f;

uint64_t GenerateColliderRuntimeId() {
	static std::atomic<uint64_t> counter = 0;
	return ++counter;
}

Vector3 ReadVector3(const nlohmann::json& json, const char* key, const Vector3& defaultValue) {
	if (!json.contains(key)) {
		return defaultValue;
	}
	if (!json.at(key).is_array()) {
		return defaultValue;
	}
	if (json.at(key).size() < 3) {
		return defaultValue;
	}

	Vector3 value = defaultValue;
	if (json.at(key).at(0).is_number()) {
		value.x = json.at(key).at(0).get<float>();
	}
	if (json.at(key).at(1).is_number()) {
		value.y = json.at(key).at(1).get<float>();
	}
	if (json.at(key).at(2).is_number()) {
		value.z = json.at(key).at(2).get<float>();
	}
	return value;
}

bool ReadBool(const nlohmann::json& json, const char* key, bool defaultValue) {
	if (!json.contains(key)) {
		return defaultValue;
	}
	if (!json.at(key).is_boolean()) {
		return defaultValue;
	}
	return json.at(key).get<bool>();
}

uint32_t ReadUint32(const nlohmann::json& json, const char* key, uint32_t defaultValue) {
	if (!json.contains(key)) {
		return defaultValue;
	}
	if (!json.at(key).is_number_unsigned() && !json.at(key).is_number_integer()) {
		return defaultValue;
	}

	int64_t value = json.at(key).get<int64_t>();
	if (value < 0) {
		return defaultValue;
	}
	return static_cast<uint32_t>(value);
}

float ReadFloat(const nlohmann::json& json, const char* key, float defaultValue) {
	if (!json.contains(key)) {
		return defaultValue;
	}
	if (!json.at(key).is_number()) {
		return defaultValue;
	}
	return json.at(key).get<float>();
}

Vector3 AbsoluteVector(const Vector3& value) {
	return {std::fabs(value.x), std::fabs(value.y), std::fabs(value.z)};
}

Vector3 MaxVector(const Vector3& a, const Vector3& b) {
	return {
	    (std::max)(a.x, b.x),
	    (std::max)(a.y, b.y),
	    (std::max)(a.z, b.z),
	};
}

bool IsNearlyEqual(float a, float b) {
	return std::fabs(a - b) <= kWorldRotationEpsilon;
}

bool IsNearlyEqual(const Vector3& a, const Vector3& b) {
	if (!IsNearlyEqual(a.x, b.x)) {
		return false;
	}
	if (!IsNearlyEqual(a.y, b.y)) {
		return false;
	}
	if (!IsNearlyEqual(a.z, b.z)) {
		return false;
	}
	return true;
}

Vector3 GetWorldAxis(const Matrix4x4& matrix, uint32_t axisIndex) {
	Vector3 axis = {matrix.m[axisIndex][0], matrix.m[axisIndex][1], matrix.m[axisIndex][2]};
	float length = Length(axis);
	if (length <= kWorldRotationEpsilon) {
		if (axisIndex == 0) {
			return {1.0f, 0.0f, 0.0f};
		}
		if (axisIndex == 1) {
			return {0.0f, 1.0f, 0.0f};
		}
		return {0.0f, 0.0f, 1.0f};
	}
	return axis / length;
}

float GetWorldAxisScale(const Matrix4x4& matrix, uint32_t axisIndex) {
	Vector3 axis = {matrix.m[axisIndex][0], matrix.m[axisIndex][1], matrix.m[axisIndex][2]};
	return Length(axis);
}

bool HasWorldRotation(const Matrix4x4& matrix) {
	if (!IsNearlyEqual(GetWorldAxis(matrix, 0), {1.0f, 0.0f, 0.0f})) {
		return true;
	}
	if (!IsNearlyEqual(GetWorldAxis(matrix, 1), {0.0f, 1.0f, 0.0f})) {
		return true;
	}
	if (!IsNearlyEqual(GetWorldAxis(matrix, 2), {0.0f, 0.0f, 1.0f})) {
		return true;
	}
	return false;
}

// GetShapeType()で型を確認済みの前提でBox派生へ落とす(dynamic_castよりRTTIコストが無い分速い)。
const BoxColliderComponent& AsBoxColliderUnchecked(const ColliderComponent& collider) { return static_cast<const BoxColliderComponent&>(collider); }

// GetShapeType()で型を確認済みの前提でCapsule派生へ落とす。
const CapsuleColliderComponent& AsCapsuleColliderUnchecked(const ColliderComponent& collider) { return static_cast<const CapsuleColliderComponent&>(collider); }

bool IsCollisionBoxAndBox(const ColliderComponent& a, const ColliderComponent& b) {
	const BoxColliderComponent& boxA = AsBoxColliderUnchecked(a);
	const BoxColliderComponent& boxB = AsBoxColliderUnchecked(b);

	if (boxA.UsesWorldOBB() || boxB.UsesWorldOBB()) {
		return ShapeUtil::IsCollision(boxA.GetWorldOBB(), boxB.GetWorldOBB());
	}

	return ShapeUtil::IsCollision(boxA.GetWorldAABB(), boxB.GetWorldAABB());
}

// Sphere/CapsuleをワールドCapsuleへ変換する(球はp0==p1の退化カプセルとして扱う)。Boxには使わない。
Capsule MakeWorldCapsule(const ColliderComponent& collider) {
	if (collider.GetShapeType() == ColliderShapeType::Capsule) {
		return AsCapsuleColliderUnchecked(collider).GetWorldCapsule();
	}
	Sphere sphere = collider.GetWorldSphere();
	return {sphere.center, sphere.center, sphere.radius};
}

// 任意ColliderをワールドOBBへ変換する(Box以外はAABBから軸並行OBBを作る)。
OBB MakeWorldOBB(const ColliderComponent& collider) {
	if (collider.GetShapeType() == ColliderShapeType::Box) {
		return AsBoxColliderUnchecked(collider).GetWorldOBB();
	}
	AABB aabb = collider.GetWorldAABB();
	OBB obb{};
	obb.center = (aabb.min + aabb.max) * 0.5f;
	obb.orientations[0] = {1.0f, 0.0f, 0.0f};
	obb.orientations[1] = {0.0f, 1.0f, 0.0f};
	obb.orientations[2] = {0.0f, 0.0f, 1.0f};
	obb.size = (aabb.max - aabb.min) * 0.5f;
	return obb;
}

// a→b への接触情報を計算する(normal は a から b へ向かう分離方向)。
// Sphere/Capsuleはカプセル(球は退化カプセル)、BoxはOBBへ統一して扱う。
bool ComputeContactByShape(const ColliderComponent& a, const ColliderComponent& b, Contact& out) {
	const bool aBox = a.GetShapeType() == ColliderShapeType::Box;
	const bool bBox = b.GetShapeType() == ColliderShapeType::Box;

	if (!aBox && !bBox) {
		// カプセル-カプセル(sphere-sphere / sphere-capsule を含む)
		return ShapeUtil::ComputeContact(MakeWorldCapsule(a), MakeWorldCapsule(b), out);
	}
	if (aBox && bBox) {
		return ShapeUtil::ComputeContact(MakeWorldOBB(a), MakeWorldOBB(b), out);
	}
	if (!aBox && bBox) {
		// カプセルa→箱b: normal は capsule→obb = a→b
		return ShapeUtil::ComputeContact(MakeWorldCapsule(a), MakeWorldOBB(b), out);
	}
	// 箱a→カプセルb: capsule(b)→obb(a) を求めると normal は b→a なので反転する
	if (ShapeUtil::ComputeContact(MakeWorldCapsule(b), MakeWorldOBB(a), out)) {
		out.normal = out.normal * -1.0f;
		return true;
	}
	return false;
}

bool IntersectsByShape(const ColliderComponent& a, const ColliderComponent& b) {
	const bool aBox = a.GetShapeType() == ColliderShapeType::Box;
	const bool bBox = b.GetShapeType() == ColliderShapeType::Box;

	if (!aBox && !bBox) {
		return ShapeUtil::IsCollision(MakeWorldCapsule(a), MakeWorldCapsule(b));
	}

	if (aBox && bBox) {
		return IsCollisionBoxAndBox(a, b);
	}

	// カプセル(または球) vs 箱
	const ColliderComponent& capsuleCollider = aBox ? b : a;
	const ColliderComponent& boxCollider = aBox ? a : b;
	return ShapeUtil::IsCollision(MakeWorldCapsule(capsuleCollider), MakeWorldOBB(boxCollider));
}

} // namespace

ColliderComponent::ColliderComponent() : runtimeId_(GenerateColliderRuntimeId()) {}

uint32_t ColliderComponent::GetLayerMaskBit() const {
	GameObject* owner = GetOwner();
	if (!owner) {
		return 1u;
	}

	uint32_t layer = owner->GetLayer();
	if (layer > 31) {
		layer = 31;
	}
	return 1u << layer;
}

bool ColliderComponent::CanCollideWith(const ColliderComponent& other) const {
	uint32_t selfLayerBit = GetLayerMaskBit();
	uint32_t otherLayerBit = other.GetLayerMaskBit();

	if ((collisionMask_ & otherLayerBit) == 0) {
		return false;
	}
	if ((other.collisionMask_ & selfLayerBit) == 0) {
		return false;
	}

	return true;
}

Vector3 ColliderComponent::GetWorldCenter() const {
	GameObject* owner = GetOwner();
	if (!owner) {
		return center_;
	}

	const_cast<GameObject*>(owner)->UpdateWorldTransformSelfAndAncestors();
	return Transform(center_, owner->GetTransform().matWorld_);
}

Sphere ColliderComponent::GetWorldSphere() const {
	return {GetWorldCenter(), 0.0f};
}

AABB ColliderComponent::GetWorldAABB() const {
	Vector3 center = GetWorldCenter();
	return {center, center};
}

bool ColliderComponent::Intersects(const ColliderComponent& other) const {
	if (!CanCollideWith(other)) {
		return false;
	}

	return IntersectsByShape(*this, other);
}

bool ColliderComponent::ComputeContact(const ColliderComponent& other, Contact& out) const {
	return ComputeContactByShape(*this, other, out);
}

void ColliderComponent::DrawInspector() {
	bool isTrigger = isTrigger_;
	if (InspectorUI::Checkbox("Is Trigger", &isTrigger)) {
		isTrigger_ = isTrigger;
	}

	bool moving = movingCollider_;
	if (InspectorUI::Checkbox("Moving Collider", &moving)) {
		movingCollider_ = moving;
	}
	InspectorUI::ItemTooltip(
	    "このコライダーが動くなら on。地面・足場として扱われなくなる。\n"
	    "**Rigidbodyの有無では判定できない構成のための明示指定**。\n"
	    "例: 部位ごとに当たり判定を分けた敵の脚(部位側はRigidbodyを持たない)。");

	InspectorUI::DragFloat3("Center", &center_.x, 0.01f);

	int collisionMask = -1;
	if (collisionMask_ != 0xffffffff) {
		collisionMask = static_cast<int>(collisionMask_);
	}
	if (InspectorUI::DragInt("Collision Mask", &collisionMask, 1.0f, -1, 0x7fffffff)) {
		if (collisionMask < 0) {
			collisionMask_ = 0xffffffff;
		} else {
			collisionMask_ = static_cast<uint32_t>(collisionMask);
		}
	}

	DrawShapeInspector();
}

void ColliderComponent::WriteJson(nlohmann::json& json) const {
	json["isTrigger"] = isTrigger_;
	json["movingCollider"] = movingCollider_;
	json["center"] = {center_.x, center_.y, center_.z};
	json["collisionMask"] = collisionMask_;
	WriteShapeJson(json);
}

void ColliderComponent::ReadJson(const nlohmann::json& json) {
	isTrigger_ = ReadBool(json, "isTrigger", isTrigger_);
	movingCollider_ = ReadBool(json, "movingCollider", movingCollider_);
	center_ = ReadVector3(json, "center", center_);
	collisionMask_ = ReadUint32(json, "collisionMask", collisionMask_);
	ReadShapeJson(json);
}

void SphereColliderComponent::SetRadius(float radius) {
	if (radius < 0.0f) {
		radius = 0.0f;
	}
	radius_ = radius;
}

Sphere SphereColliderComponent::GetWorldSphere() const {
	float maxScale = 1.0f;
	GameObject* owner = GetOwner();
	if (owner) {
		Vector3 scale = AbsoluteVector(owner->GetTransform().scale_);
		maxScale = (std::max)(scale.x, (std::max)(scale.y, scale.z));
	}

	return {GetWorldCenter(), radius_ * maxScale};
}

AABB SphereColliderComponent::GetWorldAABB() const {
	Sphere sphere = GetWorldSphere();
	Vector3 radiusVector = {sphere.radius, sphere.radius, sphere.radius};
	return {sphere.center - radiusVector, sphere.center + radiusVector};
}

void SphereColliderComponent::DrawShapeInspector() {
	InspectorUI::DragFloat("Radius", &radius_, 0.01f, 0.0f, 0.0f);
	if (radius_ < 0.0f) {
		radius_ = 0.0f;
	}
}

void SphereColliderComponent::WriteShapeJson(nlohmann::json& json) const {
	json["radius"] = radius_;
}

void SphereColliderComponent::ReadShapeJson(const nlohmann::json& json) {
	SetRadius(ReadFloat(json, "radius", radius_));
}

void BoxColliderComponent::SetSize(const Vector3& size) {
	size_ = MaxVector(size, {0.0f, 0.0f, 0.0f});
}

Sphere BoxColliderComponent::GetWorldSphere() const {
	OBB obb = GetWorldOBB();
	float radius = Length(obb.size);
	Vector3 center = obb.center;
	return {center, radius};
}

AABB BoxColliderComponent::GetWorldAABB() const {
	if (UsesWorldOBB()) {
		OBB obb = GetWorldOBB();
		Vector3 corners[8] = {
		    obb.center - obb.orientations[0] * obb.size.x - obb.orientations[1] * obb.size.y - obb.orientations[2] * obb.size.z,
		    obb.center + obb.orientations[0] * obb.size.x - obb.orientations[1] * obb.size.y - obb.orientations[2] * obb.size.z,
		    obb.center + obb.orientations[0] * obb.size.x + obb.orientations[1] * obb.size.y - obb.orientations[2] * obb.size.z,
		    obb.center - obb.orientations[0] * obb.size.x + obb.orientations[1] * obb.size.y - obb.orientations[2] * obb.size.z,
		    obb.center - obb.orientations[0] * obb.size.x - obb.orientations[1] * obb.size.y + obb.orientations[2] * obb.size.z,
		    obb.center + obb.orientations[0] * obb.size.x - obb.orientations[1] * obb.size.y + obb.orientations[2] * obb.size.z,
		    obb.center + obb.orientations[0] * obb.size.x + obb.orientations[1] * obb.size.y + obb.orientations[2] * obb.size.z,
		    obb.center - obb.orientations[0] * obb.size.x + obb.orientations[1] * obb.size.y + obb.orientations[2] * obb.size.z,
		};

		AABB aabb{corners[0], corners[0]};
		for (const Vector3& corner : corners) {
			aabb.min.x = (std::min)(aabb.min.x, corner.x);
			aabb.min.y = (std::min)(aabb.min.y, corner.y);
			aabb.min.z = (std::min)(aabb.min.z, corner.z);
			aabb.max.x = (std::max)(aabb.max.x, corner.x);
			aabb.max.y = (std::max)(aabb.max.y, corner.y);
			aabb.max.z = (std::max)(aabb.max.z, corner.z);
		}
		return aabb;
	}

	Vector3 center = GetWorldCenter();
	Vector3 size = size_;
	GameObject* owner = GetOwner();
	if (owner) {
		Vector3 scale = AbsoluteVector(owner->GetTransform().scale_);
		size.x *= scale.x;
		size.y *= scale.y;
		size.z *= scale.z;
	}

	Vector3 halfSize = size * 0.5f;
	AABB aabb{center - halfSize, center + halfSize};
	aabb.SwapMinMax();
	return aabb;
}

bool BoxColliderComponent::UsesWorldOBB() const {
	GameObject* owner = GetOwner();
	if (!owner) {
		return false;
	}

	const_cast<GameObject*>(owner)->UpdateWorldTransformSelfAndAncestors();
	return HasWorldRotation(owner->GetTransform().matWorld_);
}

OBB BoxColliderComponent::GetWorldOBB() const {
	OBB obb{};
	obb.center = GetWorldCenter();
	obb.orientations[0] = {1.0f, 0.0f, 0.0f};
	obb.orientations[1] = {0.0f, 1.0f, 0.0f};
	obb.orientations[2] = {0.0f, 0.0f, 1.0f};
	obb.size = size_ * 0.5f;

	GameObject* owner = GetOwner();
	if (!owner) {
		return obb;
	}

	const_cast<GameObject*>(owner)->UpdateWorldTransformSelfAndAncestors();
	const Matrix4x4& matrix = owner->GetTransform().matWorld_;
	obb.orientations[0] = GetWorldAxis(matrix, 0);
	obb.orientations[1] = GetWorldAxis(matrix, 1);
	obb.orientations[2] = GetWorldAxis(matrix, 2);
	obb.size.x *= GetWorldAxisScale(matrix, 0);
	obb.size.y *= GetWorldAxisScale(matrix, 1);
	obb.size.z *= GetWorldAxisScale(matrix, 2);
	return obb;
}

void BoxColliderComponent::DrawShapeInspector() {
	InspectorUI::DragFloat3("Size", &size_.x, 0.01f, 0.0f, 0.0f);
	SetSize(size_);
}

void BoxColliderComponent::WriteShapeJson(nlohmann::json& json) const {
	json["size"] = {size_.x, size_.y, size_.z};
}

void BoxColliderComponent::ReadShapeJson(const nlohmann::json& json) {
	SetSize(ReadVector3(json, "size", size_));
}

void CapsuleColliderComponent::SetRadius(float radius) {
	radius_ = (radius < 0.0f) ? 0.0f : radius;
}

void CapsuleColliderComponent::SetHeight(float height) {
	height_ = (height < 0.0f) ? 0.0f : height;
}

void CapsuleColliderComponent::SetDirection(int direction) {
	if (direction < 0) {
		direction = 0;
	}
	if (direction > 2) {
		direction = 2;
	}
	direction_ = direction;
}

Capsule CapsuleColliderComponent::GetWorldCapsule() const {
	Vector3 localAxis = {0.0f, 1.0f, 0.0f};
	if (direction_ == 0) {
		localAxis = {1.0f, 0.0f, 0.0f};
	} else if (direction_ == 2) {
		localAxis = {0.0f, 0.0f, 1.0f};
	}

	// spineの半長 = 全長の半分から半径を引いた分(cylinder部分の半分)。
	float halfSpine = height_ * 0.5f - radius_;
	if (halfSpine < 0.0f) {
		halfSpine = 0.0f;
	}

	Vector3 localP0 = center_ - localAxis * halfSpine;
	Vector3 localP1 = center_ + localAxis * halfSpine;

	GameObject* owner = GetOwner();
	if (!owner) {
		return {localP0, localP1, radius_};
	}

	const_cast<GameObject*>(owner)->UpdateWorldTransformSelfAndAncestors();
	const Matrix4x4& matrix = owner->GetTransform().matWorld_;

	Vector3 worldP0 = Transform(localP0, matrix);
	Vector3 worldP1 = Transform(localP1, matrix);

	// 半径はカプセル軸に垂直な2軸のスケール最大で拡縮する。
	float perpScale = 1.0f;
	if (direction_ == 0) {
		perpScale = (std::max)(GetWorldAxisScale(matrix, 1), GetWorldAxisScale(matrix, 2));
	} else if (direction_ == 2) {
		perpScale = (std::max)(GetWorldAxisScale(matrix, 0), GetWorldAxisScale(matrix, 1));
	} else {
		perpScale = (std::max)(GetWorldAxisScale(matrix, 0), GetWorldAxisScale(matrix, 2));
	}

	return {worldP0, worldP1, radius_ * perpScale};
}

Sphere CapsuleColliderComponent::GetWorldSphere() const {
	Capsule capsule = GetWorldCapsule();
	Vector3 center = (capsule.p0 + capsule.p1) * 0.5f;
	float halfLength = Length(capsule.p1 - capsule.p0) * 0.5f;
	return {center, halfLength + capsule.radius};
}

AABB CapsuleColliderComponent::GetWorldAABB() const {
	Capsule capsule = GetWorldCapsule();
	Vector3 radiusVector = {capsule.radius, capsule.radius, capsule.radius};
	Vector3 min0 = capsule.p0 - radiusVector;
	Vector3 max0 = capsule.p0 + radiusVector;
	Vector3 min1 = capsule.p1 - radiusVector;
	Vector3 max1 = capsule.p1 + radiusVector;

	AABB aabb{};
	aabb.min = {(std::min)(min0.x, min1.x), (std::min)(min0.y, min1.y), (std::min)(min0.z, min1.z)};
	aabb.max = {(std::max)(max0.x, max1.x), (std::max)(max0.y, max1.y), (std::max)(max0.z, max1.z)};
	return aabb;
}

void CapsuleColliderComponent::DrawShapeInspector() {
	InspectorUI::DragFloat("Radius", &radius_, 0.01f, 0.0f, 0.0f);
	if (radius_ < 0.0f) {
		radius_ = 0.0f;
	}
	InspectorUI::DragFloat("Height", &height_, 0.01f, 0.0f, 0.0f);
	if (height_ < 0.0f) {
		height_ = 0.0f;
	}

	const char* directionItems[] = {"X-Axis", "Y-Axis", "Z-Axis"};
	if (InspectorUI::Combo("Direction", &direction_, directionItems, 3)) {
		SetDirection(direction_);
	}
}

void CapsuleColliderComponent::WriteShapeJson(nlohmann::json& json) const {
	json["radius"] = radius_;
	json["height"] = height_;
	json["direction"] = direction_;
}

void CapsuleColliderComponent::ReadShapeJson(const nlohmann::json& json) {
	SetRadius(ReadFloat(json, "radius", radius_));
	SetHeight(ReadFloat(json, "height", height_));
	SetDirection(static_cast<int>(ReadUint32(json, "direction", static_cast<uint32_t>(direction_))));
}

} // namespace KujataEngine
