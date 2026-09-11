#include "MaterialInspector.h"

#include "../../externals/imgui/imgui.h"
#include "../../externals/imsearch/imsearch.h"
#include "../assets/MaterialAsset.h"
#include "../scene/Component.h"
#include "../scene/GameObject.h"
#include "../scene/IMaterialTarget.h"
#include "../scene/Scene.h"
#include "AssetDatabase.h"
#include "EditorApplication.h"
#include "EditorImGuiUtil.h"
#include "../base/ProjectPath.h"
#include "../base/TextureManager.h"
#include "EditorSelection.h"
#include "ProjectWindow.h"
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

// OSファイル選択ダイアログ(IFileDialog)用。
#include <Windows.h>
#include <shobjidl.h>
#pragma comment(lib, "ole32.lib")

namespace KujataEngine {
namespace {

struct MaterialInspectorState {
	std::filesystem::path materialPath;
	MaterialAssetData material;
	std::array<char, 256> nameBuffer{};
	std::array<char, 256> baseColorTextureBuffer{};
	std::array<char, 256> normalTextureBuffer{};
	std::array<char, 256> environmentTextureBuffer{};
	std::array<char, 256> emissiveTextureBuffer{};
	std::string errorMessage;
	bool loaded = false;
};

MaterialInspectorState& GetMaterialInspectorState() {
	static MaterialInspectorState state;
	return state;
}

void CopyTextToBuffer(std::array<char, 256>& buffer, const std::string& text) {
	std::memset(buffer.data(), 0, buffer.size());
	strncpy_s(buffer.data(), buffer.size(), text.c_str(), _TRUNCATE);
}

void FillMaterialInspectorBuffers(MaterialInspectorState& state) {
	CopyTextToBuffer(state.nameBuffer, state.material.name);
	CopyTextToBuffer(state.baseColorTextureBuffer, MaterialAsset::GetTexturePath(state.material, MaterialTextureSlot::BaseColor));
	CopyTextToBuffer(state.normalTextureBuffer, MaterialAsset::GetTexturePath(state.material, MaterialTextureSlot::Normal));
	CopyTextToBuffer(state.environmentTextureBuffer, MaterialAsset::GetTexturePath(state.material, MaterialTextureSlot::Environment));
	CopyTextToBuffer(state.emissiveTextureBuffer, MaterialAsset::GetTexturePath(state.material, MaterialTextureSlot::Emissive));
}

void ApplyMaterialInspectorBuffers(MaterialInspectorState& state) {
	MaterialAsset::SetTexture(state.material, MaterialTextureSlot::BaseColor, "", state.baseColorTextureBuffer.data());
	MaterialAsset::SetTexture(state.material, MaterialTextureSlot::Normal, "", state.normalTextureBuffer.data());
	MaterialAsset::SetTexture(state.material, MaterialTextureSlot::Environment, "", state.environmentTextureBuffer.data());
	MaterialAsset::SetTexture(state.material, MaterialTextureSlot::Emissive, "", state.emissiveTextureBuffer.data());
}

bool LoadMaterialInspectorState(const std::filesystem::path& materialPath, MaterialInspectorState& state) {
	std::filesystem::path normalizedPath = NormalizeEditorPath(materialPath);
	if (state.loaded && state.materialPath == normalizedPath) {
		return true;
	}

	state.materialPath = normalizedPath;
	state.material = MaterialAsset::CreateDefault();
	state.errorMessage.clear();
	state.loaded = false;

	std::string message;
	if (!MaterialAsset::Load(normalizedPath, state.material, message)) {
		state.errorMessage = message;
		FillMaterialInspectorBuffers(state);
		return false;
	}

	FillMaterialInspectorBuffers(state);
	state.loaded = true;
	return true;
}

bool SaveMaterialInspectorState(MaterialInspectorState& state) {
	std::string message;
	if (!MaterialAsset::Save(state.materialPath, state.material, message)) {
		state.errorMessage = message;
		return false;
	}

	AssetDatabase::GetInstance().GetOrCreateAssetId(state.materialPath);
	state.errorMessage.clear();
	return true;
}

// 選択したテクスチャパスをassetId解決付きでMaterialへ反映し、表示用バッファも更新する。
void ApplyTextureSelection(MaterialInspectorState& state, MaterialTextureSlot slot, std::array<char, 256>& buffer, const std::filesystem::path& texturePath) {
	AssetDatabase& db = AssetDatabase::GetInstance();
	// プロジェクト相対パスを保存パスに、assetIdを安定参照として保存する(従来はpathのみでassetIdが空だった)。
	std::string relativePath = db.MakeProjectRelativePath(texturePath);
	if (relativePath.empty()) {
		relativePath = texturePath.generic_string();
	}
	std::string assetId = db.GetOrCreateAssetId(texturePath);
	MaterialAsset::SetTexture(state.material, slot, assetId, relativePath);
	CopyTextToBuffer(buffer, relativePath);
}

// プロジェクト内の画像ファイル(.png/.jpg/.jpeg)を列挙する。
std::vector<std::filesystem::path> EnumerateProjectTextures() {
	std::vector<std::filesystem::path> textures;
	std::filesystem::path projectRoot = GetActiveProjectRoot();
	if (projectRoot.empty()) {
		return textures;
	}

	std::error_code errorCode;
	std::filesystem::recursive_directory_iterator iterator(projectRoot, std::filesystem::directory_options::skip_permission_denied, errorCode);
	std::filesystem::recursive_directory_iterator end;
	for (; iterator != end; iterator.increment(errorCode)) {
		if (errorCode) {
			break;
		}
		if (!iterator->is_regular_file(errorCode)) {
			continue;
		}
		std::filesystem::path extension = iterator->path().extension();
		std::string ext = extension.string();
		for (char& character : ext) {
			character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
		}
		if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
			textures.push_back(iterator->path());
		}
	}
	return textures;
}

// OSのファイル選択ダイアログで画像ファイルを1つ選ぶ。選択されたらtrueとパスを返す。
bool OpenTextureFileDialog(std::filesystem::path& outPath) {
	HRESULT initResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	const bool shouldUninitialize = SUCCEEDED(initResult);

	bool picked = false;
	IFileOpenDialog* fileDialog = nullptr;
	if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fileDialog)))) {
		const COMDLG_FILTERSPEC filters[] = {
		    {L"Image Files", L"*.png;*.jpg;*.jpeg"},
		    {L"All Files", L"*.*"},
		};
		fileDialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);

		if (SUCCEEDED(fileDialog->Show(nullptr))) {
			IShellItem* item = nullptr;
			if (SUCCEEDED(fileDialog->GetResult(&item))) {
				PWSTR filePath = nullptr;
				if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &filePath))) {
					outPath = std::filesystem::path(filePath);
					picked = true;
					CoTaskMemFree(filePath);
				}
				item->Release();
			}
		}
		fileDialog->Release();
	}

	if (shouldUninitialize) {
		CoUninitialize();
	}
	return picked;
}

