#pragma once

#include "../3d/Model.h"
#include "../math/Vector2.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include "../runtime/KujataApi.h"
#include "../scene/Component.h"
#include "../scene/IMaterialTarget.h"
#include "../scene/SerializedFieldRegistry.h"
#include <memory>
#include <string>

namespace KujataEngine {

class Camera;
class Scene;

/// <summary>
/// 海面。付けるだけで、細かく分割した板(格子)が波打つ(ローポリ・ドット絵向けに、陰も泡も段で切り替える)。
///
/// 仕組み:
///   - 形は Model::CreateGrid の格子(XZ平面。このオブジェクトの位置が海面の高さの基準、Size が広さ)
///   - 波はシェーダー(EngineData/shader/Custom/Ocean.hlsl)の頂点シェーダーで、正弦波を最大4つ足して頂点を上下させる
///   - **同じ式を C++ でも計算する**(GetSurfaceHeight)。描かれている三角形の上の高さを返すので、見た目の海面とぴったり合う
///   - 波の時間はゲームの時間(Time::GetDeltaTime。ヒットストップ・スローで波も止まる)。Play 中でないときは実時間で動かす
///
/// 海面の高さを使う:
///   - OceanComponent::TryGetSurfaceHeight(scene, x, z, height) … その位置の海面の高さ(海の外なら false)
///   - FloatOnWaterComponent … 付けた物を海面に浮かべる
///
/// 見た目は Material で変えられる(未設定なら Water Color の色で、トゥーン3段・面ごとの陰)。
/// マテリアルの Shader が空なら、海のシェーダーを使う(波は Shader Params ではなく、この Inspector の Wave で決める)。
/// **回転は Y 軸まわりだけ、拡大は X・Z だけにする**(傾けると波が板に沿わない。波の高さは Wave Height で決める)。
/// </summary>
class KUJATA_API OceanComponent : public Component, public IMaterialTarget {
public:
	static constexpr int kWaveCount = 4;

	const char* GetTypeName() const override { return "OceanComponent"; }
	bool AllowMultiple() const override { return false; }

	void OnPlayStart() override;
	void OnPlayStop() override;
	void Update() override;
	void Draw() override;

	bool IsTransparentDraw() const override { return transparent_; }

	bool ApplyMaterialAsset(const std::string& materialPath) override;
	bool UsesMaterialAsset(const std::string& materialPath) const override;

	/// <summary>描画に使うカメラ。Sceneが各ビューの描画前に配る(ModelRendererComponentと同じ扱い)。</summary>
	void SetCamera(const Camera* camera) { camera_ = camera; }

	/// <summary>
	/// ワールド座標 (x, z) の海面の高さ(ワールドの Y)を返す。描かれている三角形の上の高さなので、見た目と一致する。
	/// 海の板の外なら false(outHeight は変えない)。
	/// </summary>
	bool GetSurfaceHeight(float x, float z, float& outHeight) const;

	/// <summary>
	/// シーンの海(有効な OceanComponent)から、ワールド座標 (x, z) の海面の高さを探す。重なっていれば高い方。
	/// どの海の上でもなければ false。浮かぶ物・水しぶき・着水の判定などに使う。
	/// </summary>
	static bool TryGetSurfaceHeight(const Scene& scene, float x, float z, float& outHeight);

	/// <summary>
	/// 波の式そのもの(このオブジェクトの位置・拡大を含まない、波だけの上下[m])。worldXZ はワールドの xz。
	/// シェーダーの WaveHeight と同じ式(変えるときは両方直す)。
	/// </summary>
	float EvaluateWaves(const Vector2& worldXZ) const;

