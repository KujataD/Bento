#include "SpriteRendererComponent.h"

#include "../3d/Camera.h"
#include "../base/TextureManager.h"
#include "../runtime/AssetResolver.h"
#include "../runtime/InspectorUI.h"
#include "../scene/GameObject.h"
#include <algorithm>
#include <cstring>
#include <filesystem>

namespace KujataEngine {

namespace {

const char* kBlendModeItems[] = {"None", "Normal", "Add", "Subtract", "Multiply", "Screen", "Exclusion", "PremultipliedAlpha"};
constexpr int kBlendModeItemCount = static_cast<int>(sizeof(kBlendModeItems) / sizeof(kBlendModeItems[0]));

std::string ReadString(const nlohmann::json& json, const char* key, const std::string& defaultValue) {
	if (!json.contains(key) || !json.at(key).is_string()) {
		return defaultValue;
	}
	return json.at(key).get<std::string>();
}

Vector2 ReadVector2(const nlohmann::json& json, const char* key, const Vector2& defaultValue) {
	if (!json.contains(key)) {
		return defaultValue;
	}
	const nlohmann::json& value = json.at(key);
	if (!value.is_array() || value.size() < 2) {
		return defaultValue;
	}
	for (int i = 0; i < 2; ++i) {
		if (!value[i].is_number()) {
			return defaultValue;
		}
	}
	return {value[0].get<float>(), value[1].get<float>()};
}

Vector4 ReadVector4(const nlohmann::json& json, const char* key, const Vector4& defaultValue) {
	if (!json.contains(key)) {
		return defaultValue;
	}
	const nlohmann::json& value = json.at(key);
	if (!value.is_array() || value.size() < 4) {
		return defaultValue;
	}
	for (int i = 0; i < 4; ++i) {
		if (!value[i].is_number()) {
			return defaultValue;
		}
	}
	return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()};
}

} // namespace

void SpriteRendererComponent::SyncPathBuffer() {
	std::memset(pathBuffer_.data(), 0, pathBuffer_.size());
	strncpy_s(pathBuffer_.data(), pathBuffer_.size(), texturePath_.c_str(), _TRUNCATE);
}

void SpriteRendererComponent::SetSprite(const std::string& assetId, const std::string& path) {
	textureAssetId_ = assetId;
	texturePath_ = path;

	// パスだけの参照はassetIdを補完し、リネーム/移動に追従できる参照へ揃える。
	// Editor外(FallbackAssetResolver)ではIDが取れないため、その場合はパスをそのまま保持する。
	if (textureAssetId_.empty() && !texturePath_.empty()) {
		IAssetResolver& assetDatabase = GetAssetResolver();
		std::filesystem::path resolvedPath = assetDatabase.ResolveAssetPath("", texturePath_);
		textureAssetId_ = assetDatabase.GetOrCreateAssetId(resolvedPath);
		if (!textureAssetId_.empty()) {
			texturePath_ = assetDatabase.MakeProjectRelativePath(resolvedPath);
		}
	}

	SyncPathBuffer();
	textureResolved_ = false;
	// 読み込みは描画パス外のここで済ませる(Drawでは読み込まない)。
	EnsureTextureLoaded();
}

void SpriteRendererComponent::SetColor(const Vector4& color) {
	color_ = color;
	if (quad_.IsInitialized()) {
		quad_.SetColor(color_);
	}
}

void SpriteRendererComponent::SetPixelsPerUnit(float pixelsPerUnit) {
	// 0以下だとサイズが発散するので下限を設ける。
	pixelsPerUnit_ = (std::max)(pixelsPerUnit, 0.0001f);
	UpdateSizeFromTexture();
}

void SpriteRendererComponent::UpdateSizeFromTexture() {
	// Unity 2Dと同じく「テクスチャの解像度 / PPU」でworld単位の大きさを決める。
	uint32_t width = 0;
	uint32_t height = 0;
	if (TextureManager::GetInstance()->TryGetTextureSize(textureIndex_, width, height)) {
		size_ = {static_cast<float>(width) / pixelsPerUnit_, static_cast<float>(height) / pixelsPerUnit_};
	} else {
		size_ = {1.0f, 1.0f};
	}
	if (quad_.IsInitialized()) {
		quad_.SetSize(size_, pivot_);
	}
}

