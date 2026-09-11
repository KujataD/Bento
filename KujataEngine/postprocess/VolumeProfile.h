#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include "../runtime/KujataApi.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 26495)
#pragma warning(disable : 26819)
#endif
#include "../../externals/nlohmann/json.hpp"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace KujataEngine {

/// <summary>
/// ポストエフェクトの種別。
///
/// 新しいエフェクトを追加する手順はこの4つだけで完結する:
///   1. ここに列挙子を1つ足す(kCountより前)
///   2. 設定structを定義する
///   3. VolumeProfileDataへメンバを足す
///   4. VolumeProfile.cppのディスクリプタ表へ1エントリ足す(JSON/ブレンド/Inspectorをまとめて登録)
/// PostProcess側でGPUへ渡す処理だけは各エフェクト固有なので個別に書く。
/// </summary>
enum class VolumeEffectId : int32_t {
	kBloom,
	kTonemap,
	kColorGrading,
	kVignette,
	kFog,
	kCount,
};

inline constexpr size_t kVolumeEffectCount = static_cast<size_t>(VolumeEffectId::kCount);

/// ブルーム(合成の全体ON/OFFと強度。閾値/soft kneeはマテリアル別設定側)。
struct BloomSettings {
	bool enabled = true;
	float intensity = 0.65f;
};

/// 露出とトーンマップ。
struct TonemapSettings {
	float exposure = 1.0f;
	int32_t tonemapper = 2; // 0=None(検証用) / 1=Reinhard / 2=ACES
};

/// 簡易カラーグレーディング。
struct ColorGradingSettings {
	float saturation = 1.0f;
	float contrast = 1.0f;
	Vector4 colorFilter = {1.0f, 1.0f, 1.0f, 1.0f}; // rgbのみ使用
};

/// ビネット。
struct VignetteSettings {
	float intensity = 0.0f;
	float smoothness = 0.4f;
};

/// フォグ(距離+高さ+SpotLight散乱)。
struct FogSettings {
	bool enabled = false;
	Vector3 color = {0.5f, 0.55f, 0.6f}; // 霧色(リニア)
	float density = 0.02f;               // 距離ベースの指数フォグ密度
	float heightFalloff = 0.1f;          // 高さによる密度減衰(0で均一)
	float heightBase = 0.0f;             // この高さより上ほど薄くなる
	float startDistance = 1.0f;          // このカメラ距離まではフォグなし
	float maxDistance = 80.0f;           // レイマーチの最大距離
	float spotScatter = 1.0f;            // SpotLightが霧を照らす強さ
};

/// <summary>
/// Volume 1つ分のポストエフェクト設定一式。
/// overrides[]がfalseのエフェクトは「このVolumeは触らない」扱いになり、
/// 優先度の低いVolume(や既定値)の値がそのまま残る。UnityのVolume Profileと同じ考え方。
/// </summary>
struct VolumeProfileData {
	bool overrides[kVolumeEffectCount] = {};

	BloomSettings bloom;
	TonemapSettings tonemap;
	ColorGradingSettings colorGrading;
	VignetteSettings vignette;
	FogSettings fog;

	bool IsOverriding(VolumeEffectId id) const { return overrides[static_cast<size_t>(id)]; }
	void SetOverriding(VolumeEffectId id, bool value) { overrides[static_cast<size_t>(id)] = value; }
};

/// <summary>
/// エフェクト1種のJSON入出力・ブレンド・Inspector描画をまとめた登録情報。
/// エフェクトごとの処理をこの1箇所に集めることで、追加時に触る場所を限定する。
/// </summary>
/// overrideフラグの読み書き・ブレンド可否の判定・見出し描画は共通処理側が行うため、
/// 各関数はそのエフェクト固有の値だけを扱えばよい。
struct VolumeEffectDescriptor {
	VolumeEffectId id;
	const char* name; // Inspectorの見出し兼JSONキー

	/// このエフェクト専用のノードへ値を書く。
	void (*write)(const VolumeProfileData& profile, nlohmann::json& effectJson);
	/// このエフェクト専用のノードから値を読む(キーが無ければprofileの現在値を維持)。
	void (*read)(const nlohmann::json& effectJson, VolumeProfileData& profile);
	/// sourceの値をweightでdestinationへ重ねる。float系は線形補間、bool/intはweight>=0.5で置換。
	void (*blend)(const VolumeProfileData& source, float weight, VolumeProfileData& destination);
	/// 値のInspectorを描画する。変更があればtrue(USE_IMGUI無効時は常にfalse)。
	bool (*drawInspector)(VolumeProfileData& profile);
};

/// 登録済みエフェクトの一覧。並び順がInspectorの表示順になる。
KUJATA_API const std::vector<VolumeEffectDescriptor>& GetVolumeEffectDescriptors();

/// 全エフェクトをjsonへ書き出す。
KUJATA_API void WriteVolumeProfileJson(const VolumeProfileData& profile, nlohmann::json& json);

/// 全エフェクトをjsonから読み込む(欠けているキーはprofileの現在値を維持)。
KUJATA_API void ReadVolumeProfileJson(const nlohmann::json& json, VolumeProfileData& profile);

/// <summary>
/// sourceをweightでdestinationへ重ねる。
/// sourceがoverrideしているエフェクトだけが対象で、そのエフェクトはdestination側もoverride済みになる。
/// </summary>
KUJATA_API void BlendVolumeProfile(const VolumeProfileData& source, float weight, VolumeProfileData& destination);

} // namespace KujataEngine
