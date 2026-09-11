#pragma once

#include "../runtime/KujataApi.h"
#include "../3d/SpotLight.h"
#include "../scene/Component.h"
#include "../scene/IEditorBillboard.h"

namespace KujataEngine {

/// <summary>
/// GameObjectのTransformからSpotLightの位置と向きを決めるComponent。
///
/// Unityと同じく、光は**Transformの前方(+Z)**へ照射する。向きはInspectorではなく
/// GameObjectを回転させて決める(Sceneビューにコーンのギズモが出る)。
/// 角度はUnity準拠でdegree(Spot Angle / Inner Spot Angle)を持ち、
/// GPUへ渡すときだけcosineへ変換する。
///
/// シーンに置ける数はkMaxSpotLight(16灯)まで。超えた分は無視される。
/// </summary>
class KUJATA_API SpotLightComponent : public Component, public IEditorBillboard {
public:
	/// SpotLightDataは0初期化されるメンバが多いので、ここでUnity相当の既定値を入れる。
	SpotLightComponent();

	const char* GetTypeName() const override { return "SpotLightComponent"; }

	bool AllowMultiple() const override { return false; }
	const char* GetEditorBillboardIconName() const override { return "icon_light_spot.png"; }
	float GetEditorBillboardPickRadius() const override { return 0.65f; }

	void DrawInspector() override;

	/// <summary>
	/// Transformから位置・向きを取り込み、SpotLightのGPUデータへ反映する。
	/// </summary>
	void Apply();

	void WriteJson(nlohmann::json& json) const override;

	void ReadJson(const nlohmann::json& json) override;

	SpotLightData& GetData() { return data_; }

	/// 光の届く距離(Unityの Range)。
	float GetRange() const { return data_.distance; }

	/// コーンの全開き角(度)。Unityの Spot Angle と同じ定義。
	float GetSpotAngleDegrees() const { return spotAngleDegrees_; }

	/// 減衰が始まる内側の全開き角(度)。Unityの Inner Spot Angle 相当。
	float GetInnerSpotAngleDegrees() const { return innerSpotAngleDegrees_; }

	/// <summary>
	/// ワールド空間での照射方向(正規化済み)。TransformのZ+前方。
	/// ギズモ描画とApplyで同じ値を使うためここに集約する。
	/// </summary>
	Vector3 GetWorldDirection() const;

private:
	/// degreeで持っている角度をSpotLightDataのcosineへ書き込む。
	void ApplyAnglesToData();

	SpotLightData data_{};
	// Unityの既定値に合わせる(Range 10 / Spot Angle 30 / Inner 21.8)。
	float spotAngleDegrees_ = 30.0f;
	float innerSpotAngleDegrees_ = 21.8f;
};

} // namespace KujataEngine
