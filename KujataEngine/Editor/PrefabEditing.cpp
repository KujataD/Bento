#include "PrefabEditing.h"

#include "EditorConsole.h"
#include "EditorSelection.h"
#include "EditorUndoManager.h"
#include "PrefabAsset.h"
#include "../base/ProjectPath.h"
#include "../scene/GameObject.h"
#include "../scene/Scene.h"

namespace KujataEngine {

namespace {

void Log(const std::string& message, EditorLogLevel level) { EditorConsole::GetInstance()->AddLog("[Prefab] " + message, level); }

PrefabEditing::Result Fail(const std::string& message) {
	Log(message, EditorLogLevel::Error);
	PrefabEditing::Result result;
	result.message = message;
	return result;
}

} // namespace

namespace PrefabEditing {

Result Create(Scene& scene, GameObject& object, const std::string& undoLabel) {
	EditorUndoManager::GetInstance()->Capture(scene, undoLabel);
	PrefabAsset::SaveResult saveResult = PrefabAsset::SaveAsPrefab(object, GetProjectDataRoot());
	if (!saveResult.succeeded) {
		return Fail("Save failed: " + saveResult.message);
	}
	PrefabAsset::BindHierarchyToPrefab(object, saveResult.outputPath);
	Log("Saved: " + saveResult.outputPath.string(), EditorLogLevel::Info);

	Result result;
	result.succeeded = true;
	result.object = &object;
	result.prefabPath = saveResult.outputPath;
	return result;
}

Result Instantiate(Scene& scene, const std::filesystem::path& prefabPath, GameObject* parent, const std::string& undoLabel) {
	EditorUndoManager::GetInstance()->Capture(scene, undoLabel);
	PrefabAsset::InstantiateResult instantiateResult = PrefabAsset::Instantiate(scene, prefabPath);
	if (!instantiateResult.succeeded || !instantiateResult.rootObject) {
		return Fail("Instantiate failed: " + instantiateResult.message);
	}
	if (parent) {
		instantiateResult.rootObject->SetParent(parent);
	}
	EditorSelection::GetInstance()->SetSelectedGameObject(instantiateResult.rootObject);
	Log("Instantiated: " + prefabPath.string(), EditorLogLevel::Info);

	Result result;
	result.succeeded = true;
	result.object = instantiateResult.rootObject;
	result.prefabPath = prefabPath;
	return result;
}

Result Apply(Scene& scene, GameObject& instance) {
	// Applyはシーンではなくプレハブファイルを書き換えるので、シーンのUndoは取らない。
	PrefabAsset::SaveResult saveResult = PrefabAsset::ApplyPrefabInstance(scene, instance);
	if (!saveResult.succeeded) {
		return Fail("Apply failed: " + saveResult.message);
	}
	Log("Applied: " + saveResult.outputPath.string(), EditorLogLevel::Info);

	Result result;
	result.succeeded = true;
	result.object = &instance;
	result.prefabPath = saveResult.outputPath;
	return result;
}

Result Revert(Scene& scene, GameObject& instance, const std::string& undoLabel) {
	EditorUndoManager::GetInstance()->Capture(scene, undoLabel);
	PrefabAsset::InstantiateResult revertResult = PrefabAsset::RevertPrefabInstance(scene, instance);
	if (!revertResult.succeeded) {
		return Fail("Revert failed: " + revertResult.message);
	}
	// Revertはオブジェクトを作り直すので、選択も新しいルートへ移す(消えたオブジェクトを指さないように)。
	EditorSelection::GetInstance()->SetSelectedGameObject(revertResult.rootObject);
	Log("Reverted.", EditorLogLevel::Info);

	Result result;
	result.succeeded = true;
	result.object = revertResult.rootObject;
	return result;
}

Result Unpack(Scene& scene, GameObject& instance, const std::string& undoLabel) {
	EditorUndoManager::GetInstance()->Capture(scene, undoLabel);
	if (!PrefabAsset::UnpackPrefabInstance(scene, instance)) {
		return Fail("Unpack failed: プレハブのインスタンスではありません。");
	}
	Log("Unpacked.", EditorLogLevel::Info);

	Result result;
	result.succeeded = true;
	result.object = &instance;
	return result;
}

} // namespace PrefabEditing

} // namespace KujataEngine
