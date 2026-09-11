#include "ProjectWindow.h"

#include "AssetDatabase.h"
#include "EditorApplication.h"
#include "PrefabAsset.h"
#include "../base/ProjectPath.h"
#include "EditorSelection.h"
#include "../assets/AnimationClipAsset.h"
#include "../assets/MaterialAsset.h"
#include "../base/DirectXCommon.h"
#include "../base/TextureManager.h"
#include "../scene/GameObject.h"
#include "../scene/Scene.h"
#include "../../externals/imgui/imgui.h"

#include <Windows.h>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>
#include <shellapi.h>
#include <string>
#include <system_error>

namespace KujataEngine {

namespace {

constexpr const char* kProjectPrefabDragPayloadType = "KujataProjectPrefab";
constexpr const char* kProjectMaterialDragPayloadType = "KujataProjectMaterial";
constexpr const char* kProjectTextureDragPayloadType = "KujataProjectTexture";
constexpr const char* kProjectModelDragPayloadType = "KujataProjectModel";
constexpr const char* kProjectAnimClipDragPayloadType = "KujataProjectAnimClip";
constexpr const char* kRenameMaterialPopupName = "Rename Material";

} // namespace

void ProjectWindow::Initialize() {
	// Project Windowの基準は開いているプロジェクトのフォルダ(起動引数 --project。未指定ならエンジンのフォルダ)。
	projectRoot_ = DetectProjectRoot();
	// アセットの実体(Resources/Materials/Prefabs等)はDataフォルダ配下にまとまっているため、
	// AssetDatabaseの基準はProject Windowの表示ルートとは別にDataフォルダへ向ける。
	AssetDatabase::GetInstance().Initialize(GetProjectDataRoot());
	// 起動時はProjectDir直下を表示する。
	currentDirectory_ = projectRoot_;
	// ファイル種別の判定はProjectAssetClassifierに寄せ、UI描画側へ拡張子判定を散らさない。
	classifier_ = std::make_unique<ProjectAssetClassifier>(GetEditorIconDirectory());
	initialized_ = true;
	// 初期表示用にProjectDir直下をスキャンする。
	Refresh();
}

void ProjectWindow::EnsureInitialized() {
	if (initialized_) {
		return;
	}

	Initialize();
}

void ProjectWindow::Draw(bool* pOpen) {
	// ImGuiManagerの初期化順が変わっても落ちないよう、未初期化ならここで初期化する。
	EnsureInitialized();

	// DrawItemで「今フレーム表示されたモデル」を積み直すため、先に前フレーム分をクリアする。
	modelPreviewsToRender_.clear();
	for (const auto& previewPair : modelPreviewCache_) {
		if (previewPair.second) {
			previewPair.second->requestedThisFrame = false;
		}
	}

	ImGui::Begin("Project", pOpen);
	// 現在パス、Back、Refreshなど、Project Window上部の操作UIを描画する。
	DrawToolbar();
	ImGui::Separator();

	if (items_.empty()) {
		ImGui::TextDisabled("No files.");
	}

	// ImGuiのID安定化のため、項目ごとの表示番号もDrawItemへ渡す。
	for (int itemIndex = 0; itemIndex < static_cast<int>(items_.size()); ++itemIndex) {
		DrawItem(items_[itemIndex], itemIndex);
	}

	if (ImGui::BeginPopupContextWindow("ProjectWindowContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
		if (ImGui::BeginMenu("Create")) {
			if (ImGui::MenuItem("Material")) {
				CreateMaterialInCurrentDirectory();
			}
			if (ImGui::MenuItem("Animation Clip")) {
				CreateAnimationClipInCurrentDirectory();
			}
			ImGui::EndMenu();
		}
		ImGui::EndPopup();
	}

	DrawRenameMaterialPopup();

	ImGui::End();
}

std::filesystem::path ProjectWindow::DetectProjectRoot() const {
	return GetActiveProjectRoot();
}

std::filesystem::path ProjectWindow::NormalizePath(const std::filesystem::path& path) const {
	return NormalizeEditorPath(path);
}

bool ProjectWindow::IsInsideProjectRoot(const std::filesystem::path& path) const {
	std::filesystem::path normalizedPath = NormalizePath(path);
	// ProjectDir自身は当然移動可能な範囲として扱う。
	if (normalizedPath == projectRoot_) {
		return true;
	}

	// ProjectDirから見た相対パスに".."が含まれる場合、ProjectDirの外へ出ている。
	std::filesystem::path relative = normalizedPath.lexically_relative(projectRoot_);
	if (relative.empty()) {
		return false;
	}

	for (const std::filesystem::path& part : relative) {
		if (part == "..") {
			return false;
		}
	}

	return true;
}

std::string ProjectWindow::GetCurrentRelativePathText() const {
	std::error_code error;
	// 表示上は絶対パスではなく、ProjectDirからの相対パスにする。
	std::filesystem::path relative = std::filesystem::relative(currentDirectory_, projectRoot_, error);
	if (error) {
		return "(ProjectDir)";
	}
	if (relative.empty()) {
		return "(ProjectDir)";
	}
	if (relative == ".") {
		return "(ProjectDir)";
	}
	return relative.generic_string();
}

void ProjectWindow::Refresh() {
	if (!initialized_) {
		Initialize();
		return;
	}

	// 表示中ディレクトリが削除された場合はProjectDirへ戻す。
	if (!std::filesystem::exists(currentDirectory_)) {
		currentDirectory_ = projectRoot_;
	}

	// Refreshでは現在ディレクトリを再スキャンし、ファイル追加/削除を反映する。
	AssetDatabase::GetInstance().Refresh();
	items_.clear();
	std::error_code error;
	std::filesystem::directory_iterator iterator(currentDirectory_, error);
	if (error) {
		return;
	}

	for (const std::filesystem::directory_entry& entry : iterator) {
		if (AssetDatabase::GetInstance().IsMetaFile(entry.path())) {
			continue;
		}

		ProjectItem item{};
		// 実ファイル操作やExplorer起動には絶対パスを使う。
		item.absolutePath = NormalizePath(entry.path());
		// UI上の表示名はファイル名だけにする。
		item.displayName = entry.path().filename().string();
		item.isDirectory = entry.is_directory(error);
		// フォルダ/画像/通常ファイルの分類と、使用するアイコン方針を取得する。
		item.viewInfo = classifier_->Classify(item.absolutePath);
		if (AssetDatabase::GetInstance().IsAssetFile(item.absolutePath)) {
			item.assetId = AssetDatabase::GetInstance().GetOrCreateAssetId(item.absolutePath);
		}
		// アイコンや画像プレビューのSRV番号を解決する。失敗してもクラッシュさせない。
		TryResolveTexture(item);
		items_.push_back(item);
	}

	// UnityのProject Windowに近い見た目にするため、フォルダを先、ファイルを後に並べる。
	std::sort(items_.begin(), items_.end(), [](const ProjectItem& left, const ProjectItem& right) {
		if (left.isDirectory && !right.isDirectory) {
			return true;
		}
		if (!left.isDirectory && right.isDirectory) {
			return false;
		}
		return left.displayName < right.displayName;
	});
}

void ProjectWindow::MoveToDirectory(const std::filesystem::path& directory) {
	std::filesystem::path normalized = NormalizePath(directory);
	// ProjectDirより上や外部ディレクトリへは移動させない。
	if (!IsInsideProjectRoot(normalized)) {
		return;
	}

	std::error_code error;
	// ファイルを誤ってディレクトリ移動先として扱わないようにする。
	if (!std::filesystem::is_directory(normalized, error)) {
		return;
	}

	// 移動後は即Refreshして、移動先の中身を表示する。
	currentDirectory_ = normalized;
	Refresh();
}

void ProjectWindow::MoveToParent() {
	// ProjectDir直下ではBackしてもそれ以上上へ戻らない。
	if (currentDirectory_ == projectRoot_) {
		return;
	}

	std::filesystem::path parent = NormalizePath(currentDirectory_.parent_path());
	// 念のため、親フォルダがProjectDir外なら移動しない。
	if (!IsInsideProjectRoot(parent)) {
		return;
	}

	currentDirectory_ = parent;
	Refresh();
}

void ProjectWindow::OpenFileInExplorer(const std::filesystem::path& filePath) const {
	if (!std::filesystem::exists(filePath)) {
		return;
	}

	// ファイルのダブルクリックでは、Windows Explorerを開き対象ファイルを選択状態にする。
	std::wstring parameters = L"/select,\"";
	parameters += filePath.wstring();
	parameters += L"\"";
	ShellExecuteW(nullptr, L"open", L"explorer.exe", parameters.c_str(), nullptr, SW_SHOWNORMAL);
}

bool ProjectWindow::InstantiatePrefabItem(const std::filesystem::path& prefabPath) {
	Scene* scene = EditorApplication::GetInstance()->GetCurrentScene();
	if (!scene) {
		return false;
	}

	GameObject* rootObject = PrefabAsset::Instantiate(*scene, prefabPath).rootObject;
	if (!rootObject) {
		return false;
	}

	EditorSelection::GetInstance()->SetSelectedGameObject(rootObject);
	return true;
}

void ProjectWindow::CreateMaterialInCurrentDirectory() {
	std::filesystem::path materialPath = MakeUniqueMaterialPath();
	std::string message;
	if (MaterialAsset::CreateDefaultFile(materialPath, message)) {
		AssetDatabase::GetInstance().GetOrCreateAssetId(materialPath);
		Refresh();
	}
}

void ProjectWindow::CreateAnimationClipInCurrentDirectory() {
	// MakeUniqueMaterialPathと同じ方式で重複しないファイル名を決める。
	std::filesystem::path candidate = currentDirectory_ / "NewAnimation.anim.json";
	for (int index = 1; index < 10000 && std::filesystem::exists(candidate); ++index) {
		candidate = currentDirectory_ / ("NewAnimation " + std::to_string(index) + ".anim.json");
	}

	std::string message;
	if (AnimationClipAsset::CreateDefaultFile(candidate, message)) {
		AssetDatabase::GetInstance().GetOrCreateAssetId(candidate);
		Refresh();
	}
}

std::filesystem::path ProjectWindow::MakeUniqueMaterialPath() const {
	std::filesystem::path candidate = currentDirectory_ / "New Material.material.json";
	if (!std::filesystem::exists(candidate)) {
		return candidate;
	}

	for (int index = 1; index < 10000; ++index) {
		std::string fileName = "New Material " + std::to_string(index) + ".material.json";
		candidate = currentDirectory_ / fileName;
		if (!std::filesystem::exists(candidate)) {
			return candidate;
		}
	}

	return currentDirectory_ / "New Material 9999.material.json";
}

void ProjectWindow::BeginRenameMaterial(const ProjectItem& item) {
	renameMaterialTargetPath_ = item.absolutePath;
	std::string materialName = MaterialAsset::GetDisplayName(item.absolutePath);
	std::memset(renameMaterialBuffer_.data(), 0, renameMaterialBuffer_.size());
	strncpy_s(renameMaterialBuffer_.data(), renameMaterialBuffer_.size(), materialName.c_str(), _TRUNCATE);
	renameMaterialErrorMessage_.clear();
	requestOpenRenameMaterialPopup_ = true;
}

void ProjectWindow::DrawRenameMaterialPopup() {
	if (requestOpenRenameMaterialPopup_) {
		ImGui::OpenPopup(kRenameMaterialPopupName);
		requestOpenRenameMaterialPopup_ = false;
	}

	if (!ImGui::BeginPopupModal(kRenameMaterialPopupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}

	ImGui::TextUnformatted("Material Name");
	ImGui::SetNextItemWidth(260.0f);
	bool enterPressed = ImGui::InputText("##MaterialName", renameMaterialBuffer_.data(), renameMaterialBuffer_.size(), ImGuiInputTextFlags_EnterReturnsTrue);

	if (!renameMaterialErrorMessage_.empty()) {
		ImGui::TextDisabled("%s", renameMaterialErrorMessage_.c_str());
	}

	if (enterPressed) {
		if (CommitRenameMaterial()) {
			ImGui::CloseCurrentPopup();
		}
	}

	if (ImGui::Button("OK")) {
		if (CommitRenameMaterial()) {
			ImGui::CloseCurrentPopup();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel")) {
		renameMaterialTargetPath_.clear();
		renameMaterialErrorMessage_.clear();
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

bool ProjectWindow::CommitRenameMaterial() {
	if (renameMaterialTargetPath_.empty()) {
		renameMaterialErrorMessage_ = "Rename target is empty.";
		return false;
	}

	std::filesystem::path newPath;
	std::string message;
	if (!MaterialAsset::Rename(renameMaterialTargetPath_, renameMaterialBuffer_.data(), newPath, message)) {
		renameMaterialErrorMessage_ = message;
		return false;
	}

	AssetDatabase::GetInstance().GetOrCreateAssetId(newPath);
	EditorSelection::GetInstance()->SetSelectedAsset(newPath, AssetType::Material);
	renameMaterialTargetPath_.clear();
	renameMaterialErrorMessage_.clear();
	Refresh();
	return true;
}

void ProjectWindow::DrawToolbar() {
	// 現在見ている場所をProjectDirからの相対パスで表示する。
	ImGui::Text("Path: %s", GetCurrentRelativePathText().c_str());

	bool canMoveParent = currentDirectory_ != projectRoot_;
	if (!canMoveParent) {
		ImGui::BeginDisabled();
	}
	if (ImGui::Button("Back")) {
		MoveToParent();
	}
	if (!canMoveParent) {
		ImGui::EndDisabled();
	}

	ImGui::SameLine();
	// 外部でファイルを追加/削除した場合はRefreshで再スキャンする。
	if (ImGui::Button("Refresh")) {
		Refresh();
	}
}

void ProjectWindow::DrawItem(ProjectItem& item, int itemIndex) {
	// パス文字列が空または変換不能なケースでもImGui IDが空にならないよう、表示順の番号をIDに使う。
	// ImGuiは空IDに厳しいため、ユーザー作成ファイル名に依存しないIDを先に積む。
	ImGui::PushID(itemIndex);

	// モデルプレビューは毎フレームOffscreen描画するため、表示された項目だけ描画キューへ積む。
	if (item.viewInfo.useModelPreview) {
		TryResolveModelPreview(item);
	}

	if (item.hasTexture) {
		// TextureManagerに読み込まれたSRVを使って、フォルダアイコンまたは画像プレビューを表示する。
		D3D12_GPU_DESCRIPTOR_HANDLE handle = TextureManager::GetInstance()->GetSrvHandle(item.textureIndex);
		ImGui::Image(static_cast<ImTextureID>(handle.ptr), ImVec2(iconSize_, iconSize_));
	}
	else {
		// アイコン読み込みに失敗してもレイアウトが崩れないよう、同じサイズの空白を置く。
		ImGui::Dummy(ImVec2(iconSize_, iconSize_));
	}

	ImGui::SameLine();
	ImGuiSelectableFlags flags = ImGuiSelectableFlags_AllowDoubleClick;
	std::string displayName = item.displayName;
	if (displayName.empty()) {
		displayName = "(unnamed)";
	}
	// ラベルが空になるとImGuiのID assertにつながるため、空名は"(unnamed)"として表示する。
	bool isPrefabFile = item.viewInfo.type == ProjectItemType::PrefabFile;
	bool isMaterialFile = item.viewInfo.type == ProjectItemType::MaterialFile;
	bool isSelectedProjectAsset = false;
	if (isMaterialFile && EditorSelection::GetInstance()->GetSelectedAssetType() == AssetType::Material) {
		isSelectedProjectAsset = NormalizePath(EditorSelection::GetInstance()->GetSelectedAssetPath()) == item.absolutePath;
	}
	if (isPrefabFile) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.47f, 0.74f, 0.68f, 1.0f));
	} else if (isMaterialFile) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.86f, 0.80f, 0.48f, 1.0f));
	}
	if (ImGui::Selectable(displayName.c_str(), isSelectedProjectAsset, flags, ImVec2(0.0f, iconSize_))) {
		if (isMaterialFile) {
			EditorSelection::GetInstance()->SetSelectedAsset(item.absolutePath, AssetType::Material);
		}
	}
	if (isPrefabFile || isMaterialFile) {
		ImGui::PopStyleColor();
	}