// 1テクスチャスロット分のUI(手動入力 + D&D + 一覧選択 + ファイルダイアログ)。変更があればtrue。
bool DrawTextureSlotEditor(const char* label, const char* popupId, MaterialInspectorState& state, MaterialTextureSlot slot, std::array<char, 256>& buffer) {
	bool changed = false;
	ImGui::PushID(label);

	// UnityのようにテクスチャをImageプレビューで表示する(パス入力欄は廃止)。
	// 現在のパスからSRVを解決(エディタUpdate内なので読み込み=コマンドリスト実行が安全)。
	TextureManager* textureManager = TextureManager::GetInstance();
	const char* currentPath = buffer.data();
	bool hasTexture = false;
	uint32_t previewIndex = textureManager->GetDefaultWhiteTexture();
	if (currentPath[0] != '\0') {
		uint32_t loaded = 0;
		if (textureManager->TryLoadTexture(currentPath, loaded)) {
			previewIndex = loaded;
			hasTexture = true;
		}
	}

	const ImVec2 previewSize(56.0f, 56.0f);
	D3D12_GPU_DESCRIPTOR_HANDLE previewHandle = textureManager->GetSrvHandle(previewIndex);
	ImGui::Image(static_cast<ImTextureID>(previewHandle.ptr), previewSize);
	// プレビュー枠(未設定は赤みで示す)。
	ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), hasTexture ? IM_COL32(120, 120, 120, 255) : IM_COL32(200, 90, 90, 255));

	// プレビュー画像へProjectからテクスチャをドロップ可能。
	if (ImGui::BeginDragDropTarget()) {
		const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kProjectTextureDragPayloadType);
		if (payload && payload->DataSize > 0) {
			ApplyTextureSelection(state, slot, buffer, std::filesystem::path(static_cast<const char*>(payload->Data)));
			changed = true;
		}
		ImGui::EndDragDropTarget();
	}

	ImGui::SameLine();
	ImGui::BeginGroup();
	ImGui::TextUnformatted(label);
	ImGui::TextDisabled("%s", hasTexture ? currentPath : "(no texture)");
	if (ImGui::Button("Select")) {
		ImGui::OpenPopup(popupId);
	}
	ImGui::SameLine();
	if (ImGui::Button("...")) {
		std::filesystem::path picked;
		if (OpenTextureFileDialog(picked)) {
			ApplyTextureSelection(state, slot, buffer, picked);
			changed = true;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear")) {
		MaterialAsset::SetTexture(state.material, slot, "", "");
		CopyTextToBuffer(buffer, "");
		changed = true;
	}
	ImGui::EndGroup();

	// プロジェクト内テクスチャ一覧(ImSearchで絞り込み)。
	// 一覧はPopupを開いた瞬間に1回だけ再帰スキャンしてキャッシュする。
	// (毎フレーム recursive_directory_iterator するとPopup表示中ずっと重くなるため)
	if (ImGui::BeginPopup(popupId)) {
		// (表示名, フルパス) のキャッシュ。IsWindowAppearing()時のみ再構築する。
		static std::vector<std::pair<std::string, std::filesystem::path>> cachedTextures;
		if (ImGui::IsWindowAppearing()) {
			cachedTextures.clear();
			AssetDatabase& db = AssetDatabase::GetInstance();
			for (const std::filesystem::path& texturePath : EnumerateProjectTextures()) {
				std::string relative = db.MakeProjectRelativePath(texturePath);
				if (relative.empty()) {
					relative = texturePath.generic_string();
				}
				cachedTextures.emplace_back(std::move(relative), texturePath);
			}
		}

		if (ImSearch::BeginSearch()) {
			ImSearch::SearchBar("Search Texture");
			for (const std::pair<std::string, std::filesystem::path>& entry : cachedTextures) {
				const std::filesystem::path& texturePath = entry.second;
				ImSearch::SearchableItem(entry.first.c_str(), [&](const char* name) {
					if (ImGui::Selectable(name)) {
						ApplyTextureSelection(state, slot, buffer, texturePath);
						changed = true;
						ImGui::CloseCurrentPopup();
					}
				});
			}
			ImSearch::EndSearch();
		}
		ImGui::EndPopup();
	}

	ImGui::PopID();
	return changed;
}

