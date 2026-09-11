#pragma once

#include <KujataEngine.h>
#include <algorithm>
#include <cmath>

/// <summary>
/// ガーディアンのリグ計算で使う小道具。
///
/// エンジンの Dot/Cross/Lerp は KUJATA_API が付いておらずGameModule(DLL)からリンクできないため、
/// ここでinline実装を持つ。Length/Normalizeはエクスポート済み、Quaternionは全関数ヘッダinlineなので
/// そちらはエンジンのものをそのまま使う。
/// </summary>
namespace GuardianRigMath {

using KujataEngine::GameObject;
using KujataEngine::Quaternion;
using KujataEngine::Vector3;

// 名前に3を付けているのは、引数がKujataEngine::Vector3なのでADLで KujataEngine::Dot/Cross/Lerp が
// 候補に入り、同名だと呼び出しが曖昧になる(C2668)ため。
inline float Dot3(const Vector3& a, const Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Vector3 Cross3(const Vector3& a, const Vector3& b) {
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline Vector3 Lerp3(const Vector3& a, const Vector3& b, float t) {
	return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

/// <summary>vがほぼゼロなら fallback を、そうでなければ正規化したvを返します。</summary>
inline Vector3 SafeNormalize(const Vector3& v, const Vector3& fallback = {0.0f, 0.0f, 1.0f}) {
	float length = KujataEngine::Length(v);
	if (length <= 1.0e-6f) {
		return fallback;
	}
	return {v.x / length, v.y / length, v.z / length};
}

/// <summary>vに直交する適当な単位ベクトルを1本返します(曲げ平面が潰れた時の逃げ道)。</summary>
inline Vector3 AnyPerpendicular(const Vector3& v) {
	// vと最も平行でない基本軸を選んで外積すると、必ず有効な直交ベクトルが得られる。
	Vector3 axis = (std::abs(v.y) < 0.9f) ? Vector3{0.0f, 1.0f, 0.0f} : Vector3{1.0f, 0.0f, 0.0f};
	return SafeNormalize(Cross3(axis, v));
}

// --- 接地判定用の下向きレイ ---
//
// ShapeUtil::RaycastSegment も同じことをするが KUJATA_API が付いておらずGameModule(DLL)から
// リンクできないため、「真下(-Y)向き」の場合だけを形状ごとにここへ持つ。
//
// 共通の約束:
//   - 返す距離は「レイ原点から最初に当たる面まで」で、常に 0 以上。
//   - **原点が形状の内部にある場合は当たりとみなさない。**
//     これを当たり(距離0)にすると、背の高いコライダーの脇に立ったときに
//     足が rayUp ぶん真上へ跳ね上がる。原点は必ず足より上に置くので、
//     内部にいる = その形状の天面は足より上、つまり足場ではない。

/// <summary>真下レイと球の交差。</summary>
inline bool RaycastDownSphere(const Vector3& origin, float maxDistance, const KujataEngine::Sphere& sphere, float& outDistance) {
	float dx = origin.x - sphere.center.x;
	float dz = origin.z - sphere.center.z;
	float horizontalSq = dx * dx + dz * dz;
	float radiusSq = sphere.radius * sphere.radius;
	if (horizontalSq > radiusSq) {
		return false;
	}

	// 垂直線が球を貫く上側の交点。
	float topY = sphere.center.y + std::sqrt((std::max)(radiusSq - horizontalSq, 0.0f));
	float distance = origin.y - topY;
	if (distance < 0.0f || distance > maxDistance) {
		return false;
	}

	outDistance = distance;
	return true;
}

/// <summary>
/// 真下レイとOBBの交差。レイをOBBのローカル空間へ移してスラブ法で解く。
/// AABBで代用すると、傾いた箱では「一番高い角の高さ」に足が浮いてしまう。
/// </summary>
inline bool RaycastDownObb(const Vector3& origin, float maxDistance, const KujataEngine::OBB& obb, float& outDistance) {
	Vector3 offset = origin - obb.center;

	// orientations[] はワールドでの各軸。内積でローカル成分へ落とす。
	Vector3 localOrigin = {
	    Dot3(offset, obb.orientations[0]),
	    Dot3(offset, obb.orientations[1]),
	    Dot3(offset, obb.orientations[2]),
	};
	Vector3 down = {0.0f, -1.0f, 0.0f};
	Vector3 localDirection = {
	    Dot3(down, obb.orientations[0]),
	    Dot3(down, obb.orientations[1]),
	    Dot3(down, obb.orientations[2]),
	};

	const float halfExtents[3] = {obb.size.x, obb.size.y, obb.size.z};
	const float originAxis[3] = {localOrigin.x, localOrigin.y, localOrigin.z};
	const float directionAxis[3] = {localDirection.x, localDirection.y, localDirection.z};

	float entry = 0.0f;
	float exit = maxDistance;

	for (int axis = 0; axis < 3; ++axis) {
		if (std::abs(directionAxis[axis]) < 1.0e-6f) {
			// この軸に平行。スラブの外なら永久に当たらない。
			if (std::abs(originAxis[axis]) > halfExtents[axis]) {
				return false;
			}
			continue;
		}

		float inverse = 1.0f / directionAxis[axis];
		float t0 = (-halfExtents[axis] - originAxis[axis]) * inverse;
		float t1 = (halfExtents[axis] - originAxis[axis]) * inverse;
		if (t0 > t1) {
			std::swap(t0, t1);
		}
		entry = (std::max)(entry, t0);
		exit = (std::min)(exit, t1);
		if (entry > exit) {
			return false;
		}
	}

	// entry が 0 のままなら原点が箱の内部。天面が足より上なので足場にしない。
	if (entry <= 0.0f) {
		return false;
	}

	outDistance = entry;
	return true;
}

/// <summary>
/// 真下レイとカプセルの交差。無限円柱との交点を求めてspineの範囲で判定し、
/// 範囲外なら近い側の半球(端点の球)へ落とす。
/// </summary>
inline bool RaycastDownCapsule(const Vector3& origin, float maxDistance, const KujataEngine::Capsule& capsule, float& outDistance) {
	Vector3 axis = capsule.p1 - capsule.p0;
	Vector3 toOrigin = origin - capsule.p0;
	Vector3 down = {0.0f, -1.0f, 0.0f};

	float axisSq = Dot3(axis, axis);
	if (axisSq <= 1.0e-8f) {
		// spineが潰れている = 球と等価。
		return RaycastDownSphere(origin, maxDistance, KujataEngine::Sphere{capsule.p0, capsule.radius}, outDistance);
	}

	float axisDotDown = Dot3(axis, down);
	float axisDotOrigin = Dot3(axis, toOrigin);

	// 原点がカプセル内部なら足場ではない(他の形状と同じ約束)。
	// 端点の球へフォールバックする前にここで弾かないと、円柱部分の内側にいるとき
	// 下側の半球の天面を拾って足が沈む。
	float alongOrigin = std::clamp(axisDotOrigin / axisSq, 0.0f, 1.0f);
	Vector3 closest = capsule.p0 + axis * alongOrigin;
	if (KujataEngine::Length(origin - closest) < capsule.radius) {
		return false;
	}

	float a = axisSq - axisDotDown * axisDotDown;
	float b = axisSq * Dot3(toOrigin, down) - axisDotOrigin * axisDotDown;
	float c = axisSq * Dot3(toOrigin, toOrigin) - axisDotOrigin * axisDotOrigin - capsule.radius * capsule.radius * axisSq;

	// 円柱部分。aが0に近いときはレイがspineと平行なので端点の球だけを見る。
	if (std::abs(a) > 1.0e-6f) {
		float discriminant = b * b - a * c;
		if (discriminant >= 0.0f) {
			float root = std::sqrt(discriminant);
			float t = (-b - root) / a;
			float along = axisDotOrigin + t * axisDotDown;
			if (along >= 0.0f && along <= axisSq && t > 0.0f && t <= maxDistance) {
				outDistance = t;
				return true;
			}
		}
	}

	// 端点の半球。上側にある方を先に見る。
	const Vector3& upperCap = (capsule.p0.y >= capsule.p1.y) ? capsule.p0 : capsule.p1;
	const Vector3& lowerCap = (capsule.p0.y >= capsule.p1.y) ? capsule.p1 : capsule.p0;

	if (RaycastDownSphere(origin, maxDistance, KujataEngine::Sphere{upperCap, capsule.radius}, outDistance)) {
		return true;
	}
	return RaycastDownSphere(origin, maxDistance, KujataEngine::Sphere{lowerCap, capsule.radius}, outDistance);
}

/// <summary>
/// 真下レイとAABBの交差。回転を持たない箱のときだけ使う(OBBより軽い)。
/// </summary>
inline bool RaycastDownAabb(const Vector3& origin, float maxDistance, const KujataEngine::AABB& aabb, float& outDistance) {
	// 水平方向は「レイのxzがAABBの範囲内か」を見るだけでよい(真下方向のレイなので)。
	if (origin.x < aabb.min.x || origin.x > aabb.max.x) {
		return false;
	}
	if (origin.z < aabb.min.z || origin.z > aabb.max.z) {
		return false;
	}

	// 天面が原点より上 = 原点が箱の内部か下。足場にしない(上の共通の約束を参照)。
	float distance = origin.y - aabb.max.y;
	if (distance < 0.0f || distance > maxDistance) {
		return false;
	}

	outDistance = distance;
	return true;
}

/// <summary>
/// ワールド空間での姿勢。スケールは一様(x=y=z)前提で1成分だけ持つ。
/// </summary>
struct WorldPose {
	Vector3 position = {0.0f, 0.0f, 0.0f};
	Quaternion rotation = Quaternion::Identity();
	float scale = 1.0f;

	/// <summary>ローカル座標をワールド座標へ変換します。</summary>
	Vector3 TransformPoint(const Vector3& local) const { return position + rotation.RotateVector(local * scale); }

	/// <summary>ワールド座標をローカル座標へ変換します。</summary>
	Vector3 InverseTransformPoint(const Vector3& world) const {
		Vector3 offset = world - position;
		Vector3 rotated = rotation.Inverse().RotateVector(offset);
		return (scale > 1.0e-6f) ? rotated / scale : rotated;
	}
};

/// <summary>
/// 親を辿ってワールド姿勢を合成します。
///
/// Scene::UpdateWorldTransforms は全Componentの Update の後に走るため、Update中の matWorld_ は
/// 1フレーム古い。IKは「同じフレーム内で書き換えたローカル値」を前提にするので、ここで自前に合成する。
/// 非一様スケールには対応しない(脚チェーンのscaleは1のまま、見た目のスケールはMesh子オブジェクトで行う)。
/// </summary>
inline WorldPose ComputeWorldPose(const GameObject* object) {
	if (!object) {
		return WorldPose{};
	}

	const KujataEngine::WorldTransform& local = object->GetTransform();
	WorldPose parent = ComputeWorldPose(object->GetParent());

	WorldPose result{};
	result.position = parent.position + parent.rotation.RotateVector(local.translation_ * parent.scale);
	result.rotation = parent.rotation * Quaternion::FromEuler(local.rotation_);
	result.scale = parent.scale * local.scale_.x;
	return result;
}

// --- 曲線(3次ベジェ)まわり ---

/// <summary>折れ線に落とすときの最大分割数。</summary>
inline constexpr int kMaxCurveSamples = 128;

/// <summary>3次ベジェをtで評価します。</summary>
inline Vector3 EvaluateBezier(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t) {
	float u = 1.0f - t;
	float w0 = u * u * u;
	float w1 = 3.0f * u * u * t;
	float w2 = 3.0f * u * t * t;
	float w3 = t * t * t;
	return p0 * w0 + p1 * w1 + p2 * w2 + p3 * w3;
}

/// <summary>
/// ベジェを折れ線化したもの。弧長からの位置逆引きに使う。
/// 毎フレーム4本ぶん作るので、ヒープを触らないよう固定長配列で持つ。
/// </summary>
struct CurvePolyline {
	Vector3 points[kMaxCurveSamples + 1] = {};
	// points[i]までの累積弧長。cumulative[0] = 0。
	float cumulative[kMaxCurveSamples + 1] = {};
	int pointCount = 0;
	float totalLength = 0.0f;

	/// <summary>始点から弧長distanceだけ進んだ位置を返します(範囲外は端でクランプ)。</summary>
	Vector3 PointAtDistance(float distance) const {
		if (pointCount <= 0) {
			return Vector3{0.0f, 0.0f, 0.0f};
		}
		if (distance <= 0.0f || totalLength <= 1.0e-6f) {
			return points[0];
		}
		if (distance >= totalLength) {
			return points[pointCount - 1];
		}

		// cumulativeは単調増加なので二分探索でdistanceを跨ぐ区間を見つける。
		int low = 0;
		int high = pointCount - 1;
		while (low + 1 < high) {
			int mid = (low + high) / 2;
			if (cumulative[mid] <= distance) {
				low = mid;
			} else {
				high = mid;
			}
		}

		float segmentLength = cumulative[high] - cumulative[low];
		float t = (segmentLength > 1.0e-6f) ? (distance - cumulative[low]) / segmentLength : 0.0f;
		return Lerp3(points[low], points[high], t);
	}
};

/// <summary>ベジェを等パラメータで分割し、累積弧長付きの折れ線にします。</summary>
inline void BuildBezierPolyline(
    const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, int sampleCount, CurvePolyline& out) {

	sampleCount = std::clamp(sampleCount, 4, kMaxCurveSamples);

	out.pointCount = sampleCount + 1;
	out.cumulative[0] = 0.0f;
	out.points[0] = p0;

	for (int index = 1; index <= sampleCount; ++index) {
		float t = static_cast<float>(index) / static_cast<float>(sampleCount);
		out.points[index] = EvaluateBezier(p0, p1, p2, p3, t);
		out.cumulative[index] = out.cumulative[index - 1] + KujataEngine::Length(out.points[index] - out.points[index - 1]);
	}

	out.totalLength = out.cumulative[sampleCount];
}

} // namespace GuardianRigMath