	if (isPrefabFile && ImGui::BeginDragDropSource()) {
		std::string pathText = item.absolutePath.generic_string();
		ImGui::SetDragDropPayload(kProjectPrefabDragPayloadType, pathText.c_str(), pathText.size() + 1);
		ImGui::TextUnformatted(displayName.c_str());
		ImGui::EndDragDropSource();
	}

	if (isMaterialFile && ImGui::BeginDragDropSource()) {
		std::string pathText = item.absolutePath.generic_string();
		ImGui::SetDragDropPayload(kProjectMaterialDragPayloadType, pathText.c_str(), pathText.size() + 1);
		ImGui::TextUnformatted(displayName.c_str());
		ImGui::EndDragDropSource();
	}

	// 画像ファイルはテクスチャとしてMaterial InspectorへD&Dできるようにする。
	bool isImageFile = item.viewInfo.type == ProjectItemType::ImageFile;
	if (isImageFile && ImGui::BeginDragDropSource()) {
		std::string pathText = item.absolutePath.generic_string();
		ImGui::SetDragDropPayload(kProjectTextureDragPayloadType, pathText.c_str(), pathText.size() + 1);
		ImGui::TextUnformatted(displayName.c_str());
		ImGui::EndDragDropSource();
	}

	// モデルファイル(.obj/.gltf)はModelRendererのModel欄へD&Dできるようにする。
	bool isModelFile = item.viewInfo.type == ProjectItemType::ModelFile;
	if (isModelFile && ImGui::BeginDragDropSource()) {
		std::string pathText = item.absolutePath.generic_string();
		ImGui::SetDragDropPayload(kProjectModelDragPayloadType, pathText.c_str(), pathText.size() + 1);
		ImGui::TextUnformatted(displayName.c_str());
		ImGui::EndDragDropSource();
	}