void RefreshMaterialUsers(const std::filesystem::path& materialPath) {
	Scene* scene = EditorApplication::GetInstance()->GetCurrentScene();
	if (!scene) {
		return;
	}

	const std::string materialPathText = materialPath.generic_string();
	for (const std::unique_ptr<GameObject>& gameObject : scene->GetGameObjects()) {
		if (!gameObject) {
			continue;
		}

		for (const std::unique_ptr<Component>& component : gameObject->GetComponents()) {
			if (!component) {
				continue;
			}
			IMaterialTarget* materialTarget = dynamic_cast<IMaterialTarget*>(component.get());
			if (materialTarget && materialTarget->UsesMaterialAsset(materialPathText)) {
				materialTarget->ApplyMaterialAsset(materialPathText);
			}
		}
	}
}

} // namespace

void DrawMaterialAssetInspector(ProjectWindow& projectWindow) {
	EditorSelection* selection = EditorSelection::GetInstance();
	std::filesystem::path materialPath = selection->GetSelectedAssetPath();
	MaterialInspectorState& state = GetMaterialInspectorState();

	LoadMaterialInspectorState(materialPath, state);

	ImGui::TextUnformatted("Material");
	ImGui::Separator();

	if (!state.errorMessage.empty()) {
		ImGui::TextDisabled("%s", state.errorMessage.c_str());
	}

	ImGui::InputText("Name", state.nameBuffer.data(), state.nameBuffer.size());
	ImGui::SameLine();
	if (ImGui::Button("Apply")) {
		std::filesystem::path renamedPath;
		std::string message;
		if (MaterialAsset::Rename(state.materialPath, state.nameBuffer.data(), renamedPath, message)) {
			selection->SetSelectedAsset(renamedPath, AssetType::Material);
			RefreshMaterialUsers(renamedPath);
			state.materialPath.clear();
			state.loaded = false;
			projectWindow.Refresh();
		} else {
			state.errorMessage = message;
		}
	}

	bool changed = false;
	if (ImGui::ColorEdit4("Base Color", &state.material.baseColor.x)) {
		changed = true;
	}

	// エミッション(自己発光)。チェックを付けたマテリアルだけ発光する(Unityと同じopt-in方式)。
	// 強度>1でHDR輝度になり、ブルーム(閾値以上)で滲む。
	if (ImGui::Checkbox("Emission", &state.material.emissiveEnabled)) {
		changed = true;
	}
	if (state.material.emissiveEnabled) {
		ImGui::Indent();
		if (ImGui::ColorEdit3("Emissive Color", &state.material.emissiveColor.x)) {
			changed = true;
		}
		if (ImGui::DragFloat("Emissive Intensity", &state.material.emissiveIntensity, 0.05f, 0.0f, 100.0f)) {
			changed = true;
		}
		// 露出光(ブルーム)のマテリアル別設定。発光の滲み方だけを制御する。
		if (ImGui::DragFloat("Bloom Intensity", &state.material.bloomIntensity, 0.05f, 0.0f, 10.0f)) {
			changed = true;
		}
		if (ImGui::DragFloat("Bloom Threshold", &state.material.bloomThreshold, 0.01f, 0.0f, 10.0f)) {
			changed = true;
		}
		if (ImGui::SliderFloat("Bloom Soft Knee", &state.material.bloomSoftKnee, 0.0f, 1.0f)) {
			changed = true;
		}
		ImGui::Unindent();
	}

	// シェーダー方式の選択(ShaderModel enumの順序に一致させる)。
	const char* shaderItems[] = {"None (Unlit)", "Lambert", "Half Lambert", "Phong", "Blinn-Phong", "Shockwave (Ring)", "Barrier (Hex/Pentagon)", "Trail (Ribbon)"};
	int shaderIndex = state.material.shaderModel;
	if (shaderIndex < 0 || shaderIndex >= static_cast<int>(IM_ARRAYSIZE(shaderItems))) {
		shaderIndex = 0;
	}
	if (ImGui::Combo("Shader Model", &shaderIndex, shaderItems, IM_ARRAYSIZE(shaderItems))) {
		state.material.shaderModel = shaderIndex;
		changed = true;
	}

	// 合成方法。加算は光・炎・魔法のように「重ねるほど明るくなる」表現に使う。
	// KujataEngine::BlendMode と並び順を一致させること。
	const char* blendItems[] = {"None (Opaque)", "Normal (Alpha)", "Add", "Subtract", "Multiply", "Screen", "Exclusion", "Premultiplied Alpha"};
	int blendIndex = state.material.blendMode;
	if (blendIndex < 0 || blendIndex >= static_cast<int>(IM_ARRAYSIZE(blendItems))) {
		blendIndex = 1;
	}
	if (ImGui::Combo("Blend Mode", &blendIndex, blendItems, IM_ARRAYSIZE(blendItems))) {
		state.material.blendMode = blendIndex;
		changed = true;
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("重ね方。Addは光や炎など「重なるほど明るくなる」もの向け。\n加算や半透明にしたら Depth Write を切ること。");
	}

	if (ImGui::Checkbox("Depth Write", &state.material.depthWrite)) {
		changed = true;
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("深度バッファへ書き込むか。半透明・加算はOFFにする。\nONのままだと後から描かれる背後の物を隠し、自分の裏面とも喧嘩する。\n深度テスト(不透明物に隠れること)は常に行われる。");
	}

	// UVトランスフォーム(タイリング・スクロール・回転)。サンプラーはWRAPなので範囲外はリピートする。
	ImGui::SeparatorText("UV Transform");
	if (ImGui::DragFloat2("UV Offset", &state.material.uvOffset.x, 0.01f)) {
		changed = true;
	}
	if (ImGui::DragFloat2("UV Scale", &state.material.uvScale.x, 0.01f)) {
		changed = true;
	}
	if (ImGui::DragFloat("UV Rotation", &state.material.uvRotation, 0.01f)) {
		changed = true;
	}

	if (DrawTextureSlotEditor("BaseColor Texture", "BaseColorTexturePicker", state, MaterialTextureSlot::BaseColor, state.baseColorTextureBuffer)) {
		changed = true;
	}

	if (DrawTextureSlotEditor("Normal Texture", "NormalTexturePicker", state, MaterialTextureSlot::Normal, state.normalTextureBuffer)) {
		changed = true;
	}

	if (DrawTextureSlotEditor("Environment Texture", "EnvironmentTexturePicker", state, MaterialTextureSlot::Environment, state.environmentTextureBuffer)) {
		changed = true;
	}

	if (DrawTextureSlotEditor("Emissive Texture", "EmissiveTexturePicker", state, MaterialTextureSlot::Emissive, state.emissiveTextureBuffer)) {
		changed = true;
	}
	ImGui::TextDisabled("(Emissiveは上のEmissionをONにしないと効きません)");

	if (changed) {
		if (SaveMaterialInspectorState(state)) {
			RefreshMaterialUsers(state.materialPath);
		}
	}

	if (ImGui::Button("Save")) {
		ApplyMaterialInspectorBuffers(state);
		if (SaveMaterialInspectorState(state)) {
			RefreshMaterialUsers(state.materialPath);
		}
	}

	AssetDatabase& assetDatabase = AssetDatabase::GetInstance();
	std::string assetId = assetDatabase.GetOrCreateAssetId(state.materialPath);
	ImGui::Separator();
	ImGui::TextWrapped("Asset ID: %s", assetId.c_str());
	ImGui::TextWrapped("Path: %s", assetDatabase.MakeProjectRelativePath(state.materialPath).c_str());
	if (ImGui::Button("Copy Asset ID")) {
		ImGui::SetClipboardText(assetId.c_str());
	}
}

} // namespace KujataEngine
