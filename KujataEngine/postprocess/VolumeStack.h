#pragma once

#include <vector>

#include "../math/Vector3.h"
#include "../runtime/KujataApi.h"
#include "VolumeProfile.h"

namespace KujataEngine {

class Scene;
class VolumeComponent;

/// <summary>
/// Volumeを解決した結果。
/// contributorsは実効weightが0より大きかったVolumeで、Rendering Windowのデバッグ表示に使う。
/// </summary>
struct VolumeResolveResult {
	VolumeProfileData profile;
	std::vector<VolumeComponent*> contributors;
};

/// <summary>
/// シーン上のVolumeComponentを集め、優先度順にブレンドして最終的なポストエフェクト設定を作る。
/// UnityのVolumeManagerに相当する。カメラ位置はLocal Volumeの内外判定に使う。
/// </summary>
class VolumeStack {
public:
	/// <summary>
	/// シーン内の有効なVolumeを priority 昇順にブレンドして解決する。
	/// priorityが同じ場合はシーン内の並び順(Hierarchyの上から)で後勝ちになる。
	/// </summary>
	KUJATA_API static VolumeResolveResult Resolve(Scene& scene, const Vector3& cameraPosition);

	/// <summary>
	/// Volume 1つの実効weightを求める。
	/// Global: weightをそのまま使う。
	/// Local : 同じGameObjectのColliderの内側で最大、blendDistanceだけ外へ出るまでに0へ減衰する。
	///         Colliderが無いLocal Volumeは範囲を決められないため0(=無効)を返す。
	/// </summary>
	KUJATA_API static float ComputeWeight(VolumeComponent& volume, const Vector3& cameraPosition);
};

} // namespace KujataEngine
