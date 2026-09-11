#include "EditorUndoManager.h"
#include "EditorSelection.h"
#include "SceneJsonImporter.h"
#include "../scene/GameObject.h"
#include "../scene/Scene.h"
#include <algorithm>
#include <utility>

namespace KujataEngine {

EditorUndoManager* EditorUndoManager::GetInstance() {
	static EditorUndoManager instance;
	return &instance;
}

void EditorUndoManager::Capture(Scene& scene, const std::string& label) {
	Capture(scene, label, scene.ToJson());
}

void EditorUndoManager::Capture(Scene& scene, const std::string& label, const std::string& sceneJson) {
	if (sceneJson.empty()) {
		return;
	}
	if (!undoStack_.empty() && undoStack_.back().sceneJson == sceneJson) {
		return;
	}

	PushUndo(MakeSnapshot(scene, label, sceneJson));
	redoStack_.clear();
}

bool EditorUndoManager::Undo(Scene& scene) {
	if (undoStack_.empty()) {
		return false;
	}

	Snapshot current = MakeSnapshot(scene, "Redo", scene.ToJson());
	Snapshot target = undoStack_.back();
	undoStack_.pop_back();

	if (!ApplySnapshot(scene, target)) {
		PushUndo(std::move(target));
		return false;
	}

	PushRedo(std::move(current));
	return true;
}

bool EditorUndoManager::Redo(Scene& scene) {
	if (redoStack_.empty()) {
		return false;
	}

	Snapshot current = MakeSnapshot(scene, "Undo", scene.ToJson());
	Snapshot target = redoStack_.back();
	redoStack_.pop_back();

	if (!ApplySnapshot(scene, target)) {
		PushRedo(std::move(target));
		return false;
	}

	PushUndo(std::move(current));
	return true;
}

void EditorUndoManager::Clear() {
	undoStack_.clear();
	redoStack_.clear();
}

void EditorUndoManager::SetMaxUndoCount(size_t maxUndoCount) {
	if (maxUndoCount < 1) {
		maxUndoCount = 1;
	}

	maxUndoCount_ = maxUndoCount;
	TrimUndoStack();
}

EditorUndoManager::Snapshot EditorUndoManager::MakeSnapshot(Scene& scene, const std::string& label, const std::string& sceneJson) const {
	Snapshot snapshot{};
	snapshot.label = label;
	snapshot.sceneJson = sceneJson;

	GameObject* selectedObject = EditorSelection::GetInstance()->GetSelectedGameObject();
	if (selectedObject && scene.FindGameObjectByInstanceId(selectedObject->GetInstanceId()) == selectedObject) {
		snapshot.selectedObjectInstanceId = selectedObject->GetInstanceId();
	}

	return snapshot;
}

void EditorUndoManager::PushUndo(Snapshot snapshot) {
	if (snapshot.sceneJson.empty()) {
		return;
	}
	undoStack_.push_back(std::move(snapshot));
	TrimUndoStack();
}

void EditorUndoManager::PushRedo(Snapshot snapshot) {
	if (snapshot.sceneJson.empty()) {
		return;
	}
	redoStack_.push_back(std::move(snapshot));
}

bool EditorUndoManager::ApplySnapshot(Scene& scene, const Snapshot& snapshot) {
	SceneJsonImporter::ImportResult result = SceneJsonImporter::ApplySceneJsonString(scene, snapshot.sceneJson);
	if (!result.succeeded) {
		return false;
	}

	scene.UpdateWorldTransforms();
	RestoreSelection(scene, snapshot.selectedObjectInstanceId);
	return true;
}

void EditorUndoManager::TrimUndoStack() {
	if (undoStack_.size() <= maxUndoCount_) {
		return;
	}

	size_t removeCount = undoStack_.size() - maxUndoCount_;
	undoStack_.erase(undoStack_.begin(), undoStack_.begin() + static_cast<std::ptrdiff_t>(removeCount));
}

void EditorUndoManager::RestoreSelection(Scene& scene, const std::string& selectedObjectInstanceId) {
	if (selectedObjectInstanceId.empty()) {
		EditorSelection::GetInstance()->Clear();
		return;
	}

	GameObject* selectedObject = scene.FindGameObjectByInstanceId(selectedObjectInstanceId);
	if (!selectedObject) {
		EditorSelection::GetInstance()->Clear();
		return;
	}

	EditorSelection::GetInstance()->SetSelectedGameObject(selectedObject);
}

} // namespace KujataEngine
