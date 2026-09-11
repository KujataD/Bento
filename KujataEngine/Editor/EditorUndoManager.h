#pragma once

#include "../runtime/KujataApi.h"
#include <cstddef>
#include <string>
#include <vector>

namespace KujataEngine {

class Scene;

/// <summary>
/// Editor上のUndo/Redo履歴をSceneスナップショットで管理します。
/// </summary>
class EditorUndoManager {
public:
	/// <summary>
	/// Undo履歴1件分のScene状態。
	/// </summary>
	struct Snapshot {
		std::string label;
		std::string sceneJson;
		std::string selectedObjectInstanceId;
	};

	static KUJATA_API EditorUndoManager* GetInstance();

	/// <summary>
	/// 現在SceneをUndo履歴へ保存します。
	/// </summary>
	KUJATA_API void Capture(Scene& scene, const std::string& label);

	/// <summary>
	/// 指定JSONをUndo履歴へ保存します。
	/// </summary>
	KUJATA_API void Capture(Scene& scene, const std::string& label, const std::string& sceneJson);

	KUJATA_API bool Undo(Scene& scene);
	KUJATA_API bool Redo(Scene& scene);

	KUJATA_API void Clear();

	KUJATA_API void SetMaxUndoCount(size_t maxUndoCount);

	size_t GetMaxUndoCount() const { return maxUndoCount_; }

	bool CanUndo() const { return !undoStack_.empty(); }

	bool CanRedo() const { return !redoStack_.empty(); }

private:
	EditorUndoManager() = default;
	~EditorUndoManager() = default;
	EditorUndoManager(const EditorUndoManager&) = delete;
	EditorUndoManager& operator=(const EditorUndoManager&) = delete;

	Snapshot MakeSnapshot(Scene& scene, const std::string& label, const std::string& sceneJson) const;
	void PushUndo(Snapshot snapshot);
	void PushRedo(Snapshot snapshot);
	bool ApplySnapshot(Scene& scene, const Snapshot& snapshot);
	void TrimUndoStack();
	void RestoreSelection(Scene& scene, const std::string& selectedObjectInstanceId);

	size_t maxUndoCount_ = 20;
	std::vector<Snapshot> undoStack_;
	std::vector<Snapshot> redoStack_;
};

} // namespace KujataEngine
