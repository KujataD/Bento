#pragma once

#include "Matrix4x4.h"
#include "Vector3.h"
#include <algorithm>
#include <cmath>

namespace KujataEngine {

/// <summary>
/// 回転を表すクオータニオン。
///
/// 規約(エンジン全体の行ベクトル規約 v * M に合わせる):
/// - operator* は数学標準のHamilton積。
/// - 行列との対応は M(b * a) == M(a) * M(b)。
///   つまり「aを適用してからbを適用」する合成は b * a と書く(行列の積順と逆になる点に注意)。
/// - FromEuler / ToEuler は既存の MakeRotateMatrix(euler) = Mx * My * Mz (X→Y→Z適用) と厳密に一致する。
///
/// DLL境界を跨いでも安全なよう、全関数をヘッダ内inlineで実装している。
/// </summary>
struct Quaternion {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	float w = 1.0f;

	static Quaternion Identity() { return {0.0f, 0.0f, 0.0f, 1.0f}; }

	/// <summary>Hamilton積。M(b*a) == M(a)*M(b)(適用順はa→b)。</summary>
	Quaternion operator*(const Quaternion& rhs) const {
		// this = q1, rhs = q2 として q1 ⊗ q2
		return {
		    w * rhs.x + x * rhs.w + y * rhs.z - z * rhs.y,
		    w * rhs.y - x * rhs.z + y * rhs.w + z * rhs.x,
		    w * rhs.z + x * rhs.y - y * rhs.x + z * rhs.w,
		    w * rhs.w - x * rhs.x - y * rhs.y - z * rhs.z,
		};
	}

	Quaternion Conjugate() const { return {-x, -y, -z, w}; }

	float Norm() const { return std::sqrt(x * x + y * y + z * z + w * w); }

	Quaternion Normalized() const {
		float norm = Norm();
		if (norm <= 1.0e-8f) {
			return Identity();
		}
		return {x / norm, y / norm, z / norm, w / norm};
	}

	/// <summary>単位クオータニオン前提の逆回転(共役)。非単位でも安全なよう正規化して返す。</summary>
	Quaternion Inverse() const {
		float normSq = x * x + y * y + z * z + w * w;
		if (normSq <= 1.0e-12f) {
			return Identity();
		}
		return {-x / normSq, -y / normSq, -z / normSq, w / normSq};
	}