	// Animation Clip(*.anim.json)はAnimator/AnimationWindowへD&Dできるようにする。
	bool isAnimClipFile = AnimationClipAsset::IsClipFile(item.absolutePath);
	if (isAnimClipFile && ImGui::BeginDragDropSource()) {
		std::string pathText = item.absolutePath.generic_string();
		ImGui::SetDragDropPayload(kProjectAnimClipDragPayloadType, pathText.c_str(), pathText.size() + 1);
		ImGui::TextUnformatted(displayName.c_str());
		ImGui::EndDragDropSource();
	}

	if (ImGui::BeginPopupContextItem("ProjectItemContext")) {
		if (isMaterialFile) {
			EditorSelection::GetInstance()->SetSelectedAsset(item.absolutePath, AssetType::Material);
		}

		if (isPrefabFile) {
			if (ImGui::MenuItem("Open Prefab Edit Mode")) {
				EditorApplication::GetInstance()->OpenPrefabEditMode(item.absolutePath);
			}
			if (ImGui::MenuItem("Instantiate Prefab")) {
				InstantiatePrefabItem(item.absolutePath);
			}
			ImGui::Separator();
		}

		if (isMaterialFile) {
			if (ImGui::MenuItem("Rename Material")) {
				BeginRenameMaterial(item);
			}
			ImGui::Separator();
		}

		if (!item.assetId.empty()) {
			if (ImGui::MenuItem("Copy Asset ID")) {
				ImGui::SetClipboardText(item.assetId.c_str());
			}
		}

		if (ImGui::MenuItem("Copy File Path")) {
			std::error_code error;
			std::filesystem::path relativePath = std::filesystem::relative(item.absolutePath, projectRoot_, error);

			std::string filePathText;
			if (error || relativePath.empty()) {
				// 失敗時だけ安全側で絶対パスにフォールバック
				filePathText = item.absolutePath.generic_string();
			}
			else {
				// Windowsの \ ではなく、エンジン内で扱いやすい / 区切りにする
				filePathText = relativePath.generic_string();
			}

			ImGui::SetClipboardText(filePathText.c_str());
		}

		if (ImGui::MenuItem("Show in Explorer")) {
			OpenFileInExplorer(item.absolutePath);
		}

		ImGui::EndPopup();
	}

