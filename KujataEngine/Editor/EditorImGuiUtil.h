#pragma once

#include <string>

namespace KujataEngine {

class Scene;

// Editor UI 全体で共有するドラッグ&ドロップのペイロード種別。
constexpr const char* kHierarchyDragPayloadType = "KujataHierarchyGameObject";
constexpr const char* kProjectPrefabDragPayloadType = "KujataProjectPrefab";
constexpr const char* kProjectMaterialDragPayloadType = "KujataProjectMaterial";
constexpr const char* kProjectTextureDragPayloadType = "KujataProjectTexture";
constexpr const char* kProjectModelDragPayloadType = "KujataProjectModel";
constexpr const char* kProjectAnimClipDragPayloadType = "KujataProjectAnimClip";

// 現在のSceneの状態をUndo履歴へ積む。
void CaptureUndo(Scene& scene, const std::string& label);

// 指定したScene JSONをUndo履歴へ積む（編集前スナップショットを渡す用途）。
void CaptureUndo(Scene& scene, const std::string& label, const std::string& sceneJson);

} // namespace KujataEngine