void SpriteRendererComponent::SetPivot(const Vector2& pivot) {
	pivot_ = pivot;
	if (quad_.IsInitialized()) {
		quad_.SetSize(size_, pivot_);
	}
}

void SpriteRendererComponent::SetFlip(bool flipX, bool flipY) {
	flipX_ = flipX;
	flipY_ = flipY;
	if (quad_.IsInitialized()) {
		quad_.SetFlip(flipX_, flipY_);
	}
}

void SpriteRendererComponent::SetBlendMode(BlendMode blendMode) {
	blendModeIndex_ = static_cast<int>(blendMode);
	if (quad_.IsInitialized()) {
		quad_.SetBlendMode(blendMode);
	}
}

void SpriteRendererComponent::EnsureTextureLoaded() {
	if (textureAssetId_.empty() && texturePath_.empty()) {
		textureIndex_ = TextureManager::GetInstance()->GetDefaultWhiteTexture();
		textureResolved_ = true;
		UpdateSizeFromTexture();
		return;
	}

	// assetId優先で現在のパスへ解決する(移動済みアセットは.meta経由で新パスが返る)。
	std::string resolvedPath = GetAssetResolver().ResolveAssetPath(textureAssetId_, texturePath_).string();
	if (textureResolved_ && loadedPath_ == resolvedPath) {
		return;
	}

	TextureManager* textureManager = TextureManager::GetInstance();
	uint32_t index = textureManager->GetDefaultWhiteTexture();
	if (!resolvedPath.empty()) {
		uint32_t loaded = 0;
		if (textureManager->TryLoadTexture(resolvedPath, loaded)) {
			index = loaded;
		}
	}
	textureIndex_ = index;
	loadedPath_ = resolvedPath;
	textureResolved_ = true;
	// 解像度が変わるので大きさを取り直す。
	UpdateSizeFromTexture();
}

void SpriteRendererComponent::EnsureQuadInitialized() {
	if (quad_.IsInitialized()) {
		return;
	}
	quad_.Initialize();
	ApplyToQuad();
}

void SpriteRendererComponent::ApplyToQuad() {
	quad_.SetSize(size_, pivot_);
	quad_.SetFlip(flipX_, flipY_);
	quad_.SetColor(color_);
	quad_.SetUVTransform(uvOffset_, uvScale_, uvRotation_);
	quad_.SetBlendMode(static_cast<BlendMode>(blendModeIndex_));
}

void SpriteRendererComponent::DrawSprite() {
	GameObject* owner = GetOwner();
	if (!owner || !camera_) {
		return;
	}

	EnsureQuadInitialized();

	// 未解決なら白テクスチャで描く(描画パス中に新規読み込みはしない)。
	if (!textureResolved_) {
		textureIndex_ = TextureManager::GetInstance()->GetDefaultWhiteTexture();
	}
	quad_.SetTexture(textureIndex_);

	// GameObjectの通常のTransformで配置する(RectTransform/Canvasは使わない)。
	owner->GetTransform().UpdateMatrix(*camera_);
	quad_.Draw(owner->GetTransform(), *camera_);
}