	// フォルダは中へ移動、PrefabはSceneへ生成し、それ以外のファイルはExplorerで選択する。
	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
		if (item.isDirectory) {
			MoveToDirectory(item.absolutePath);
		} else if (isPrefabFile) {
			EditorApplication::GetInstance()->OpenPrefabEditMode(item.absolutePath);
		} else {
			OpenFileInExplorer(item.absolutePath);
		}
	}

	ImGui::PopID();
}

bool ProjectWindow::TryResolveTexture(ProjectItem& item) {
	if (item.viewInfo.useModelPreview) {
		return TryResolveModelPreview(item);
	}

	std::filesystem::path texturePath;
	// 画像ファイルはアイコンではなく画像そのものをプレビューとして使う。
	if (item.viewInfo.usePreview) {
		texturePath = item.absolutePath;
	}
	else {
		texturePath = item.viewInfo.iconPath;
	}

	if (texturePath.empty()) {
		return false;
	}
	if (!std::filesystem::exists(texturePath)) {
		return false;
	}

	std::string textureKey = NormalizePath(texturePath).string();
	// 過去に読み込み失敗したテクスチャは、毎Refreshで再挑戦しない。
	if (failedTextureCache_.contains(textureKey)) {
		return false;
	}

	// ProjectWindow側でも一度解決したTextureIndexをキャッシュする。
	// TextureManagerにもキャッシュはあるが、ここでhasTextureまで即設定できるようにしている。
	if (textureCache_.contains(textureKey)) {
		item.textureIndex = textureCache_[textureKey];
		item.hasTexture = true;
		return true;
	}

	uint32_t textureIndex = 0;
	// Project Windowは任意ファイルを扱うため、失敗してもassertしないTryLoadTextureを使う。
	if (TextureManager::GetInstance()->TryLoadTexture(textureKey, textureIndex)) {
		textureCache_[textureKey] = textureIndex;
		item.textureIndex = textureIndex;
		item.hasTexture = true;
		return true;
	}

	failedTextureCache_[textureKey] = true;

	// 画像プレビューに失敗した場合は、通常ファイルアイコンへフォールバックする。
	if (item.viewInfo.usePreview) {
		item.viewInfo.usePreview = false;
		item.viewInfo.type = ProjectItemType::OtherFile;
		item.viewInfo.iconPath = GetEditorIconDirectory() / "icon_file.png";
		return TryResolveTexture(item);
	}

	return false;
}

