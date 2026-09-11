#include "ProjectAssetClassifier.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace KujataEngine {

ProjectAssetClassifier::ProjectAssetClassifier(const std::filesystem::path& iconDirectory) : iconDirectory_(iconDirectory) {}

ProjectItemViewInfo ProjectAssetClassifier::Classify(const std::filesystem::path& path) const {
	ProjectItemViewInfo viewInfo{};
	std::error_code error;

	// フォルダは空/非空でアイコンを変える。
	// 将来フォルダ種別を増やす場合も、まずここでProjectItemTypeを増やす。
	if (std::filesystem::is_directory(path, error)) {
		if (IsFolderEmpty(path)) {
			viewInfo.type = ProjectItemType::FolderEmpty;
		} else {
			viewInfo.type = ProjectItemType::FolderFilled;
		}
		viewInfo.iconPath = GetIconPath(viewInfo.type);
		return viewInfo;
	}

	if (IsPrefabFile(path)) {
		viewInfo.type = ProjectItemType::PrefabFile;
		viewInfo.iconPath = GetIconPath(viewInfo.type);
		return viewInfo;
	}

	if (IsMaterialFile(path)) {
		viewInfo.type = ProjectItemType::MaterialFile;
		viewInfo.iconPath = GetIconPath(viewInfo.type);
		return viewInfo;
	}

	// 画像ファイルは固定アイコンではなく、実画像をProject Window上のプレビューとして使う。
	if (IsImageFile(path)) {
		viewInfo.type = ProjectItemType::ImageFile;
		viewInfo.usePreview = true;
		return viewInfo;
	}

	// 3DモデルはProjectWindow側で小さなRenderTargetへ描画し、その結果をプレビューとして表示する。
	if (IsModelFile(path)) {
		viewInfo.type = ProjectItemType::ModelFile;
		viewInfo.useModelPreview = true;
		return viewInfo;
	}

	if (IsAudioFile(path)) {
		viewInfo.type = ProjectItemType::AudioFile;
		viewInfo.iconPath = GetIconPath(viewInfo.type);
		return viewInfo;
	}

	// まだ専用分類を持たないファイルは通常ファイルアイコンにする。
	// .json/.cpp/.gltfなどを追加したくなったら、この前に判定を増やす。
	viewInfo.type = ProjectItemType::OtherFile;
	viewInfo.iconPath = GetIconPath(viewInfo.type);
	return viewInfo;
}

bool ProjectAssetClassifier::IsPrefabFile(const std::filesystem::path& path) const {
	std::string fileName = path.filename().string();
	std::transform(fileName.begin(), fileName.end(), fileName.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	return fileName.ends_with(".prefab.json");
}

bool ProjectAssetClassifier::IsMaterialFile(const std::filesystem::path& path) const {
	std::string fileName = path.filename().string();
	std::transform(fileName.begin(), fileName.end(), fileName.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	return fileName.ends_with(".material.json");
}

bool ProjectAssetClassifier::IsImageFile(const std::filesystem::path& path) const {
	std::string extension = path.extension().string();
	// 拡張子は大文字小文字が混ざることがあるため、小文字にそろえて比較する。
	std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	// まずはWICで読み込みやすいpng/jpg/jpegだけを画像プレビュー対象にする。
	if (extension == ".png") {
		return true;
	}
	if (extension == ".jpg") {
		return true;
	}
	if (extension == ".jpeg") {
		return true;
	}

	return false;
}

bool ProjectAssetClassifier::IsModelFile(const std::filesystem::path& path) const {
	std::string extension = path.extension().string();
	// 大文字拡張子の.OBJ/.GLTFも同じ扱いにするため、小文字へそろえる。
	std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	if (extension == ".obj") {
		return true;
	}
	if (extension == ".gltf") {
		return true;
	}
	if (extension == ".glb") {
		return true;
	}

	return false;
}

bool ProjectAssetClassifier::IsAudioFile(const std::filesystem::path& path) const {
	std::string extension = path.extension().string();
	// 拡張子は大文字小文字が混ざることがあるため、小文字にそろえて比較する。
	std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	// Audio用アイコンを出す拡張子はここに集約し、UI描画側に分岐を増やさない。
	if (extension == ".mp3") {
		return true;
	}
	if (extension == ".wav") {
		return true;
	}
	if (extension == ".m4a") {
		return true;
	}

	return false;
}

bool ProjectAssetClassifier::IsFolderEmpty(const std::filesystem::path& path) const {
	std::error_code error;
	bool isEmpty = std::filesystem::is_empty(path, error);
	if (error) {
		// アクセス権や読み込み失敗時は安全側として空フォルダ扱いにする。
		return true;
	}
	return isEmpty;
}

std::filesystem::path ProjectAssetClassifier::GetIconPath(ProjectItemType type) const {
	if (type == ProjectItemType::FolderFilled) {
		return iconDirectory_ / "icon_folder_fill.png";
	}
	if (type == ProjectItemType::FolderEmpty) {
		return iconDirectory_ / "icon_folder_empty.png";
	}
	if (type == ProjectItemType::AudioFile) {
		return iconDirectory_ / "icon_file_audio.png";
	}
	if (type == ProjectItemType::PrefabFile) {
		return iconDirectory_ / "icon_prefab.png";
	}
	if (type == ProjectItemType::MaterialFile) {
		return iconDirectory_ / "icon_file.png";
	}

	return iconDirectory_ / "icon_file.png";
}

} // namespace KujataEngine
