#pragma once

#include <filesystem>

namespace KujataEngine {

// Project Window上で表示するアセット種別。
// 種別を増やす場合は、ここに追加してProjectAssetClassifier::Classifyで判定する。
enum class ProjectItemType {
	FolderEmpty,
	FolderFilled,
	PrefabFile,
	MaterialFile,
	ImageFile,
	ModelFile,
	AudioFile,
	OtherFile,
};

struct ProjectItemViewInfo {
	// 表示対象の分類結果。
	ProjectItemType type = ProjectItemType::OtherFile;

	// trueの場合は固定アイコンではなく、対象ファイル自体を画像プレビューとして表示する。
	bool usePreview = false;

	// trueの場合はモデルを小さなRenderTargetへ描画してプレビューとして表示する。
	bool useModelPreview = false;

	// 固定アイコンを使う場合の画像パス。
	std::filesystem::path iconPath;
};

/// <summary>
/// プロジェクトアセットのファイル型によって分類します。
/// </summary>
class ProjectAssetClassifier {
public:
	explicit ProjectAssetClassifier(const std::filesystem::path& iconDirectory);

	ProjectItemViewInfo Classify(const std::filesystem::path& path) const;

	bool IsPrefabFile(const std::filesystem::path& path) const;

	bool IsMaterialFile(const std::filesystem::path& path) const;

	bool IsImageFile(const std::filesystem::path& path) const;

	/// <summary>
	/// モデルプレビュー対象かどうかを拡張子で判定する。
	/// </summary>
	bool IsModelFile(const std::filesystem::path& path) const;

	bool IsAudioFile(const std::filesystem::path& path) const;

private:

	bool IsFolderEmpty(const std::filesystem::path& path) const;

	std::filesystem::path GetIconPath(ProjectItemType type) const;

private:
	// 固定アイコン画像のフォルダ(エンジン側の持ち物)。
	std::filesystem::path iconDirectory_;
};

} // namespace KujataEngine