bool ProjectWindow::TryResolveModelPreview(ProjectItem& item) {
	ModelPreview* preview = GetOrCreateModelPreview(item.absolutePath);
	if (!preview || preview->loadFailed || !preview->resourcesCreated) {
		// モデル読み込みに失敗した場合もProject Window全体は止めず、通常ファイルアイコンへ戻す。
		item.viewInfo.useModelPreview = false;
		item.viewInfo.type = ProjectItemType::OtherFile;
		item.viewInfo.iconPath = GetEditorIconDirectory() / "icon_file.png";
		item.hasTexture = false;
		return TryResolveTexture(item);
	}

	item.textureIndex = preview->srvIndex;
	item.hasTexture = true;

	// サムネイルは初回だけ描画すればRTに残る。既に描画済みなら再描画キューに積まない(毎フレーム負荷を回避)。
	// 同一フレーム内の重複も requestedThisFrame で防ぐ。
	if (!preview->requestedThisFrame && !preview->thumbnailRendered) {
		preview->requestedThisFrame = true;
		modelPreviewsToRender_.push_back(preview);
	}

	return true;
}

ProjectWindow::ModelPreview* ProjectWindow::GetOrCreateModelPreview(const std::filesystem::path& path) {
	std::string previewKey = NormalizePath(path).string();
	if (modelPreviewCache_.contains(previewKey)) {
		return modelPreviewCache_[previewKey].get();
	}

	std::unique_ptr<ModelPreview> preview = std::make_unique<ModelPreview>();

	if (!CreateModelPreviewResources(*preview)) {
		preview->loadFailed = true;
		modelPreviewCache_[previewKey] = std::move(preview);
		return modelPreviewCache_[previewKey].get();
	}

	// Assimpで.obj/.gltfを読み込む。マテリアルにdiffuse textureがあればModel側でTextureManagerへ渡される。
	preview->model.reset(Model::TryCreateFromFile(previewKey, ShaderModel::kBlingPhongReflection));
	if (!preview->model) {
		preview->loadFailed = true;
		modelPreviewCache_[previewKey] = std::move(preview);
		return modelPreviewCache_[previewKey].get();
	}

	// プレビュー用のカメラとTransformは、通常ゲームシーンとは独立した最小構成にする。
	preview->camera.Initialize();
	preview->camera.translation_ = { 0.0f, 0.0f, -3.0f };
	preview->camera.rotation_ = { 0.0f, 0.0f, 0.0f };
	preview->camera.aspectRatio = 1.0f;
	preview->camera.fovAngleY = 0.65f;
	preview->camera.UpdateMatrix();

	preview->worldTransform.Initialize();
	SetupModelPreviewTransform(*preview);

	modelPreviewCache_[previewKey] = std::move(preview);
	return modelPreviewCache_[previewKey].get();
}