	/// <summary>波の時間[秒](シェーダーの gShaderParams.time と同じ値)。</summary>
	float GetWaveTime() const { return waveTime_; }

private:
	KUJATA_SERIALIZED_FIELDS_BEGIN() {
		KUJATA_REGISTER_FLOAT_NAMED_TIP(sizeX_, "Size X", 0.1f, 0.1f, 10000.0f, "海の広さ(X方向[m])。このオブジェクトの位置が中心。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(sizeZ_, "Size Z", 0.1f, 0.1f, 10000.0f, "海の広さ(Z方向[m])。");
		KUJATA_REGISTER_INT_NAMED_TIP(divisions_, "Divisions", 1.0f, 1, 512,
		    "長い方の辺を何マスに分けるか(短い方は同じマスの大きさになるように決める)。少ないほどローポリ。1マスは波長の1/3以下がおすすめ。");
		KUJATA_REGISTER_VECTOR4_NAMED_TIP(waterColor_, "Water Color", 0.01f, 0.0f, 1.0f, "海の色(Material が未設定のときだけ使う)。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(foamLevel_, "Foam Level", 0.01f, 0.0f, 1.0f,
		    "波の山のどこから上を泡(白)にするか。0=泡なし、0.8=山の上の方だけ、0.5=半分より上。");
		KUJATA_REGISTER_FLOAT_NAMED_TIP(timeScale_, "Time Scale", 0.01f, 0.0f, 10.0f, "波の進む速さの倍率(0で止まる)。");
		registry.FloatNamed("wave1Direction", "Wave1 Direction", waves_[0].x, 1.0f, -360.0f, 360.0f, "波1の進む向き[度](0=+X、90=+Z)。");
		registry.FloatNamed("wave1Length", "Wave1 Length", waves_[0].y, 0.05f, 0.0f, 1000.0f, "波1の波長[m](山から次の山まで)。0でこの波を使わない。");
		registry.FloatNamed("wave1Height", "Wave1 Height", waves_[0].z, 0.01f, 0.0f, 100.0f, "波1の高さ[m](平らな面から山の頂点まで)。");
		registry.FloatNamed("wave1Speed", "Wave1 Speed", waves_[0].w, 0.05f, -100.0f, 100.0f, "波1の進む速さ[m/秒]。");
		registry.FloatNamed("wave2Direction", "Wave2 Direction", waves_[1].x, 1.0f, -360.0f, 360.0f, "波2の進む向き[度]。波1と向きを変えると自然になる。");
		registry.FloatNamed("wave2Length", "Wave2 Length", waves_[1].y, 0.05f, 0.0f, 1000.0f, "波2の波長[m]。0でこの波を使わない。");
		registry.FloatNamed("wave2Height", "Wave2 Height", waves_[1].z, 0.01f, 0.0f, 100.0f, "波2の高さ[m]。");
		registry.FloatNamed("wave2Speed", "Wave2 Speed", waves_[1].w, 0.05f, -100.0f, 100.0f, "波2の進む速さ[m/秒]。");
		registry.FloatNamed("wave3Direction", "Wave3 Direction", waves_[2].x, 1.0f, -360.0f, 360.0f, "波3の進む向き[度]。");
		registry.FloatNamed("wave3Length", "Wave3 Length", waves_[2].y, 0.05f, 0.0f, 1000.0f, "波3の波長[m]。0でこの波を使わない。");
		registry.FloatNamed("wave3Height", "Wave3 Height", waves_[2].z, 0.01f, 0.0f, 100.0f, "波3の高さ[m]。");
		registry.FloatNamed("wave3Speed", "Wave3 Speed", waves_[2].w, 0.05f, -100.0f, 100.0f, "波3の進む速さ[m/秒]。");
		registry.FloatNamed("wave4Direction", "Wave4 Direction", waves_[3].x, 1.0f, -360.0f, 360.0f, "波4の進む向き[度]。");
		registry.FloatNamed("wave4Length", "Wave4 Length", waves_[3].y, 0.05f, 0.0f, 1000.0f, "波4の波長[m]。0でこの波を使わない(既定は使わない)。");
		registry.FloatNamed("wave4Height", "Wave4 Height", waves_[3].z, 0.01f, 0.0f, 100.0f, "波4の高さ[m]。");
		registry.FloatNamed("wave4Speed", "Wave4 Speed", waves_[3].w, 0.05f, -100.0f, 100.0f, "波4の進む速さ[m/秒]。");
		KUJATA_REGISTER_STRING_NAMED_TIP(materialPath_, "Material",
		    "見た目を変えるときのマテリアル(Project からドラッグでも設定できる)。Shader が空なら海のシェーダーを使う。");
	}

	KUJATA_FIELD_FLOAT(sizeX_, 40.0f);
	KUJATA_FIELD_FLOAT(sizeZ_, 40.0f);
	KUJATA_FIELD_INT(divisions_, 40);
	Vector4 waterColor_ = {0.15f, 0.45f, 0.75f, 1.0f};
	KUJATA_FIELD_FLOAT(foamLevel_, 0.85f);
	KUJATA_FIELD_FLOAT(timeScale_, 1.0f);
	// 波(向き[度], 波長[m], 高さ[m], 速さ[m/s])。シェーダーの gShaderParams.params[0..3] へそのまま渡す。
	Vector4 waves_[kWaveCount] = {
	    {0.0f, 8.0f, 0.25f, 2.0f},
	    {60.0f, 5.0f, 0.15f, 1.5f},
	    {150.0f, 3.0f, 0.08f, 1.2f},
	    {0.0f, 0.0f, 0.0f, 1.0f},
	};
	KUJATA_FIELD_STRING(materialPath_, "");

	/// <summary>格子のマスの数(長い辺が Divisions マス)。</summary>
	void GetGridDivisions(uint32_t& outX, uint32_t& outZ) const;
	/// <summary>形(格子)が無いか、広さ・分割が変わっていれば作り直す。</summary>
	void EnsureModel();
	/// <summary>マテリアル(未設定なら既定の海の見た目)をモデルへ反映する。</summary>
	void ApplyMaterial();

	// --- 実行時 ---
	const Camera* camera_ = nullptr;
	std::unique_ptr<Model> model_;
	float builtSizeX_ = 0.0f;
	float builtSizeZ_ = 0.0f;
	uint32_t builtDivisionsX_ = 0;
	uint32_t builtDivisionsZ_ = 0;
	std::string appliedMaterialPath_;
	Vector4 appliedWaterColor_ = {};
	bool materialDirty_ = true;
	bool transparent_ = false;
	// 波の時間。Play 中は Update がゲームの時間で進め、それ以外は Draw がフレームに1回、実時間で進める。
	float waveTime_ = 0.0f;
	bool playing_ = false;
	uint64_t advancedFrame_ = ~0ull;
};

} // namespace KujataEngine
