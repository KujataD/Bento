#include "ShapeUtil.h"
#include <algorithm>
#include <cassert>

namespace KujataEngine {
namespace ShapeUtil {

bool IsCollision(const Sphere& sphere, const Plane& plane) {
	float k = std::abs(Dot(plane.normal, sphere.center) - plane.distance);
	return (k <= sphere.radius);
}

bool IsCollision(const Segment& segment, const Plane& plane) {
	float dot = Dot(plane.normal, segment.diff);

	if (dot == 0.0f) {
		return false;
	}

	float t = (plane.distance - Dot(segment.origin, plane.normal)) / dot;

	return (t >= 0.0f && t <= 1.0f);
}

bool IsCollision(const Segment& segment, const Triangle& triangle) {
	Vector3 normal = Normalize(Cross(triangle.vertices[1] - triangle.vertices[0], triangle.vertices[2] - triangle.vertices[0]));

	float dot = Dot(normal, segment.diff);

	if (dot == 0.0f) {
		return false;
	}

	float d = Dot(normal, triangle.vertices[0]);
	float t = (d - Dot(segment.origin, normal)) / dot;

	Vector3 p = segment.origin + segment.diff * t;

	Vector3 v01 = triangle.vertices[1] - triangle.vertices[0];
	Vector3 v12 = triangle.vertices[2] - triangle.vertices[1];
	Vector3 v20 = triangle.vertices[0] - triangle.vertices[2];
	Vector3 v0p = p - triangle.vertices[0];
	Vector3 v1p = p - triangle.vertices[1];
	Vector3 v2p = p - triangle.vertices[2];
	Vector3 cross01 = Cross(v01, v1p);
	Vector3 cross12 = Cross(v12, v2p);
	Vector3 cross20 = Cross(v20, v0p);

	bool resultA = (Dot(cross01, normal) >= 0.0f && Dot(cross12, normal) >= 0.0f && Dot(cross20, normal) >= 0.0f);
	bool resultB = (t >= 0.0f && t <= 1.0f);

	return (resultA && resultB);
}

bool IsCollision(const AABB& aabb1, const AABB& aabb2) {
	bool resultX = (aabb1.min.x <= aabb2.max.x && aabb1.max.x >= aabb2.min.x);
	bool resultY = (aabb1.min.y <= aabb2.max.y && aabb1.max.y >= aabb2.min.y);
	bool resultZ = (aabb1.min.z <= aabb2.max.z && aabb1.max.z >= aabb2.min.z);

	return (resultX && resultY && resultZ);
}

bool IsCollision(const AABB& aabb, const Sphere& sphere) {
	// 最近接点を求める
	Vector3 closestPoint{
	    std::clamp(sphere.center.x, aabb.min.x, aabb.max.x),
	    std::clamp(sphere.center.y, aabb.min.y, aabb.max.y),
	    std::clamp(sphere.center.z, aabb.min.z, aabb.max.z),
	};

	// 最近接点と球の中心との距離を求める
	float distance = Length(closestPoint - sphere.center);

	// 距離が半径よりも小さければ衝突
	return (distance <= sphere.radius);
}

bool IsCollision(const AABB& aabb, const Vector3& point) {
	bool resultX = (aabb.min.x <= point.x && aabb.max.x >= point.x);
	bool resultY = (aabb.min.y <= point.y && aabb.max.y >= point.y);
	bool resultZ = (aabb.min.z <= point.z && aabb.max.z >= point.z);

	return (resultX && resultY && resultZ);
}

bool IsCollision(const AABB& aabb, const Segment& segment) {
	float txMin = (aabb.min.x - segment.origin.x) / segment.diff.x;
	float txMax = (aabb.max.x - segment.origin.x) / segment.diff.x;
	float tyMin = (aabb.min.y - segment.origin.y) / segment.diff.y;
	float tyMax = (aabb.max.y - segment.origin.y) / segment.diff.y;
	float tzMin = (aabb.min.z - segment.origin.z) / segment.diff.z;
	float tzMax = (aabb.max.z - segment.origin.z) / segment.diff.z;

	float tNearX = (std::min)(txMin, txMax);
	float tNearY = (std::min)(tyMin, tyMax);
	float tNearZ = (std::min)(tzMin, tzMax);
	float tFarX = (std::max)(txMin, txMax);
	float tFarY = (std::max)(tyMin, tyMax);
	float tFarZ = (std::max)(tzMin, tzMax);

	// AABBとの衝突点(貫通点)のtが小さい方
	float tmin = (std::max)((std::max)(tNearX, tNearY), tNearZ);
	// AABBとの衝突点(貫通点)のtが大きい方
	float tmax = (std::min)((std::min)(tFarX, tFarY), tFarZ);

	// Segment
	if (tmin <= tmax) {
		if (tmax >= 0.0f && tmin <= 1.0f) {
			return true;
		}
	}
	return false;
}

bool IsCollision(const AABB& aabb, const Line& line) {
	float txMin = (aabb.min.x - line.origin.x) / line.diff.x;
	float txMax = (aabb.max.x - line.origin.x) / line.diff.x;
	float tyMin = (aabb.min.y - line.origin.y) / line.diff.y;
	float tyMax = (aabb.max.y - line.origin.y) / line.diff.y;
	float tzMin = (aabb.min.z - line.origin.z) / line.diff.z;
	float tzMax = (aabb.max.z - line.origin.z) / line.diff.z;

	float tNearX = (std::min)(txMin, txMax);
	float tNearY = (std::min)(tyMin, tyMax);
	float tNearZ = (std::min)(tzMin, tzMax);
	float tFarX = (std::max)(txMin, txMax);
	float tFarY = (std::max)(tyMin, tyMax);
	float tFarZ = (std::max)(tzMin, tzMax);

	// AABBとの衝突点(貫通点)のtが小さい方
	float tmin = (std::max)((std::max)(tNearX, tNearY), tNearZ);
	// AABBとの衝突点(貫通点)のtが大きい方
	float tmax = (std::min)((std::min)(tFarX, tFarY), tFarZ);

	// Line
	if (tmin <= tmax) {
		return true;
	}

	return false;
}

bool IsCollision(const AABB& aabb, const Ray& ray) {
	float txMin = (aabb.min.x - ray.origin.x) / ray.diff.x;
	float txMax = (aabb.max.x - ray.origin.x) / ray.diff.x;
	float tyMin = (aabb.min.y - ray.origin.y) / ray.diff.y;
	float tyMax = (aabb.max.y - ray.origin.y) / ray.diff.y;
	float tzMin = (aabb.min.z - ray.origin.z) / ray.diff.z;
	float tzMax = (aabb.max.z - ray.origin.z) / ray.diff.z;

	float tNearX = (std::min)(txMin, txMax);
	float tNearY = (std::min)(tyMin, tyMax);
	float tNearZ = (std::min)(tzMin, tzMax);
	float tFarX = (std::max)(txMin, txMax);
	float tFarY = (std::max)(tyMin, tyMax);
	float tFarZ = (std::max)(tzMin, tzMax);

	// AABBとの衝突点(貫通点)のtが小さい方
	float tmin = (std::max)((std::max)(tNearX, tNearY), tNearZ);
	// AABBとの衝突点(貫通点)のtが大きい方
	float tmax = (std::min)((std::min)(tFarX, tFarY), tFarZ);

	// Ray
	if (tmin <= tmax) {
		if (tmax >= 0.0f) {
			return true;
		}
	}

	return false;
}

bool IsCollision(const OBB& obb, const Sphere& sphere) {
	Matrix4x4 obbWorldMatrix = obb.GetWorldMatrix();
	Matrix4x4 obbWorldMatrixInverse = Inverse(obbWorldMatrix);
	Vector3 centerInOBBLocalSpace = Transform(sphere.center, obbWorldMatrixInverse);

	AABB aabbOBBLocal{.min = -obb.size, .max = obb.size};
	Sphere sphereOBBLocal{centerInOBBLocalSpace, sphere.radius};
	// ローカル空間で衝突判定
	return IsCollision(aabbOBBLocal, sphereOBBLocal);
}

bool IsCollision(const OBB& obb, const Segment& segment) {
	Matrix4x4 obbWorldMatrix = obb.GetWorldMatrix();
	Matrix4x4 obbWorldMatrixInverse = Inverse(obbWorldMatrix);

	Vector3 localOrigin = Transform(segment.origin, obbWorldMatrixInverse);
	Vector3 localEnd = Transform(segment.origin + segment.diff, obbWorldMatrixInverse);

	AABB localAABB{
	    -obb.size,
	    obb.size,
	};

	Segment localSegment;
	localSegment.origin = localOrigin;
	localSegment.diff = localEnd - localOrigin;
	return IsCollision(localAABB, localSegment);
}

bool IsCollision(const OBB& obb, const Line& line) {
	Matrix4x4 obbWorldMatrix = obb.GetWorldMatrix();
	Matrix4x4 obbWorldMatrixInverse = Inverse(obbWorldMatrix);

	Vector3 localOrigin = Transform(line.origin, obbWorldMatrixInverse);
	Vector3 localEnd = Transform(line.origin + line.diff, obbWorldMatrixInverse);

	AABB localAABB{
	    -obb.size,
	    obb.size,
	};

	Line localLine;
	localLine.origin = localOrigin;
	localLine.diff = localEnd - localOrigin;
	return IsCollision(localAABB, localLine);
}

bool IsCollision(const OBB& obb, const Ray& ray) {
	Matrix4x4 obbWorldMatrix = obb.GetWorldMatrix();
	Matrix4x4 obbWorldMatrixInverse = Inverse(obbWorldMatrix);

	Vector3 localOrigin = Transform(ray.origin, obbWorldMatrixInverse);
	Vector3 localEnd = Transform(ray.origin + ray.diff, obbWorldMatrixInverse);

	AABB localAABB{
	    -obb.size,
	    obb.size,
	};

	Ray localRay;
	localRay.origin = localOrigin;
	localRay.diff = localEnd - localOrigin;
	return IsCollision(localAABB, localRay);
}

bool IsOverlappingOnAxis(const OBB& obb1, const OBB& obb2, const Vector3& axis) {
	Vector3 L = Normalize(axis);

	// 全てワールド座標からの Dot で統一する
	auto GetProjection = [&](const OBB& obb) {
		float center = Dot(obb.center, L);
		float extent = std::abs(Dot(obb.orientations[0], L)) * obb.size.x + std::abs(Dot(obb.orientations[1], L)) * obb.size.y + std::abs(Dot(obb.orientations[2], L)) * obb.size.z;
		return std::make_pair(center - extent, center + extent);
	};

	auto [min1, max1] = GetProjection(obb1);
	auto [min2, max2] = GetProjection(obb2);

	float sumSpan = (max1 - min1) + (max2 - min2);
	float longSpan = (std::max)(max1, max2) - (std::min)(min1, min2);

	return sumSpan >= longSpan;
}

bool IsCollision(const OBB& obb1, const OBB& obb2) {
	if (!IsOverlappingOnAxis(obb1, obb2, obb1.orientations[0])) {
		return false;
	}
	if (!IsOverlappingOnAxis(obb1, obb2, obb1.orientations[1])) {
		return false;
	}
	if (!IsOverlappingOnAxis(obb1, obb2, obb1.orientations[2])) {
		return false;
	}

	if (!IsOverlappingOnAxis(obb1, obb2, obb2.orientations[0])) {
		return false;
	}
	if (!IsOverlappingOnAxis(obb1, obb2, obb2.orientations[1])) {
		return false;
	}
	if (!IsOverlappingOnAxis(obb1, obb2, obb2.orientations[2])) {
		return false;
	}

	Vector3 c;

	c = Cross(obb1.orientations[0], obb2.orientations[0]);
	if (!IsOverlappingOnAxis(obb1, obb2, c)) {
		return false;
	}

	c = Cross(obb1.orientations[0], obb2.orientations[1]);
	if (!IsOverlappingOnAxis(obb1, obb2, c)) {
		return false;
	}

	c = Cross(obb1.orientations[0], obb2.orientations[2]);
	if (!IsOverlappingOnAxis(obb1, obb2, c)) {
		return false;
	}

	c = Cross(obb1.orientations[1], obb2.orientations[0]);
	if (!IsOverlappingOnAxis(obb1, obb2, c)) {
		return false;
	}

	c = Cross(obb1.orientations[1], obb2.orientations[1]);
	if (!IsOverlappingOnAxis(obb1, obb2, c)) {
		return false;
	}

	c = Cross(obb1.orientations[1], obb2.orientations[2]);
	if (!IsOverlappingOnAxis(obb1, obb2, c)) {
		return false;
	}

	c = Cross(obb1.orientations[2], obb2.orientations[0]);
	if (!IsOverlappingOnAxis(obb1, obb2, c)) {
		return false;
	}

	c = Cross(obb1.orientations[2], obb2.orientations[1]);
	if (!IsOverlappingOnAxis(obb1, obb2, c)) {
		return false;
	}

	c = Cross(obb1.orientations[2], obb2.orientations[2]);
	if (!IsOverlappingOnAxis(obb1, obb2, c)) {
		return false;
	}

	return true;
}

bool IsCollision(const Sphere& a, const Sphere& b) { return powf(b.center.x - a.center.x, 2) + powf(b.center.y - a.center.y, 2) + powf(b.center.z - a.center.z, 2) <= powf(b.radius + a.radius, 2); }

namespace {

// 線分[p0,p1]上で point に最も近い点を返す。
Vector3 ClosestPointOnSegment(const Vector3& p0, const Vector3& p1, const Vector3& point) {
	Vector3 ab = p1 - p0;
	float abLenSq = Dot(ab, ab);
	if (abLenSq <= 1e-12f) {
		return p0;
	}
	float t = Dot(point - p0, ab) / abLenSq;
	t = std::clamp(t, 0.0f, 1.0f);
	return p0 + ab * t;
}

// 2線分間の最近点対を求める(Ericson, Real-Time Collision Detection)。
// c1: 線分[p1,q1]上、c2: 線分[p2,q2]上。
void ClosestPointsBetweenSegments(const Vector3& p1, const Vector3& q1, const Vector3& p2, const Vector3& q2, Vector3& c1, Vector3& c2) {
	Vector3 d1 = q1 - p1;
	Vector3 d2 = q2 - p2;
	Vector3 r = p1 - p2;
	float a = Dot(d1, d1);
	float e = Dot(d2, d2);
	float f = Dot(d2, r);
	const float eps = 1e-12f;

	float s = 0.0f;
	float t = 0.0f;
	if (a <= eps && e <= eps) {
		s = 0.0f;
		t = 0.0f;
	} else if (a <= eps) {
		s = 0.0f;
		t = std::clamp(f / e, 0.0f, 1.0f);
	} else {
		float c = Dot(d1, r);
		if (e <= eps) {
			t = 0.0f;
			s = std::clamp(-c / a, 0.0f, 1.0f);
		} else {
			float b = Dot(d1, d2);
			float denom = a * e - b * b;
			if (denom > eps) {
				s = std::clamp((b * f - c * e) / denom, 0.0f, 1.0f);
			} else {
				s = 0.0f;
			}
			t = (b * s + f) / e;
			if (t < 0.0f) {
				t = 0.0f;
				s = std::clamp(-c / a, 0.0f, 1.0f);
			} else if (t > 1.0f) {
				t = 1.0f;
				s = std::clamp((b - c) / a, 0.0f, 1.0f);
			}
		}
	}

	c1 = p1 + d1 * s;
	c2 = p2 + d2 * t;
}

} // namespace

bool IsCollision(const Capsule& capsule, const Sphere& sphere) {
	Vector3 closest = ClosestPointOnSegment(capsule.p0, capsule.p1, sphere.center);
	return IsCollision(Sphere{closest, capsule.radius}, sphere);
}

bool IsCollision(const Capsule& a, const Capsule& b) {
	Vector3 c1{};
	Vector3 c2{};
	ClosestPointsBetweenSegments(a.p0, a.p1, b.p0, b.p1, c1, c2);
	return IsCollision(Sphere{c1, a.radius}, Sphere{c2, b.radius});
}

bool IsCollision(const Capsule& capsule, const OBB& obb) {
	// 近似: OBB中心にもっとも近いspine上の点を球とみなしてOBBと判定する。
	Vector3 closest = ClosestPointOnSegment(capsule.p0, capsule.p1, obb.center);
	return IsCollision(obb, Sphere{closest, capsule.radius});
}

bool RaycastSegment(const Segment& segment, const Sphere& sphere, float& outT) {
	Vector3 m = segment.origin - sphere.center;
	float c = Dot(m, m) - sphere.radius * sphere.radius;
	if (c <= 0.0f) {
		// 始点が球の内部。
		outT = 0.0f;
		return true;
	}

	float a = Dot(segment.diff, segment.diff);
	if (a <= 1.0e-8f) {
		return false;
	}
	float b = Dot(m, segment.diff);
	if (b > 0.0f) {
		// 球から遠ざかる向き。
		return false;
	}

	float discriminant = b * b - a * c;
	if (discriminant < 0.0f) {
		return false;
	}

	float t = (-b - std::sqrt(discriminant)) / a;
	if (t < 0.0f || t > 1.0f) {
		return false;
	}
	outT = t;
	return true;
}

bool RaycastSegment(const Segment& segment, const AABB& aabb, float& outT) {
	// slab法。始点が内部なら tMin=0 のままヒットになる。
	float tMin = 0.0f;
	float tMax = 1.0f;

	const float origins[3] = {segment.origin.x, segment.origin.y, segment.origin.z};
	const float diffs[3] = {segment.diff.x, segment.diff.y, segment.diff.z};
	const float mins[3] = {aabb.min.x, aabb.min.y, aabb.min.z};
	const float maxs[3] = {aabb.max.x, aabb.max.y, aabb.max.z};

	for (int axis = 0; axis < 3; ++axis) {
		if (std::abs(diffs[axis]) < 1.0e-8f) {
			if (origins[axis] < mins[axis] || origins[axis] > maxs[axis]) {
				return false;
			}
			continue;
		}

		float inverse = 1.0f / diffs[axis];
		float t1 = (mins[axis] - origins[axis]) * inverse;
		float t2 = (maxs[axis] - origins[axis]) * inverse;
		if (t1 > t2) {
			std::swap(t1, t2);
		}
		tMin = (std::max)(tMin, t1);
		tMax = (std::min)(tMax, t2);
		if (tMin > tMax) {
			return false;
		}
	}

	outT = tMin;
	return true;
}

bool RaycastSegment(const Segment& segment, const OBB& obb, float& outT) {
	// 線分をOBBローカル空間へ変換して、AABBのslab法に帰着する。
	Vector3 relative = segment.origin - obb.center;
	Segment localSegment{};
	localSegment.origin = {Dot(relative, obb.orientations[0]), Dot(relative, obb.orientations[1]), Dot(relative, obb.orientations[2])};
	localSegment.diff = {Dot(segment.diff, obb.orientations[0]), Dot(segment.diff, obb.orientations[1]), Dot(segment.diff, obb.orientations[2])};

	AABB localBox{};
	localBox.min = {-obb.size.x, -obb.size.y, -obb.size.z};
	localBox.max = {obb.size.x, obb.size.y, obb.size.z};
	return RaycastSegment(localSegment, localBox, outT);
}

bool RaycastSegment(const Segment& segment, const Capsule& capsule, float& outT) {
	// カプセル = spine線分[p0,p1]から半径radius膨らませた形状。
	// 「無限円柱の側面(spine範囲内)」と「両端の球」の最も手前のヒットを採用する。
	const float radius = capsule.radius;

	// 始点がカプセル内部なら即ヒット(めり込み扱い)。
	Vector3 startClosest = ClosestPointOnSegment(capsule.p0, capsule.p1, segment.origin);
	if (Dot(segment.origin - startClosest, segment.origin - startClosest) <= radius * radius) {
		outT = 0.0f;
		return true;
	}

	float bestT = 1.0f;
	bool hit = false;

	// --- 円柱側面(spine軸に沿った本体部分) ---
	Vector3 d = capsule.p1 - capsule.p0; // spine方向(非正規化)
	Vector3 m = segment.origin - capsule.p0;
	Vector3 n = segment.diff;
	float dd = Dot(d, d);

	if (dd > 1.0e-12f) {
		float nd = Dot(n, d);
		float md = Dot(m, d);
		float nn = Dot(n, n);
		float mn = Dot(m, n);
		float mm = Dot(m, m);

		// (|w|^2)dd - (w·d)^2 = r^2 dd を t の2次方程式に展開(w = m + t n)。
		float a = nn * dd - nd * nd;
		float b = mn * dd - md * nd;
		float c = mm * dd - md * md - radius * radius * dd;

		if (std::abs(a) > 1.0e-8f) {
			float discriminant = b * b - a * c;
			if (discriminant >= 0.0f) {
				float sqrtDisc = std::sqrt(discriminant);
				// 手前側の根から順に試す。
				float roots[2] = {(-b - sqrtDisc) / a, (-b + sqrtDisc) / a};
				for (float t : roots) {
					if (t < 0.0f || t > 1.0f || t >= bestT) {
						continue;
					}
					// spine上の投影位置が[0,1](端点の内側)かを確認する。
					float axial = (md + t * nd) / dd;
					if (axial < 0.0f || axial > 1.0f) {
						continue;
					}
					bestT = t;
					hit = true;
					break;
				}
			}
		}
	}

	// --- 両端の半球(端点を中心とした球はカプセルの一部) ---
	float capT = 0.0f;
	if (RaycastSegment(segment, Sphere{capsule.p0, radius}, capT) && capT < bestT) {
		bestT = capT;
		hit = true;
	}
	if (RaycastSegment(segment, Sphere{capsule.p1, radius}, capT) && capT < bestT) {
		bestT = capT;
		hit = true;
	}

	if (hit) {
		outT = bestT;
		return true;
	}
	return false;
}

bool ComputeContact(const Capsule& capsule, const Sphere& sphere, Contact& out) {
	Vector3 closest = ClosestPointOnSegment(capsule.p0, capsule.p1, sphere.center);
	// capsule表面の球(closest,radius)と相手球の球-球接触。normal は closest→sphere = capsule→sphere。
	return ComputeContact(Sphere{closest, capsule.radius}, sphere, out);
}

bool ComputeContact(const Capsule& a, const Capsule& b, Contact& out) {
	Vector3 c1{};
	Vector3 c2{};
	ClosestPointsBetweenSegments(a.p0, a.p1, b.p0, b.p1, c1, c2);
	// normal は c1→c2 = a→b。
	return ComputeContact(Sphere{c1, a.radius}, Sphere{c2, b.radius}, out);
}

bool ComputeContact(const Capsule& capsule, const OBB& obb, Contact& out) {
	// 近似: OBB中心にもっとも近いspine上の点を球とみなしてOBBと接触計算する。
	// ComputeContact(Sphere,OBB) の normal は sphere→obb = capsule→obb。
	Vector3 closest = ClosestPointOnSegment(capsule.p0, capsule.p1, obb.center);
	return ComputeContact(Sphere{closest, capsule.radius}, obb, out);
}

bool ComputeContact(const Sphere& a, const Sphere& b, Contact& out) {
	Vector3 d = b.center - a.center;
	float dist = Length(d);
	float sumR = a.radius + b.radius;
	if (dist > sumR) {
		return false;
	}
	// 中心一致の退化時は任意軸を採用
	Vector3 n = (dist > 1e-6f) ? d * (1.0f / dist) : Vector3{1.0f, 0.0f, 0.0f};
	out.normal = n;
	out.depth = sumR - dist;
	out.point = a.center + n * (a.radius - out.depth * 0.5f);
	return true;
}

bool ComputeContact(const Sphere& sphere, const OBB& obb, Contact& out) {
	Vector3 d = sphere.center - obb.center;
	float ext[3] = {obb.size.x, obb.size.y, obb.size.z};

	// OBB 上の最近点をローカル軸ごとの射影クランプで求める
	float proj[3];
	Vector3 closest = obb.center;
	for (int i = 0; i < 3; ++i) {
		proj[i] = Dot(d, obb.orientations[i]);
		float c = std::clamp(proj[i], -ext[i], ext[i]);
		closest = closest + obb.orientations[i] * c;
	}

	Vector3 v = sphere.center - closest; // OBB表面 → 球中心
	float distSq = Dot(v, v);
	float r = sphere.radius;

	if (distSq > r * r) {
		return false; // 球中心が OBB 外かつ半径外(中心が内部なら distSq==0 で下へ)
	}

	float dist = std::sqrt(distSq);
	if (dist > 1e-6f) {
		// 球中心が OBB 外(または表面): normal は sphere→obb = -(OBB→球)
		Vector3 obbToSphere = v * (1.0f / dist);
		out.normal = obbToSphere * -1.0f;
		out.depth = r - dist;
		out.point = closest;
	} else {
		// 球中心が OBB 内部: めり込み最小の面へ押し出す
		int minAxis = 0;
		float minPen = 1e30f;
		float sign = 1.0f;
		for (int i = 0; i < 3; ++i) {
			float pen = ext[i] - std::abs(proj[i]); // 内部なら >=0
			if (pen < minPen) {
				minPen = pen;
				minAxis = i;
				sign = (proj[i] >= 0.0f) ? 1.0f : -1.0f;
			}
		}
		Vector3 faceNormal = obb.orientations[minAxis] * sign; // OBB中心→球のある面側
		out.normal = faceNormal * -1.0f;                       // sphere→obb は逆向き
		out.depth = r + minPen;
		out.point = sphere.center - faceNormal * minPen;
	}
	return true;
}

bool ComputeContact(const OBB& a, const OBB& b, Contact& out) {
	// SAT の 15 軸(各3面法線 + 9エッジ外積)から、最小めり込み軸(MTV)を求める
	Vector3 axes[15];
	int n = 0;
	for (int i = 0; i < 3; ++i) {
		axes[n++] = a.orientations[i];
	}
	for (int i = 0; i < 3; ++i) {
		axes[n++] = b.orientations[i];
	}
	for (int i = 0; i < 3; ++i) {
		for (int j = 0; j < 3; ++j) {
			axes[n++] = Cross(a.orientations[i], b.orientations[j]);
		}
	}

	auto GetProjection = [](const OBB& o, const Vector3& L) {
		float center = Dot(o.center, L);
		float extent = std::abs(Dot(o.orientations[0], L)) * o.size.x + std::abs(Dot(o.orientations[1], L)) * o.size.y + std::abs(Dot(o.orientations[2], L)) * o.size.z;
		return std::make_pair(center - extent, center + extent);
	};

	float minOverlap = 1e30f;
	Vector3 minAxis{};
	for (int k = 0; k < 15; ++k) {
		float len = Length(axes[k]);
		if (len < 1e-6f) {
			continue; // 平行軸のエッジ外積は退化するのでスキップ
		}
		Vector3 L = axes[k] * (1.0f / len);

		auto [minA, maxA] = GetProjection(a, L);
		auto [minB, maxB] = GetProjection(b, L);

		float overlap = (std::min)(maxA, maxB) - (std::max)(minA, minB);
		if (overlap <= 0.0f) {
			return false; // 分離軸あり
		}
		if (overlap < minOverlap) {
			minOverlap = overlap;
			minAxis = L;
		}
	}

	// normal を a→b 方向へ揃える
	Vector3 dir = b.center - a.center;
	if (Dot(minAxis, dir) < 0.0f) {
		minAxis = minAxis * -1.0f;
	}
	out.normal = minAxis;
	out.depth = minOverlap;
	out.point = (a.center + b.center) * 0.5f; // 近似接触点
	return true;
}

Vector3 Reflect(const Vector3& input, const Vector3& normal) { return input - normal * (2.0f * Dot(input, normal)); }

Vector3 CatmullRomInterpolation(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t) {
	const float s = 0.5f;

	float t2 = t * t;
	float t3 = t2 * t;

	Vector3 e3 = -p0 + 3 * p1 - 3 * p2 + p3;
	Vector3 e2 = 2 * p0 - 5 * p1 + 4 * p2 - p3;
	Vector3 e1 = -p0 + p2;
	Vector3 e0 = 2 * p1;

	return s * (e3 * t3 + e2 * t2 + e1 * t + e0);
}

Vector3 CatmullRomPosition(const std::vector<Vector3>& points, float t) {
	assert(points.size() >= 4 && "制御点は4点以上必要です");

	// 区間数は制御点の数-1
	size_t division = points.size() - 1;
	// 1区間の長さ(全体を1.0とした割合)
	float areaWidth = 1.0f / division;

	// 区間内の始点を0.0f、終点を1.0fとしたときの現在位置
	float t_2 = std::fmod(t, areaWidth) * division;
	// 下限(0.0f)と上限(1.0f)の範囲に収める
	t_2 = std::clamp(t_2, 0.0f, 1.0f);

	// 区間番号
	size_t index = static_cast<size_t>(t / areaWidth);
	// 区間番号が上限を超えないように収める
	index = std::min<size_t>(index, division - 1);

	// 4点分のインデックス
	size_t index0 = index - 1;
	size_t index1 = index;
	size_t index2 = index + 1;
	size_t index3 = index + 2;

	// 最初の区間のp0はp1を重複使用する
	if (index == 0) {
		index0 = index1;
	}

	// 最後の区間のp3はp2を重複使用する
	if (index3 >= points.size()) {
		index3 = index2;
	}

	// 4点の座標
	const Vector3& p0 = points[index0];
	const Vector3& p1 = points[index1];
	const Vector3& p2 = points[index2];
	const Vector3& p3 = points[index3];

	// 4点を指定してCatmull-Rom補間
	return CatmullRomInterpolation(p0, p1, p2, p3, t_2);
}

Segment MakeLimitedSegment(const Vector3& start, const Vector3& end, float maxDistance) {
	Vector3 toEnd = end - start;

	Segment segment{};
	segment.origin = start;

	if (Length(toEnd) <= maxDistance) {
		segment.diff = toEnd;
	} else {
		Vector3 direction = Normalize(toEnd);
		segment.diff = direction * (maxDistance / (Length(toEnd))) * maxDistance;
	}

	return segment;
}

Segment MakeNattoSegment(const Vector3& start, const Vector3& end, float maxDistance, float minDistance) {
	Vector3 toEnd = end - start;

	Segment segment{};
	segment.origin = start;

	if (Length(toEnd) <= maxDistance) {
		segment.diff = toEnd;
	} else {
		Vector3 direction = Normalize(toEnd);
		segment.diff = direction * (maxDistance / Length(toEnd) * 4.0f);
	}

	return segment;
}

} // namespace ShapeUtil

} // namespace KujataEngine