bool ProjectWindow::CreateModelPreviewResources(ModelPreview& preview) {
	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	ID3D12Device* device = dxCommon->GetDevice();

	// プレビューはkObject3d PSO(HDRフォーマット固定)で描くため、RTもHDRに合わせる。
	// FLOAT値はImGui経由でリニア→sRGBエンコードされて表示されるので、トーンマップ無しでも見た目はほぼ従来通り。
	D3D12_RESOURCE_DESC colorDesc{};
	colorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	colorDesc.Width = modelPreviewSize_;
	colorDesc.Height = modelPreviewSize_;
	colorDesc.DepthOrArraySize = 1;
	colorDesc.MipLevels = 1;
	colorDesc.Format = DirectXCommon::kSceneColorFormat;
	colorDesc.SampleDesc.Count = 1;
	colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_CLEAR_VALUE colorClearValue{};
	colorClearValue.Format = DirectXCommon::kSceneColorFormat;
	colorClearValue.Color[0] = 0.08f;
	colorClearValue.Color[1] = 0.08f;
	colorClearValue.Color[2] = 0.09f;
	colorClearValue.Color[3] = 1.0f;

	HRESULT hr = device->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&colorDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&colorClearValue,
		IID_PPV_ARGS(&preview.colorResource));
	if (FAILED(hr)) {
		OutputDebugStringA("Failed to create Project model preview color resource.\n");
		return false;
	}

	uint32_t rtvIndex = dxCommon->AllocateRtvIndex();
	preview.rtvHandle = dxCommon->GetRtvDescriptorHeap()->GetCPUDescriptorHandleForHeapStart();
	preview.rtvHandle.ptr += static_cast<SIZE_T>(dxCommon->GetDescriptorSizeRTV()) * rtvIndex;

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DirectXCommon::kSceneColorFormat;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	device->CreateRenderTargetView(preview.colorResource.Get(), &rtvDesc, preview.rtvHandle);

	uint32_t srvIndex = dxCommon->AllocateSrvIndex();
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU = dxCommon->GetSrvDescriptorHeap()->GetCPUDescriptorHandleForHeapStart();
	srvHandleCPU.ptr += static_cast<SIZE_T>(dxCommon->GetDescriptorSizeSRV()) * srvIndex;
	preview.srvHandleGPU = dxCommon->GetSrvDescriptorHeap()->GetGPUDescriptorHandleForHeapStart();
	preview.srvHandleGPU.ptr += static_cast<SIZE_T>(dxCommon->GetDescriptorSizeSRV()) * srvIndex;
	preview.srvIndex = srvIndex;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DirectXCommon::kSceneColorFormat;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView(preview.colorResource.Get(), &srvDesc, srvHandleCPU);

	// エミッション用ダミーRT(MRT slot1)。PSOのRTVFormats[1]と一致させるために必要。
	D3D12_RESOURCE_DESC emissionDesc = colorDesc;
	emissionDesc.Format = DirectXCommon::kSceneEmissionFormat;

	D3D12_CLEAR_VALUE emissionClearValue{};
	emissionClearValue.Format = DirectXCommon::kSceneEmissionFormat;
	emissionClearValue.Color[3] = 1.0f;

	hr = device->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&emissionDesc,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		&emissionClearValue,
		IID_PPV_ARGS(&preview.emissionResource));
	if (FAILED(hr)) {
		OutputDebugStringA("Failed to create Project model preview emission resource.\n");
		return false;
	}

	uint32_t emissionRtvIndex = dxCommon->AllocateRtvIndex();
	preview.emissionRtvHandle = dxCommon->GetRtvDescriptorHeap()->GetCPUDescriptorHandleForHeapStart();
	preview.emissionRtvHandle.ptr += static_cast<SIZE_T>(dxCommon->GetDescriptorSizeRTV()) * emissionRtvIndex;

	D3D12_RENDER_TARGET_VIEW_DESC emissionRtvDesc{};
	emissionRtvDesc.Format = DirectXCommon::kSceneEmissionFormat;
	emissionRtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	device->CreateRenderTargetView(preview.emissionResource.Get(), &emissionRtvDesc, preview.emissionRtvHandle);

	D3D12_RESOURCE_DESC depthDesc{};
	depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthDesc.Width = modelPreviewSize_;
	depthDesc.Height = modelPreviewSize_;
	depthDesc.DepthOrArraySize = 1;
	depthDesc.MipLevels = 1;
	depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthDesc.SampleDesc.Count = 1;
	depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthClearValue.DepthStencil.Depth = 1.0f;

	hr = device->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&depthDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthClearValue,
		IID_PPV_ARGS(&preview.depthResource));
	if (FAILED(hr)) {
		OutputDebugStringA("Failed to create Project model preview depth resource.\n");
		return false;
	}

	uint32_t dsvIndex = dxCommon->AllocateDsvIndex();
	preview.dsvHandle = dxCommon->GetDsvDescriptorHeap()->GetCPUDescriptorHandleForHeapStart();
	preview.dsvHandle.ptr += static_cast<SIZE_T>(dxCommon->GetDescriptorSizeDSV()) * dsvIndex;

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	device->CreateDepthStencilView(preview.depthResource.Get(), &dsvDesc, preview.dsvHandle);

	preview.resourcesCreated = true;
	return true;
}

