#pragma once

#include "ProjectAssetClassifier.h"

#include "../3d/Camera.h"
#include "../3d/Model.h"
#include "../3d/WorldTransform.h"

#include <d3d12.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <wrl.h>

namespace KujataEngine {

// UnityのProject Windowに近い、ProjectDir以下のファイル閲覧用ウィンドウ。
class ProjectWindow {
public:
	void Initialize();
	void EnsureInitialized();
	void Draw(bool* pOpen = nullptr);
	void RenderModelPreviews();
	void Refresh();

private:
	struct ProjectItem {

		std::filesystem::path absolutePath; // <! 実ファイル操作に使う絶対パス。
		std::string displayName;            // <! UI表示用のファイル名。
		std::string assetId;                // <! .metaで管理されるProject内Asset ID。
		ProjectItemViewInfo viewInfo;       // <! 分類結果とアイコン/プレビュー方針。
		uint32_t textureIndex = 0;          // <! TextureManager上のSRVインデックス。
		bool isDirectory = false;
		bool hasTexture = false;			// trueならtextureIndexをImGui::Imageで表示できる。
	};

	struct ModelPreview {
		// 読み込んだモデル本体。ProjectWindowではファイルごとに一度だけ読み込み、以後は再利用する。
		std::unique_ptr<Model> model;
		WorldTransform worldTransform;
		Camera camera;

		// ImGui::Imageで表示するため、モデルはこの小さなRenderTargetへ描画する。
		Microsoft::WRL::ComPtr<ID3D12Resource> colorResource;
		Microsoft::WRL::ComPtr<ID3D12Resource> depthResource;
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle{};
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle{};
		D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU{};
		uint32_t srvIndex = 0;
		// シーン描画PSOがMRT(RTVFormats[1]=エミッション)前提のため、プレビューにもダミーの
		// エミッションRTを持たせてフォーマットを一致させる(表示には使わない)。
		Microsoft::WRL::ComPtr<ID3D12Resource> emissionResource;
		D3D12_CPU_DESCRIPTOR_HANDLE emissionRtvHandle{};

		bool resourcesCreated = false;
		bool loadFailed = false;
		bool requestedThisFrame = false;
		// サムネイルは一度描けば専用RTに残るので、初回だけ描画して以降は再描画しない(負荷軽減)。
		bool thumbnailRendered = false;
	};

private:
	// 実行時カレントからProjectDirを探す。
	std::filesystem::path DetectProjectRoot() const;
	// パス比較前に表記揺れを減らす。
	std::filesystem::path NormalizePath(const std::filesystem::path& path) const;
	// ProjectDir外へ移動しないためのガード。
	bool IsInsideProjectRoot(const std::filesystem::path& path) const;
	// UIに出す相対パス文字列を作る。
	std::string GetCurrentRelativePathText() const;

	void MoveToDirectory(const std::filesystem::path& directory);
	void MoveToParent();
	void OpenFileInExplorer(const std::filesystem::path& filePath) const;
	bool InstantiatePrefabItem(const std::filesystem::path& prefabPath);
	void CreateMaterialInCurrentDirectory();
	void CreateAnimationClipInCurrentDirectory();
	std::filesystem::path MakeUniqueMaterialPath() const;
	void BeginRenameMaterial(const ProjectItem& item);
	void DrawRenameMaterialPopup();
	/// <summary>
	/// 入力されたMaterial名をファイル名とJSONへ反映します。
	/// </summary>
	bool CommitRenameMaterial();
	void DrawToolbar();
	void DrawItem(ProjectItem& item, int itemIndex);
	bool TryResolveTexture(ProjectItem& item);
	bool TryResolveModelPreview(ProjectItem& item);
	ModelPreview* GetOrCreateModelPreview(const std::filesystem::path& path);
	bool CreateModelPreviewResources(ModelPreview& preview);
	void SetupModelPreviewTransform(ModelPreview& preview);
	void RenderModelPreview(ModelPreview& preview);

private:
	// Project Windowで閲覧できる最上位ディレクトリ。
	std::filesystem::path projectRoot_;
	// 現在表示しているディレクトリ。
	std::filesystem::path currentDirectory_;
	std::unique_ptr<ProjectAssetClassifier> classifier_;
	// Refreshで作り直す、現在ディレクトリ直下の表示項目。
	std::vector<ProjectItem> items_;
	// 読み込み成功したアイコン/プレビューのTextureIndexキャッシュ。
	std::unordered_map<std::string, uint32_t> textureCache_;
	// 読み込み失敗した画像を毎回再試行しないためのキャッシュ。
	std::unordered_map<std::string, bool> failedTextureCache_;
	// モデルプレビューは読み込みとRenderTarget作成が重いため、ファイルパスごとに保持する。
	std::unordered_map<std::string, std::unique_ptr<ModelPreview>> modelPreviewCache_;
	// 今フレームProject Window上に見えているモデルだけをOffscreen描画する。
	std::vector<ModelPreview*> modelPreviewsToRender_;
	std::filesystem::path renameMaterialTargetPath_;
	std::array<char, 256> renameMaterialBuffer_{};
	std::string renameMaterialErrorMessage_;
	float iconSize_ = 32.0f;
	uint32_t modelPreviewSize_ = 96;
	bool initialized_ = false;
	bool requestOpenRenameMaterialPopup_ = false;
};

} // namespace KujataEngine