void SpriteRendererComponent::DrawInspector() {
#ifdef USE_IMGUI
	if (InspectorUI::InputText("Sprite (Texture)", pathBuffer_.data(), pathBuffer_.size())) {
		// 存在するパスならassetIdを補完する(入力途中の不完全なパスはID無しのまま)。
		SetSprite("", pathBuffer_.data());
	}

	if (InspectorUI::ColorEdit4("Color", &color_.x)) {
		SetColor(color_);
	}

	// 大きさは「テクスチャ解像度 / PPU」で決まる(Unity 2Dと同じ)。直接のSize入力は持たない。
	if (InspectorUI::DragFloat("Pixels Per Unit", &pixelsPerUnit_, 1.0f, 0.0001f, 4096.0f)) {
		SetPixelsPerUnit(pixelsPerUnit_);
	}
	InspectorUI::TextDisabled(("Size (world): " + std::to_string(size_.x) + " x " + std::to_string(size_.y)).c_str());

	if (InspectorUI::DragFloat2("Pivot", &pivot_.x, 0.01f, 0.0f, 1.0f)) {
		SetPivot(pivot_);
	}

	// スプライトは深度を書かないので、前後はこの値の描画順だけで決まる。
	InspectorUI::DragInt("Sorting Order", &sortingOrder_, 1.0f, -32768, 32767);

	bool flipX = flipX_;
	bool flipY = flipY_;
	bool flipChanged = InspectorUI::Checkbox("Flip X", &flipX);
	flipChanged |= InspectorUI::Checkbox("Flip Y", &flipY);
	if (flipChanged) {
		SetFlip(flipX, flipY);
	}

	int blendIndex = blendModeIndex_;
	if (blendIndex < 0 || blendIndex >= kBlendModeItemCount) {
		blendIndex = static_cast<int>(BlendMode::kNormal);
	}
	if (InspectorUI::Combo("Blend Mode", &blendIndex, kBlendModeItems, kBlendModeItemCount)) {
		SetBlendMode(static_cast<BlendMode>(blendIndex));
	}

	InspectorUI::TextUnformatted("--- UV Transform ---");
	bool uvChanged = InspectorUI::DragFloat2("UV Offset", &uvOffset_.x, 0.01f);
	uvChanged |= InspectorUI::DragFloat2("UV Scale", &uvScale_.x, 0.01f);
	uvChanged |= InspectorUI::DragFloat("UV Rotation", &uvRotation_, 0.01f);
	if (uvChanged && quad_.IsInitialized()) {
		quad_.SetUVTransform(uvOffset_, uvScale_, uvRotation_);
	}
#endif // USE_IMGUI
}

void SpriteRendererComponent::WriteJson(nlohmann::json& json) const {
	json["textureAssetId"] = textureAssetId_;
	json["texturePath"] = texturePath_;
	json["color"] = {color_.x, color_.y, color_.z, color_.w};
	json["pixelsPerUnit"] = pixelsPerUnit_;
	json["sortingOrder"] = sortingOrder_;
	json["pivot"] = {pivot_.x, pivot_.y};
	json["flipX"] = flipX_;
	json["flipY"] = flipY_;
	json["uvOffset"] = {uvOffset_.x, uvOffset_.y};
	json["uvScale"] = {uvScale_.x, uvScale_.y};
	json["uvRotation"] = uvRotation_;
	json["blendMode"] = blendModeIndex_;
}

void SpriteRendererComponent::ReadJson(const nlohmann::json& json) {
	textureAssetId_ = ReadString(json, "textureAssetId", textureAssetId_);
	texturePath_ = ReadString(json, "texturePath", texturePath_);
	color_ = ReadVector4(json, "color", color_);
	if (json.contains("pixelsPerUnit") && json.at("pixelsPerUnit").is_number()) {
		pixelsPerUnit_ = (std::max)(json.at("pixelsPerUnit").get<float>(), 0.0001f);
	}
	if (json.contains("sortingOrder") && json.at("sortingOrder").is_number_integer()) {
		sortingOrder_ = json.at("sortingOrder").get<int>();
	}
	pivot_ = ReadVector2(json, "pivot", pivot_);
	if (json.contains("flipX") && json.at("flipX").is_boolean()) {
		flipX_ = json.at("flipX").get<bool>();
	}
	if (json.contains("flipY") && json.at("flipY").is_boolean()) {
		flipY_ = json.at("flipY").get<bool>();
	}
	uvOffset_ = ReadVector2(json, "uvOffset", uvOffset_);
	uvScale_ = ReadVector2(json, "uvScale", uvScale_);
	if (json.contains("uvRotation") && json.at("uvRotation").is_number()) {
		uvRotation_ = json.at("uvRotation").get<float>();
	}
	if (json.contains("blendMode") && json.at("blendMode").is_number_integer()) {
		blendModeIndex_ = std::clamp(json.at("blendMode").get<int>(), 0, kBlendModeItemCount - 1);
	}
	textureResolved_ = false;
}

void SpriteRendererComponent::OnAfterReadJson() {
	// ID未付与の旧データはここで補完される(次回保存時に永続化)。SetSprite内で読み込みも行う。
	SetSprite(textureAssetId_, texturePath_);
	if (quad_.IsInitialized()) {
		ApplyToQuad();
	}
}

} // namespace KujataEngine