void ProjectWindow::SetupModelPreviewTransform(ModelPreview& preview) {
	const std::vector<VertexData>& vertices = preview.model->GetVertices();
	if (vertices.empty()) {
		return;
	}

	Vector3 minPosition = {
		(std::numeric_limits<float>::max)(),
		(std::numeric_limits<float>::max)(),
		(std::numeric_limits<float>::max)(),
	};
	Vector3 maxPosition = {
		std::numeric_limits<float>::lowest(),
		std::numeric_limits<float>::lowest(),
		std::numeric_limits<float>::lowest(),
	};

	for (const VertexData& vertex : vertices) {
		minPosition.x = (std::min)(minPosition.x, vertex.position.x);
		minPosition.y = (std::min)(minPosition.y, vertex.position.y);
		minPosition.z = (std::min)(minPosition.z, vertex.position.z);
		maxPosition.x = (std::max)(maxPosition.x, vertex.position.x);
		maxPosition.y = (std::max)(maxPosition.y, vertex.position.y);
		maxPosition.z = (std::max)(maxPosition.z, vertex.position.z);
	}

	Vector3 center{};
	center.x = (minPosition.x + maxPosition.x) * 0.5f;
	center.y = (minPosition.y + maxPosition.y) * 0.5f;
	center.z = (minPosition.z + maxPosition.z) * 0.5f;

	Vector3 extent{};
	extent.x = maxPosition.x - minPosition.x;
	extent.y = maxPosition.y - minPosition.y;
	extent.z = maxPosition.z - minPosition.z;

	float maxExtent = (std::max)(extent.x, (std::max)(extent.y, extent.z));
	float previewScale = 1.0f;
	if (std::isfinite(maxExtent) && maxExtent > 0.0001f) {
		previewScale = 1.6f / maxExtent;
	}

	// モデルの中心を原点付近へ寄せてから少し回転し、形が分かりやすい角度でサムネイル化する。
	preview.worldTransform.scale_ = { previewScale, previewScale, previewScale };
	preview.worldTransform.rotation_ = { 0.25f, -0.45f, 0.0f };
	preview.worldTransform.translation_ = {
		-center.x * previewScale,
		-center.y * previewScale,
		-center.z * previewScale,
	};
	preview.worldTransform.UpdateMatrix(preview.camera);
}

