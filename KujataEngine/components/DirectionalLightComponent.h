#pragma once

#include "../runtime/KujataApi.h"
#include "../3d/DirectionalLight.h"
#include "../scene/Component.h"
#include "../scene/IEditorBillboard.h"

namespace KujataEngine {

/// <summary>
/// DirectionalLightのGPUデータをGameObject上で管理するComponent
/// </summary>
class KUJATA_API DirectionalLightComponent : public Component, public IEditorBillboard {
public:
	const char* GetTypeName() const override { return "DirectionalLightComponent"; }

	bool AllowMultiple() const override { return false; }
	const char* GetEditorBillboardIconName() const override { return "icon_light_directional.png"; }
	float GetEditorBillboardPickRadius() const override { return 0.65f; }

	void DrawInspector() override;

	/// <summary>
	/// DirectionalLightのGPUデータへ反映する
	/// </summary>
	void Apply();

	void WriteJson(nlohmann::json& json) const override;

	void ReadJson(const nlohmann::json& json) override;

	/// <summary>
	/// JSON復元後にライト情報を反映する
	/// </summary>
	void OnAfterReadJson() override;

	DirectionalLightData& GetData() { return data_; }

private:
	DirectionalLightData data_{};
};

} // namespace KujataEngine
