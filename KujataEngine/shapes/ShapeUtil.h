#pragma once
#include "../runtime/KujataApi.h"
#include "AABB.h"
#include "Rect.h"
#include <vector>
#include <math/MathUtil.h>

namespace KujataEngine {

// 図形仮置き　後々それぞれのヘッダーを分けます
struct Triangle {
	Vector3 vertices[3];
};

struct Spring {
	Vector3 anchor;           // アンカー
	float naturalLength;      // 自然調
	float stiffness;          // 剛性
	float dampingCoefficient; // 減衰係数
};

struct Ball {
	Vector3 position;
	Vector3 velocity;
	Vector3 acceleration;
	float mass;
	float radius;
	unsigned int color;
};

struct Pendulum {
	Vector3 anchor;
	float length;
	float angle;
	float angularVelocity;
	float angularAcceleration;
};

struct ConicalPendulum {
	Vector3 anchor;
	float length;
	float halfApexAngle;
	float angle;
	float angularVelocity;
};

class Segment {
public:
	Vector3 origin; // 始点
	Vector3 diff;   // 終点の差分ベクトル
};

struct Ray {
	Vector3 origin; // 始点
	Vector3 diff;   // 終点の差分ベクトル
};

struct Line {
	Vector3 origin; // 始点
	Vector3 diff;   // 終点の差分ベクトル
};

struct Sphere {
	Vector3 center;
	float radius;
};

// カプセル。spine(線分 p0-p1)から半径radius膨らませた形状。p0==p1のとき球と等価。
struct Capsule {
	Vector3 p0;    // spine端点(片側の球中心)
	Vector3 p1;    // spine端点(反対側の球中心)
	float radius;
};

struct Plane {
	Vector3 normal;
	float distance;
};

// 接触情報。反発応答(押し出し+速度反射)のための最小データ。
//   normal : 第1引数から第2引数へ向かう単位分離ベクトル。第1形状は -normal、第2形状は +normal 方向へ押し出す。
//   depth  : めり込み量(>=0)。
//   point  : おおよその接触点(ワールド座標)。
struct Contact {
	Vector3 normal{};
	float depth = 0.0f;
	Vector3 point{};
};

namespace ShapeUtil {

KUJATA_API bool IsCollision(const Sphere& sphere, const Plane& plane);

KUJATA_API bool IsCollision(const Segment& line, const Plane& plane);

KUJATA_API bool IsCollision(const Segment& segment, const Triangle& triangle);

KUJATA_API bool IsCollision(const AABB& aabb1, const AABB& aabb2);

KUJATA_API bool IsCollision(const AABB& aabb, const Sphere& sphere);

KUJATA_API bool IsCollision(const AABB& aabb, const Vector3& point);

KUJATA_API bool IsCollision(const AABB& aabb, const Segment& segment);

KUJATA_API bool IsCollision(const AABB& aabb, const Line& line);

KUJATA_API bool IsCollision(const AABB& aabb, const Ray& ray);

KUJATA_API bool IsCollision(const OBB& obb, const Sphere& sphere);

KUJATA_API bool IsCollision(const OBB& obb, const Segment& segment);

KUJATA_API bool IsCollision(const OBB& obb, const Line& line);

KUJATA_API bool IsCollision(const OBB& obb, const Ray& ray);

KUJATA_API bool IsCollision(const OBB& obb1, const OBB& obb2);

KUJATA_API bool IsCollision(const Sphere& a, const Sphere& b);

KUJATA_API bool IsCollision(const Capsule& capsule, const Sphere& sphere);

KUJATA_API bool IsCollision(const Capsule& a, const Capsule& b);

KUJATA_API bool IsCollision(const Capsule& capsule, const OBB& obb);

/// <summary>
/// 軸が重なっているかどうか
/// </summary>
KUJATA_API bool IsOverlappingOnAxis(const OBB& A, const OBB& B, const Vector3& axis);

// 線分(origin〜origin+diff)と形状の交差を調べ、最初のヒット位置の割合t[0,1]を返す。
// 始点が形状内部の場合は t=0 でヒット扱い。カメラの遮蔽判定などに使う。
KUJATA_API bool RaycastSegment(const Segment& segment, const Sphere& sphere, float& outT);
KUJATA_API bool RaycastSegment(const Segment& segment, const AABB& aabb, float& outT);
KUJATA_API bool RaycastSegment(const Segment& segment, const OBB& obb, float& outT);
KUJATA_API bool RaycastSegment(const Segment& segment, const Capsule& capsule, float& outT);

// 接触情報(法線・めり込み量・接触点)を計算する。交差していれば true。
// normal は第1引数から第2引数へ向かう分離方向(単位)。詳細は Contact を参照。
KUJATA_API bool ComputeContact(const Sphere& a, const Sphere& b, Contact& out);
KUJATA_API bool ComputeContact(const Sphere& sphere, const OBB& obb, Contact& out);
KUJATA_API bool ComputeContact(const OBB& a, const OBB& b, Contact& out);
// normal は capsule から相手へ向かう分離方向(単位)。
KUJATA_API bool ComputeContact(const Capsule& capsule, const Sphere& sphere, Contact& out);
KUJATA_API bool ComputeContact(const Capsule& a, const Capsule& b, Contact& out);
KUJATA_API bool ComputeContact(const Capsule& capsule, const OBB& obb, Contact& out);

KUJATA_API Vector3 Reflect(const Vector3& input, const Vector3& normal);

///< summary>
/// CatmullRom補間
///</summary>
///< param name="po">点0の座標</param>
///< param name="p1">点1の座標</param>
///< param name="p2">点2の座標</param>
///< param name="p3">点3の座標</param>
///< param name="t">点1を0.0f、点2を1.0fとした割合指定</param>
///< returns>点1と点2の間で指定された座標</returns>
KUJATA_API Vector3 CatmullRomInterpolation(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t);

///< summary>
/// CatmullRomスプライン曲線上の座標を得る
///  </summary>
///< param name="points">制御点の集合</param>
/// <param name="t">スプラインの全区間の中での割合指定[0,1]</param>
///< returns>座標</returns>
KUJATA_API Vector3 CatmullRomPosition(const std::vector<Vector3>& points, float t);

KUJATA_API Segment MakeLimitedSegment(const Vector3& start, const Vector3& end, float maxDistance);
KUJATA_API Segment MakeNattoSegment(const Vector3& start, const Vector3& end, float maxDistance, float minDistance);

} // namespace ShapeUtil

} // namespace KujataEngine