	static float Dot(const Quaternion& a, const Quaternion& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

	/// <summary>正規化済みaxis周りにangle[rad]回転するクオータニオンを作る。</summary>
	static Quaternion MakeRotateAxisAngle(const Vector3& axis, float angle) {
		float half = angle * 0.5f;
		float sinHalf = std::sin(half);
		return {axis.x * sinHalf, axis.y * sinHalf, axis.z * sinHalf, std::cos(half)};
	}

	/// <summary>
	/// Euler角(radian, X→Y→Z適用)からクオータニオンを作る。
	/// MakeRotateMatrix(euler) = Mx*My*Mz と同じ回転になる。
	/// </summary>
	static Quaternion FromEuler(const Vector3& euler) {
		Quaternion qx = MakeRotateAxisAngle({1.0f, 0.0f, 0.0f}, euler.x);
		Quaternion qy = MakeRotateAxisAngle({0.0f, 1.0f, 0.0f}, euler.y);
		Quaternion qz = MakeRotateAxisAngle({0.0f, 0.0f, 1.0f}, euler.z);
		// 行列 Mx*My*Mz(X→Y→Z適用) は Hamilton積では逆順の qz*qy*qx。
		return qz * qy * qx;
	}

	/// <summary>行ベクトル規約(v * M)の回転行列へ変換する。</summary>
	Matrix4x4 ToMatrix() const {
		float xx = x * x;
		float yy = y * y;
		float zz = z * z;
		float xy = x * y;
		float xz = x * z;
		float yz = y * z;
		float wx = w * x;
		float wy = w * y;
		float wz = w * z;

		Matrix4x4 result{};
		result.m[0][0] = 1.0f - 2.0f * (yy + zz);
		result.m[0][1] = 2.0f * (xy + wz);
		result.m[0][2] = 2.0f * (xz - wy);
		result.m[0][3] = 0.0f;
		result.m[1][0] = 2.0f * (xy - wz);
		result.m[1][1] = 1.0f - 2.0f * (xx + zz);
		result.m[1][2] = 2.0f * (yz + wx);
		result.m[1][3] = 0.0f;
		result.m[2][0] = 2.0f * (xz + wy);
		result.m[2][1] = 2.0f * (yz - wx);
		result.m[2][2] = 1.0f - 2.0f * (xx + yy);
		result.m[2][3] = 0.0f;
		result.m[3][0] = 0.0f;
		result.m[3][1] = 0.0f;
		result.m[3][2] = 0.0f;
		result.m[3][3] = 1.0f;
		return result;
	}

	/// <summary>
	/// Euler角(radian, X→Y→Z適用)へ変換する。FromEulerとの往復で同じ回転を保つ。
	/// ジンバルロック近傍のasin/atan2はfloatだと精度が大きく落ちるため、内部はdoubleで計算する。
	/// ジンバルロック時(cos(y)≈0)はz=0とみなして分解する。
	/// </summary>
	Vector3 ToEuler() const {
		// 必要な回転行列成分だけをdoubleで組み立てる(ToMatrixのdouble版)。
		double dx = x;
		double dy = y;
		double dz = z;
		double dw = w;
		double m00 = 1.0 - 2.0 * (dy * dy + dz * dz);
		double m01 = 2.0 * (dx * dy + dw * dz);
		double m02 = 2.0 * (dx * dz - dw * dy);
		double m10 = 2.0 * (dx * dy - dw * dz);
		double m11 = 1.0 - 2.0 * (dx * dx + dz * dz);
		double m12 = 2.0 * (dy * dz + dw * dx);
		double m22 = 1.0 - 2.0 * (dx * dx + dy * dy);

		Vector3 euler{};
		// m02 = -sin(y)
		double sinY = std::clamp(-m02, -1.0, 1.0);
		euler.y = static_cast<float>(std::asin(sinY));

		// cos(y)が0近傍ではx/zが縮退するため、z=0としてxへ寄せる。
		// floatのquaternion成分誤差(~1e-7)がcos(y)<1e-3の領域で通常分岐を不安定にするため、
		// しきい値はcos(y)≈1.4e-3相当(角度誤差の上限≈0.08°)に設定している。
		if (std::abs(sinY) > 0.999999) {
			euler.x = static_cast<float>(std::atan2(sinY * m10, m11));
			euler.z = 0.0f;
			return euler;
		}

		// m12 = sin(x)cos(y), m22 = cos(x)cos(y)
		euler.x = static_cast<float>(std::atan2(m12, m22));
		// m01 = cos(y)sin(z), m00 = cos(y)cos(z)
		euler.z = static_cast<float>(std::atan2(m01, m00));
		return euler;
	}

	/// <summary>ベクトルをこのクオータニオンで回転する。v * ToMatrix() と同じ結果。</summary>
	Vector3 RotateVector(const Vector3& v) const {
		// v' = v + 2w(qv×v) + 2qv×(qv×v)
		Vector3 qv = {x, y, z};
		Vector3 cross1 = {qv.y * v.z - qv.z * v.y, qv.z * v.x - qv.x * v.z, qv.x * v.y - qv.y * v.x};
		Vector3 t = {cross1.x * 2.0f, cross1.y * 2.0f, cross1.z * 2.0f};
		Vector3 cross2 = {qv.y * t.z - qv.z * t.y, qv.z * t.x - qv.x * t.z, qv.x * t.y - qv.y * t.x};
		return {v.x + w * t.x + cross2.x, v.y + w * t.y + cross2.y, v.z + w * t.z + cross2.z};
	}

	/// <summary>球面線形補間。最短経路(dot<0なら反転)で補間する。</summary>
	static Quaternion Slerp(const Quaternion& a, const Quaternion& b, float t) {
		t = std::clamp(t, 0.0f, 1.0f);

		Quaternion end = b;
		float dot = Dot(a, b);
		if (dot < 0.0f) {
			end = {-b.x, -b.y, -b.z, -b.w};
			dot = -dot;
		}

		// ほぼ同一姿勢なら線形補間で十分(0除算回避)。
		if (dot > 0.9995f) {
			Quaternion result = {
			    a.x + (end.x - a.x) * t,
			    a.y + (end.y - a.y) * t,
			    a.z + (end.z - a.z) * t,
			    a.w + (end.w - a.w) * t,
			};
			return result.Normalized();
		}

		float theta = std::acos(std::clamp(dot, -1.0f, 1.0f));
		float sinTheta = std::sin(theta);
		float scaleA = std::sin((1.0f - t) * theta) / sinTheta;
		float scaleB = std::sin(t * theta) / sinTheta;
		return {
		    a.x * scaleA + end.x * scaleB,
		    a.y * scaleA + end.y * scaleB,
		    a.z * scaleA + end.z * scaleB,
		    a.w * scaleA + end.w * scaleB,
		};
	}

	/// <summary>2つの姿勢のなす角[rad]を返す。</summary>
	static float Angle(const Quaternion& a, const Quaternion& b) {
		float dot = std::abs(Dot(a.Normalized(), b.Normalized()));
		return 2.0f * std::acos(std::clamp(dot, 0.0f, 1.0f));
	}

	/// <summary>fromからtoへ、最大maxRadiansだけ回転を進める(Unity同名APIの挙動)。</summary>
	static Quaternion RotateTowards(const Quaternion& from, const Quaternion& to, float maxRadians) {
		float angle = Angle(from, to);
		if (angle <= maxRadians || angle <= 1.0e-6f) {
			return to;
		}
		return Slerp(from, to, maxRadians / angle);
	}

	/// <summary>
	/// forward方向(+Z)をforwardへ、上方向をupへ寄せた姿勢を作る(UnityのLookRotation相当)。
	/// forwardとupが平行な場合はIdentityを返す。
	/// </summary>
	static Quaternion LookRotation(const Vector3& forward, const Vector3& up = {0.0f, 1.0f, 0.0f}) {
		auto lengthOf = [](const Vector3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); };

		float forwardLength = lengthOf(forward);
		if (forwardLength <= 1.0e-6f) {
			return Identity();
		}
		Vector3 f = {forward.x / forwardLength, forward.y / forwardLength, forward.z / forwardLength};

		// right = up × forward (左手系・行ベクトル規約の基底)
		Vector3 right = {up.y * f.z - up.z * f.y, up.z * f.x - up.x * f.z, up.x * f.y - up.y * f.x};
		float rightLength = lengthOf(right);
		if (rightLength <= 1.0e-6f) {
			return Identity();
		}
		right = {right.x / rightLength, right.y / rightLength, right.z / rightLength};

		// newUp = forward × right
		Vector3 newUp = {f.y * right.z - f.z * right.y, f.z * right.x - f.x * right.z, f.x * right.y - f.y * right.x};

		// 行ベクトル規約の回転行列: 行が基底(right, newUp, forward)。
		float m00 = right.x, m01 = right.y, m02 = right.z;
		float m10 = newUp.x, m11 = newUp.y, m12 = newUp.z;
		float m20 = f.x, m21 = f.y, m22 = f.z;

		// 行列→クオータニオン(Shepperd法)。M = R^T 前提の添字。
		float trace = m00 + m11 + m22;
		Quaternion q{};
		if (trace > 0.0f) {
			float s = std::sqrt(trace + 1.0f) * 2.0f; // 4w
			q.w = 0.25f * s;
			q.x = (m12 - m21) / s;
			q.y = (m20 - m02) / s;
			q.z = (m01 - m10) / s;
		} else if (m00 > m11 && m00 > m22) {
			float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f; // 4x
			q.w = (m12 - m21) / s;
			q.x = 0.25f * s;
			q.y = (m01 + m10) / s;
			q.z = (m02 + m20) / s;
		} else if (m11 > m22) {
			float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f; // 4y
			q.w = (m20 - m02) / s;
			q.x = (m01 + m10) / s;
			q.y = 0.25f * s;
			q.z = (m12 + m21) / s;
		} else {
			float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f; // 4z
			q.w = (m01 - m10) / s;
			q.x = (m02 + m20) / s;
			q.y = (m12 + m21) / s;
			q.z = 0.25f * s;
		}
		return q.Normalized();
	}
};

} // namespace KujataEngine
