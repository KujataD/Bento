#pragma once

#include "../runtime/IAssetResolver.h"
#include "../runtime/KujataApi.h"
#include <filesystem>
#include <string>
#include <unordered_map>

namespace KujataEngine {

/// <summary>
/// ProjectWindowやScene保存で扱うAsset種別。
/// </summary>
enum class AssetType {
	Unknown,
	Texture,
	Model,
	Scene,
	Prefab,
	Material,
};

/// <summary>
/// .metaから復元したAssetの識別情報。
/// </summary>
struct AssetInfo {
	std::string assetId;
	AssetType type = AssetType::Unknown;
	std::filesystem::path assetPath;
	std::filesystem::path metaPath;
	std::string relativePath;
};

/// <summary>
/// ProjectDir配下のAsset IDと実ファイルパスを対応付ける簡易AssetDatabase。
/// </summary>
class AssetDatabase : public IAssetResolver {
public:
	static KUJATA_API AssetDatabase& GetInstance();

	KUJATA_API void Initialize(const std::filesystem::path& projectRoot);

	/// <summary>
	/// 既存.metaを読み直し、Asset IDとパスの対応を更新します。
	/// </summary>
	KUJATA_API void Refresh();

	/// <summary>
	/// 指定ファイルの.metaを保証し、Asset IDを取得します。
	/// </summary>
	KUJATA_API std::string GetOrCreateAssetId(const std::filesystem::path& assetPath) override;

	KUJATA_API const AssetInfo* FindById(const std::string& assetId);

	KUJATA_API const AssetInfo* FindByPath(const std::filesystem::path& assetPath);

	/// <summary>
	/// Asset IDを実ファイルパスへ解決します。失敗時はfallbackPathを返します。
	/// </summary>
	KUJATA_API std::filesystem::path ResolveAssetPath(const std::string& assetId, const std::filesystem::path& fallbackPath) override;

	KUJATA_API std::string MakeProjectRelativePath(const std::filesystem::path& assetPath) const override;

	KUJATA_API AssetType ClassifyAssetType(const std::filesystem::path& assetPath) const;

	KUJATA_API bool IsAssetFile(const std::filesystem::path& assetPath) const;

	KUJATA_API bool IsMetaFile(const std::filesystem::path& path) const;

	/// <summary>
	/// AssetTypeをJSON保存用文字列へ変換します。
	/// </summary>
	static KUJATA_API const char* ToString(AssetType type);

private:
	AssetDatabase() = default;
	~AssetDatabase() = default;
	AssetDatabase(const AssetDatabase&) = delete;
	AssetDatabase& operator=(const AssetDatabase&) = delete;

	std::filesystem::path NormalizePath(const std::filesystem::path& path) const;
	std::filesystem::path ResolveProjectPath(const std::filesystem::path& path) const;
	std::string MakePathKey(const std::filesystem::path& path) const;
	std::filesystem::path GetMetaPath(const std::filesystem::path& assetPath) const;
	bool ReadMeta(const std::filesystem::path& metaPath, AssetInfo& outInfo) const;
	bool WriteMeta(const AssetInfo& info) const;
	void RegisterAssetInfo(const AssetInfo& info);
	std::string GenerateAssetId() const;
	void EnsureInitialized();

private:
	std::filesystem::path projectRoot_;
	std::unordered_map<std::string, AssetInfo> assetsById_;
	std::unordered_map<std::string, std::string> assetIdByPath_;
	bool initialized_ = false;
};

} // namespace KujataEngine
