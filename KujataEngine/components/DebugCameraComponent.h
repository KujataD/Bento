#pragma once

#include "../runtime/KujataApi.h"
#include "../3d/DebugCamera.h"
#include "../math/Vector3.h"
#include "../scene/Component.h"

namespace KujataEngine {

/// <summary>
/// Edit中にGameObjectのCameraをDebugCamera操作で動かすComponent
/// </summary>
class KUJATA_API DebugCameraComponent : public Component {
public:
	const char* GetTypeName() const override { return "DebugCameraComponent"; }

	bool AllowMultiple() const override { return false; }

	void Initialize() override;

	void DrawInspector() override;

	/// <summary>
	/// Edit中のDebugCamera操作をTransformとCameraComponentへ反映する
	/// </summary>
	void UpdateEditorCamera();

	/// <summary>
	/// JSON復元後にDebugCameraの基準値をTransformへ合わせる
	/// </summary>
	void OnAfterReadJson() override;

private:
	void InitializeFromOwnerTransform();
	void SyncDebugCameraFromExternalTransform();
	void SyncOwnerTransformFromDebugCamera();

	bool HasTransformChangedExternally(const Vector3& translation, const Vector3& rotation) const;

private:
	DebugCamera debugCamera_;
	Vector3 lastAppliedTranslation_ = {0.0f, 0.0f, 0.0f};
	Vector3 lastAppliedRotation_ = {0.0f, 0.0f, 0.0f};
	bool initialized_ = false;
};

} // namespace KujataEngine
