#pragma once

#include "KujataApi.h"
#include <filesystem>
#include <string>
#include <vector>

namespace KujataEngine {

/// <summary>
/// プロジェクト全体で共有するTagの登録リスト(Unityのタグ管理に相当)。
/// 既定タグを持ち任意のタグを追加でき、ProjectSettings/Tags.json へ永続化する。
/// GameObjectのTag(文字列)自体はGameObjectが保持し、本クラスは「選択肢」を管理する。
/// </summary>
class TagRegistry {
public:
	static KUJATA_API TagRegistry& GetInstance();

	/// <summary>
	/// 登録済みタグ一覧(先頭は常に "Untagged")。
	/// </summary>
	KUJATA_API const std::vector<std::string>& GetTags() const;

	KUJATA_API bool HasTag(const std::string& tag) const;

	/// <summary>
	/// 未登録なら追加する。追加されたら true。空文字は無視。永続パスがあれば保存する。
	/// </summary>
	KUJATA_API bool AddTag(const std::string& tag);

	/// <summary>
	/// tag が未登録なら登録する(Scene由来の未知タグ取り込み用、保存はしない)。
	/// </summary>
	KUJATA_API void EnsureTag(const std::string& tag);

	/// <summary>
	/// projectRoot/ProjectSettings/Tags.json を読み込み、以後の保存先に設定する。
	/// </summary>
	KUJATA_API void LoadFromProjectRoot(const std::filesystem::path& projectRoot);

	/// <summary>
	/// 現在の一覧を保存先へ書き出す(保存先未設定なら何もしない)。
	/// </summary>
	KUJATA_API void Save() const;

	static KUJATA_API const char* UntaggedTag();

private:
	TagRegistry();
	~TagRegistry() = default;
	TagRegistry(const TagRegistry&) = delete;
	TagRegistry& operator=(const TagRegistry&) = delete;

	std::vector<std::string> tags_;
	std::filesystem::path storagePath_;
	bool hasStoragePath_ = false;
};

} // namespace KujataEngine
