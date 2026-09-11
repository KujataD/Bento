#pragma once

#include "../runtime/KujataApi.h"
#include "../3d/Camera.h"
#include "../scene/Component.h"
#include "../scene/IEditorBillboard.h"
#include "../scene/ISceneCamera.h"

namespace KujataEngine {

/// <summary>
/// GameObjectのTransformをCameraへ反映するComponent
/// </summary>
class KUJATA_API CameraComponent : public Component, public IEditorBillboard, public ISceneCamera {
public:
	const char* GetTypeName() const override { return "CameraComponent"; }

	bool AllowMultiple() const override { return false; }
	const char* GetEditorBillboardIconName() const override { return "icon_camera.png"; }
	float GetEditorBillboardPickRadius() const override { return 0.65f; }

	void Initialize() override;
	void Update() override;
	void Draw() override;

	void DrawInspector() override;

	void WriteJson(nlohmann::json& json) const override;

	void ReadJson(const nlohmann::json& json) override;

	/// <summary>
	/// JSON復元後にTransformとProjectionをCameraへ反映する
	/// </summary>
	void OnAfterReadJson() override;

	/// <summary>
	/// 所有GameObjectのTransformをCameraへ反映する
	/// </summary>
	void SyncFromOwnerTransform();

	Camera& GetCamera() { return camera_; }
	Camera* GetSceneCamera() override { return &camera_; }
	const Camera& GetCamera() const { return camera_; }
	const Camera* GetSceneCamera() const override { return &camera_; }

private:
	Camera camera_;
	bool initialized_ = false;
};

} // namespace KujataEngine