void ProjectWindow::RenderModelPreviews() {
	if (modelPreviewsToRender_.empty()) {
		return;
	}

	for (ModelPreview* preview : modelPreviewsToRender_) {
		if (!preview) {
			continue;
		}
		if (preview->loadFailed || !preview->model || !preview->resourcesCreated) {
			continue;
		}
		RenderModelPreview(*preview);
	}

	// サムネイル描画でRenderTargetを差し替えたため、後続のImGui描画先をバックバッファへ戻す。
	DirectXCommon::GetInstance()->SetBackBufferRenderTarget();
}

void ProjectWindow::RenderModelPreview(ModelPreview& preview) {
	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();

	D3D12_RESOURCE_BARRIER beginBarrier{};
	beginBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	beginBarrier.Transition.pResource = preview.colorResource.Get();
	beginBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	beginBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	beginBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	commandList->ResourceBarrier(1, &beginBarrier);

	// MRT: [0]=カラー / [1]=エミッション(ダミー)。シーン描画PSOのRTVFormatsと形を合わせる。
	D3D12_CPU_DESCRIPTOR_HANDLE previewRtvHandles[2] = {preview.rtvHandle, preview.emissionRtvHandle};
	commandList->OMSetRenderTargets(2, previewRtvHandles, false, &preview.dsvHandle);

	const float clearColor[4] = { 0.08f, 0.08f, 0.09f, 1.0f };
	commandList->ClearRenderTargetView(preview.rtvHandle, clearColor, 0, nullptr);
	commandList->ClearDepthStencilView(preview.dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	Model::PreDraw();

	// Model::PreDrawは通常ウィンドウサイズのViewportを入れるため、プレビュー用サイズで上書きする。
	D3D12_VIEWPORT viewport{};
	viewport.Width = static_cast<float>(modelPreviewSize_);
	viewport.Height = static_cast<float>(modelPreviewSize_);
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	commandList->RSSetViewports(1, &viewport);

	D3D12_RECT scissorRect{};
	scissorRect.left = 0;
	scissorRect.top = 0;
	scissorRect.right = static_cast<LONG>(modelPreviewSize_);
	scissorRect.bottom = static_cast<LONG>(modelPreviewSize_);
	commandList->RSSetScissorRects(1, &scissorRect);

	preview.camera.UpdateMatrix();
	preview.worldTransform.UpdateMatrix(preview.camera);
	preview.model->Draw(preview.worldTransform, preview.camera);

	D3D12_RESOURCE_BARRIER endBarrier{};
	endBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	endBarrier.Transition.pResource = preview.colorResource.Get();
	endBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	endBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	endBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	commandList->ResourceBarrier(1, &endBarrier);

	// 描画済みマーク。以降は再描画キューに積まれず、RTに残った絵をそのまま表示する。
	preview.thumbnailRendered = true;
}

} // namespace KujataEngine
