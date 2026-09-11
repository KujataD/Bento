#pragma once

namespace KujataEngine {

class Camera;

// Sceneのカメラを提供するComponent(CameraComponent)が実装するインターフェース。
// 呼び出し側はdynamic_castで能力を問い合わせる。
class ISceneCamera {
public:
	virtual ~ISceneCamera() = default;

	virtual Camera* GetSceneCamera() = 0;
	virtual const Camera* GetSceneCamera() const = 0;
};

} // namespace KujataEngine
