#pragma once

#include "../runtime/KujataApi.h"
#include <cstddef>
#include <filesystem>
#include <string>

namespace KujataEngine {

class GameObject;
class Scene;

/// <summary>
/// GameObject構成をPrefabファイルとして保存・復元する
/// </summary>
class PrefabAsset {
public:
	struct SaveResult {
		bool succeeded = false;
		std::filesystem::path outputPath;
		size_t gameObjectCount = 0;
		size_t componentCount = 0;
		std::string message;
	};

	struct InstantiateResult {
		bool succeeded = false;
		GameObject* rootObject = nullptr;
		size_t gameObjectCount = 0;
		size_t componentCount = 0;
		std::string message;
	};

	/// <summary>
	/// GameObjectと子階層をProjectDir配下のPrefabsへ保存する
	/// </summary>
	KUJATA_API static SaveResult SaveAsPrefab(const GameObject& rootObject, const std::filesystem::path& projectRoot);

	/// <summary>
	/// GameObjectと子階層を指定Prefabファイルへ保存する
	/// </summary>
	KUJATA_API static SaveResult SaveToPath(const GameObject& rootObject, const std::filesystem::path& prefabPath);

	/// <summary>
	/// Prefabファイルから新しいGameObject階層をSceneへ生成する
	/// </summary>
	KUJATA_API static InstantiateResult Instantiate(Scene& scene, const std::filesystem::path& prefabPath, bool linkInstance = true);

	/// <summary>
	/// GameObject階層をファイルに書かずJSON文字列へ書き出す(Hierarchyのコピー用)。
	/// 中身はPrefabと同じ形式なので、InstantiateFromJsonでそのまま復元できる。
	/// </summary>
	KUJATA_API static std::string CopyHierarchyToJson(const GameObject& rootObject);

	/// <summary>
	/// CopyHierarchyToJsonで作ったJSONから新しいGameObject階層をSceneへ生成する(ペースト用)。
	/// Prefabとしての関連付けは行わない(独立したObjectになる)。
	/// </summary>
	KUJATA_API static InstantiateResult InstantiateFromJson(Scene& scene, const std::string& prefabJsonText);

	/// <summary>
	/// 既存GameObject階層をPrefab Instanceとして関連付ける
	/// </summary>
	KUJATA_API static void BindHierarchyToPrefab(GameObject& rootObject, const std::filesystem::path& prefabPath);

	/// <summary>
	/// Prefab Instanceを参照元Prefabの内容へ戻す
	/// </summary>
	KUJATA_API static InstantiateResult RevertPrefabInstance(Scene& scene, GameObject& instanceObject);

	/// <summary>
	/// Prefab Instanceの変更をPrefabファイルへ適用する
	/// </summary>
	KUJATA_API static SaveResult ApplyPrefabInstance(Scene& scene, GameObject& instanceObject);

	/// <summary>
	/// Prefab Instanceの関連付けを解除する
	/// </summary>
	KUJATA_API static bool UnpackPrefabInstance(Scene& scene, GameObject& instanceObject);

	/// <summary>
	/// 選択ObjectからPrefab Instanceルートを取得する
	/// </summary>
	KUJATA_API static GameObject* FindPrefabInstanceRoot(Scene& scene, GameObject& object);

	/// <summary>
	/// Scene内の同じPrefab InstanceをPrefabファイルから更新する
	/// </summary>
	KUJATA_API static size_t RefreshInstancesFromPrefab(Scene& scene, const std::filesystem::path& prefabPath, GameObject* ignoreRoot = nullptr);
};

} // namespace KujataEngine
