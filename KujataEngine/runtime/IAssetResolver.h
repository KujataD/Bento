#pragma once

#include <filesystem>
#include <string>

namespace KujataEngine {

// Material等のアセットIDと実ファイルパスの相互解決を提供する契約。
// 実装(AssetDatabase)はEditor側にあり、ランタイムはこの抽象越しに使う(依存性逆転)。
class IAssetResolver {
public:
	virtual ~IAssetResolver() = default;

	// Asset IDを実ファイルパスへ解決する。失敗時はfallbackPathを返す。
	virtual std::filesystem::path ResolveAssetPath(const std::string& assetId, const std::filesystem::path& fallbackPath) = 0;

	// 指定ファイルのAsset IDを取得する(必要なら生成する)。
	virtual std::string GetOrCreateAssetId(const std::filesystem::path& assetPath) = 0;

	// ProjectDir基準の相対パス文字列を作る。
	virtual std::string MakeProjectRelativePath(const std::filesystem::path& assetPath) const = 0;
};

} // namespace KujataEngine
