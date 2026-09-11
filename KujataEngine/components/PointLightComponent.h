#pragma once

#include "../runtime/KujataApi.h"
#include "../3d/PointLight.h"
#include "../scene/Component.h"
#include "../scene/IEditorBillboard.h"

namespace KujataEngine {

/// <summary>
/// GameObjectのTransform位置をPointLightへ反映するComponent
/// </summary>
class KUJATA_API PointLightComponent : public Component, public IEditorBillboard {
public:

	const char* GetTypeName() const override { return "PointLightComponent"; }

	bool AllowMultiple() const override { return false; }
	const char* GetEditorBillboardIconName() const override { return "icon_light_point.png"; }
	float GetEditorBillboardPickRadius() const override { return 0.65f; }
	
	void DrawInspector() override;

	/// <summary>
	/// PointLightのGPUデータへ追加する
	/// </summary>
	void Apply();

	void WriteJson(nlohmann::json& json) const override;

	void ReadJson(const nlohmann::json& json) override;

	PointLightData& GetData() { return data_; }

private:
	PointLightData data_{};
};

} // namespace KujataEngine
