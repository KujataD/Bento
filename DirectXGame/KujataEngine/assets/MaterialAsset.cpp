#include "MaterialAsset.h"
#include "../runtime/AssetResolver.h"
#include "../base/ProjectPath.h"
#include "../base/TextureManager.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <system_error>

namespace KujataEngine {

namespace {

std::string ToLower(std::string text) {
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) {
		return static_cast<char>(std::tolower(character));
	});
	return text;
}

std::string ReadString(const nlohmann::json& value, const char* key, const std::string& defaultValue) {
	if (!value.contains(key)) {
		return defaultValue;
	}
	if (!value.at(key).is_string()) {
		return defaultValue;
	}
	return value.at(key).get<std::string>();
}

std::filesystem::path ResolveProjectPath(const std::filesystem::path& path);

Vector2 ReadVector2(const nlohmann::json& value, const char* key, const Vector2& defaultValue) {
	if (!value.contains(key)) {
		return defaultValue;
	}
	if (!value.at(key).is_array()) {
		return defaultValue;
	}
	if (value.at(key).size() < 2) {
		return defaultValue;
	}

	Vector2 result = defaultValue;
	for (size_t index = 0; index < 2; ++index) {
		if (value.at(key).at(index).is_number()) {
			(&result.x)[index] = value.at(key).at(index).get<float>();
		}
	}
	return result;
}

Vector3 ReadVector3(const nlohmann::json& value, const char* key, const Vector3& defaultValue) {
	if (!value.contains(key)) {
		return defaultValue;
	}
	if (!value.at(key).is_array()) {
		return defaultValue;
	}
	if (value.at(key).size() < 3) {
		return defaultValue;
	}

	Vector3 result = defaultValue;
	for (size_t index = 0; index < 3; ++index) {
		if (value.at(key).at(index).is_number()) {
			(&result.x)[index] = value.at(key).at(index).get<float>();
		}
	}
	return result;
}

Vector4 ReadVector4(const nlohmann::json& value, const char* key, const Vector4& defaultValue) {
	if (!value.contains(key)) {
		return defaultValue;
	}
	if (!value.at(key).is_array()) {
		return defaultValue;
	}
	if (value.at(key).size() < 4) {
		return defaultValue;
	}

	Vector4 result = defaultValue;
	for (size_t index = 0; index < 4; ++index) {
		if (value.at(key).at(index).is_number()) {
			(&result.x)[index] = value.at(key).at(index).get<float>();
		}
	}
	return result;
}

std::array<MaterialTextureSlot, 4> GetKnownTextureSlots() {
	return {
	    MaterialTextureSlot::BaseColor,
	    MaterialTextureSlot::Normal,
	    MaterialTextureSlot::Environment,
	    MaterialTextureSlot::Emissive,
	};
}

MaterialTexture* FindTexture(MaterialAssetData& material, MaterialTextureSlot slot) {
	for (MaterialTexture& texture : material.textures) {
		if (texture.slot == slot) {
			return &texture;
		}
	}
	return nullptr;
}

bool ReadTextureSlotObject(const nlohmann::json& textureJson, MaterialTextureSlot slot, MaterialAssetData& material) {
	if (!textureJson.is_object() && !textureJson.is_string()) {
		return false;
	}

	std::string assetId;
	std::string path;
	if (textureJson.is_string()) {
		path = textureJson.get<std::string>();
	} else {
		assetId = ReadString(textureJson, "assetId", assetId);
		path = ReadString(textureJson, "path", path);
	}

	MaterialAsset::SetTexture(material, slot, assetId, path);
	return true;
}

bool ReadTexturesObject(const nlohmann::json& json, MaterialAssetData& material) {
	if (!json.contains("textures")) {
		return false;
	}

	const nlohmann::json& texturesJson = json.at("textures");
	bool readAnyTexture = false;

	if (texturesJson.is_object()) {
		for (auto iterator = texturesJson.begin(); iterator != texturesJson.end(); ++iterator) {
			MaterialTextureSlot slot = MaterialTextureSlot::BaseColor;
			if (!MaterialAsset::TryGetTextureSlotFromString(iterator.key(), slot)) {
				continue;
			}
			if (ReadTextureSlotObject(iterator.value(), slot, material)) {
				readAnyTexture = true;
			}
		}
		return readAnyTexture;
	}

	if (texturesJson.is_array()) {
		for (const nlohmann::json& textureJson : texturesJson) {
			if (!textureJson.is_object()) {
				continue;
			}

			MaterialTextureSlot slot = MaterialTextureSlot::BaseColor;
			std::string slotName = ReadString(textureJson, "slot", "");
			if (!MaterialAsset::TryGetTextureSlotFromString(slotName, slot)) {
				continue;
			}
			if (ReadTextureSlotObject(textureJson, slot, material)) {
				readAnyTexture = true;
			}
		}
	}

	return readAnyTexture;
}

std::filesystem::path ResolveMaterialTexturePath(const MaterialTexture* texture, MaterialTextureSlot slot) {
	if (!texture) {
		// Emissiveも白を既定にする。シェーダーは発光へ乗算するので、白=マップ無しと同じ挙動になる。
		if (slot == MaterialTextureSlot::BaseColor || slot == MaterialTextureSlot::Emissive) {
			return ResolveProjectPath("resources/white1x1.png");
		}
		return {};
	}

	IAssetResolver& assetDatabase = GetAssetResolver();
	std::filesystem::path fallbackPath;
	if (!texture->path.empty()) {
		fallbackPath = texture->path;
	}

	std::filesystem::path resolvedPath = assetDatabase.ResolveAssetPath(texture->assetId, fallbackPath);
	if (!resolvedPath.empty()) {
		return resolvedPath;
	}

	if (slot == MaterialTextureSlot::BaseColor) {
		return ResolveProjectPath("resources/white1x1.png");
	}

	return {};
}

std::filesystem::path ResolveProjectPath(const std::filesystem::path& path) {
	if (path.empty()) {
		return {};
	}
	if (path.is_absolute()) {
		return NormalizeEditorPath(path);
	}
	// アセットの相対パス("Materials/Foo.material.json"等)はプロジェクトルートではなくData配下が基準。
	// GetActiveProjectRoot()だとDataが抜けて、テクスチャ未設定時の既定white1x1が見つからずassertする。
	return NormalizeEditorPath(GetProjectDataRoot() / path);
}

std::filesystem::path GetMetaPath(const std::filesystem::path& assetPath) {
	std::filesystem::path metaPath = assetPath;
	metaPath += ".meta";
	return metaPath;
}

} // namespace

MaterialAssetData MaterialAsset::CreateDefault() {
	MaterialAssetData material{};
	SetTexture(material, MaterialTextureSlot::BaseColor, "", "resources/white1x1.png");
	return material;
}

bool MaterialAsset::IsMaterialFile(const std::filesystem::path& path) {
	std::string fileName = ToLower(path.filename().string());
	return fileName.ends_with(".material.json");
}

const char* MaterialAsset::ToString(MaterialTextureSlot slot) {
	switch (slot) {
	case MaterialTextureSlot::BaseColor:
		return "BaseColor";
	case MaterialTextureSlot::Normal:
		return "Normal";
	case MaterialTextureSlot::Environment:
		return "Environment";
	case MaterialTextureSlot::Emissive:
		return "Emissive";
	default:
		return "BaseColor";
	}
}

bool MaterialAsset::TryGetTextureSlotFromString(const std::string& slotName, MaterialTextureSlot& outSlot) {
	std::string lowerName = ToLower(slotName);
	if (lowerName == "basecolor" || lowerName == "base_color" || lowerName == "albedo" || lowerName == "diffuse") {
		outSlot = MaterialTextureSlot::BaseColor;
		return true;
	}
	if (lowerName == "normal" || lowerName == "normalmap" || lowerName == "normal_map") {
		outSlot = MaterialTextureSlot::Normal;
		return true;
	}
	if (lowerName == "environment" || lowerName == "environmentmap" || lowerName == "environment_map" || lowerName == "env") {
		outSlot = MaterialTextureSlot::Environment;
		return true;
	}
	if (lowerName == "emissive" || lowerName == "emissivemap" || lowerName == "emissive_map" || lowerName == "emission") {
		outSlot = MaterialTextureSlot::Emissive;
		return true;
	}

	return false;
}

const MaterialTexture* MaterialAsset::GetTexture(const MaterialAssetData& material, MaterialTextureSlot slot) {
	for (const MaterialTexture& texture : material.textures) {
		if (texture.slot == slot) {
			return &texture;
		}
	}
	return nullptr;
}

std::string MaterialAsset::GetTexturePath(const MaterialAssetData& material, MaterialTextureSlot slot) {
	const MaterialTexture* texture = GetTexture(material, slot);
	if (!texture) {
		return "";
	}
	return texture->path;
}

void MaterialAsset::SetTexture(MaterialAssetData& material, MaterialTextureSlot slot, const std::string& assetId, const std::string& path) {
	MaterialTexture* texture = FindTexture(material, slot);
	if (!texture) {
		MaterialTexture newTexture{};
		newTexture.slot = slot;
		material.textures.push_back(newTexture);
		texture = &material.textures.back();
	}

	texture->assetId = assetId;
	texture->path = path;

	// パスだけの参照はここでassetIdを補完し、リネーム/移動に追従できる参照へ揃える。
	// Editor外(FallbackAssetResolver)ではIDが取れないため、その場合はパスをそのまま保持する。
	if (texture->assetId.empty() && !texture->path.empty()) {
		IAssetResolver& assetDatabase = GetAssetResolver();
		std::filesystem::path resolvedPath = assetDatabase.ResolveAssetPath("", texture->path);
		texture->assetId = assetDatabase.GetOrCreateAssetId(resolvedPath);
		if (!texture->assetId.empty()) {
			texture->path = assetDatabase.MakeProjectRelativePath(resolvedPath);
		}
	}
}

std::string MaterialAsset::GetDisplayName(const std::filesystem::path& path) {
	std::string fileName = path.filename().string();
	const std::string suffix = ".material.json";
	if (fileName.ends_with(suffix)) {
		fileName.erase(fileName.size() - suffix.size());
	}
	if (fileName.empty()) {
		return "New Material";
	}
	return fileName;
}

std::string MaterialAsset::SanitizeName(const std::string& name) {
	std::string sanitized;
	sanitized.reserve(name.size());

	for (char character : name) {
		bool invalid = character == '<' || character == '>' || character == ':' || character == '"' || character == '/' ||
		               character == '\\' || character == '|' || character == '?' || character == '*';
		if (invalid) {
			sanitized += '_';
		} else {
			sanitized += character;
		}
	}

	while (!sanitized.empty() && sanitized.front() == ' ') {
		sanitized.erase(sanitized.begin());
	}
	while (!sanitized.empty() && sanitized.back() == ' ') {
		sanitized.pop_back();
	}

	if (sanitized.empty()) {
		return "New Material";
	}
	return sanitized;
}

bool MaterialAsset::Rename(const std::filesystem::path& path, const std::string& newName, std::filesystem::path& outNewPath, std::string& message) {
	std::filesystem::path oldPath = ResolveProjectPath(path);
	if (!std::filesystem::exists(oldPath)) {
		message = "Material file was not found.";
		return false;
	}
	if (!IsMaterialFile(oldPath)) {
		message = "Selected file is not Material.";
		return false;
	}

	MaterialAssetData material{};
	if (!Load(oldPath, material, message)) {
		return false;
	}

	std::string sanitizedName = SanitizeName(newName);
	std::filesystem::path newPath = oldPath.parent_path() / (sanitizedName + ".material.json");
	newPath = ResolveProjectPath(newPath);

	std::error_code error;
	if (newPath != oldPath && std::filesystem::exists(newPath, error)) {
		message = "Same name Material already exists.";
		return false;
	}

	std::filesystem::path oldMetaPath = GetMetaPath(oldPath);
	std::filesystem::path newMetaPath = GetMetaPath(newPath);
	if (newMetaPath != oldMetaPath && std::filesystem::exists(newMetaPath, error)) {
		message = "Same name meta already exists.";
		return false;
	}

	if (newPath != oldPath) {
		std::filesystem::rename(oldPath, newPath, error);
		if (error) {
			message = "Failed to rename Material: " + error.message();
			return false;
		}

		if (std::filesystem::exists(oldMetaPath, error)) {
			std::filesystem::rename(oldMetaPath, newMetaPath, error);
			if (error) {
				message = "Failed to rename Material meta: " + error.message();
				return false;
			}
		}
	}

	material.name = sanitizedName;
	if (!Save(newPath, material, message)) {
		return false;
	}

	outNewPath = newPath;
	message = "Renamed Material.";
	return true;
}

bool MaterialAsset::Load(const std::filesystem::path& path, MaterialAssetData& outMaterial, std::string& message) {
	std::filesystem::path resolvedPath = ResolveProjectPath(path);
	std::ifstream file(resolvedPath, std::ios::binary);
	if (!file) {
		message = "Failed to open Material: " + resolvedPath.string();
		return false;
	}

	nlohmann::json materialJson;
	try {
		file >> materialJson;
	} catch (const nlohmann::json::exception& exception) {
		message = "Failed to parse Material: " + resolvedPath.string() + " : " + exception.what();
		return false;
	}

	outMaterial = ReadJsonObject(materialJson, CreateDefault());
	if (outMaterial.name.empty()) {
		outMaterial.name = GetDisplayName(resolvedPath);
	}
	message = "Loaded Material.";
	return true;
}

bool MaterialAsset::Save(const std::filesystem::path& path, const MaterialAssetData& material, std::string& message) {
	std::filesystem::path resolvedPath = ResolveProjectPath(path);

	std::error_code error;
	std::filesystem::create_directories(resolvedPath.parent_path(), error);
	if (error) {
		message = "Failed to create Material directory: " + error.message();
		return false;
	}

	nlohmann::json materialJson;
	materialJson["assetType"] = "Material";
	materialJson["version"] = 1;
	WriteJsonObject(materialJson, material);

	std::ofstream file(resolvedPath, std::ios::binary);
	if (!file) {
		message = "Failed to open Material: " + resolvedPath.string();
		return false;
	}

	file << materialJson.dump(2);
	file << "\n";
	if (!file) {
		message = "Failed to write Material: " + resolvedPath.string();
		return false;
	}

	GetAssetResolver().GetOrCreateAssetId(resolvedPath);
	message = "Saved Material.";
	return true;
}

bool MaterialAsset::CreateDefaultFile(const std::filesystem::path& path, std::string& message) {
	MaterialAssetData material = CreateDefault();
	material.name = GetDisplayName(path);
	return Save(path, material, message);
}

MaterialAssetData MaterialAsset::ReadJsonObject(const nlohmann::json& json, const MaterialAssetData& defaultMaterial) {
	MaterialAssetData material = defaultMaterial;
	if (!json.is_object()) {
		return material;
	}

	material.name = ReadString(json, "name", material.name);
	material.baseColor = ReadVector4(json, "baseColor", material.baseColor);
	// 旧Material JSONではcolorという名前だったため、互換用に読み取る。
	material.baseColor = ReadVector4(json, "color", material.baseColor);

	if (json.contains("shaderModel") && json.at("shaderModel").is_number_integer()) {
		material.shaderModel = json.at("shaderModel").get<int>();
	}

	// 合成方法と深度書き込み(キーが無い旧Material JSONは 通常αブレンド+深度書き込みあり のまま)。
	if (json.contains("blendMode") && json.at("blendMode").is_number_integer()) {
		material.blendMode = json.at("blendMode").get<int>();
	}
	if (json.contains("depthWrite") && json.at("depthWrite").is_boolean()) {
		material.depthWrite = json.at("depthWrite").get<bool>();
	}

	// エミッション(キーが無い旧Material JSONはデフォルト=OFFのまま)。
	if (json.contains("emissiveEnabled") && json.at("emissiveEnabled").is_boolean()) {
		material.emissiveEnabled = json.at("emissiveEnabled").get<bool>();
	}
	material.emissiveColor = ReadVector3(json, "emissiveColor", material.emissiveColor);
	if (json.contains("emissiveIntensity") && json.at("emissiveIntensity").is_number()) {
		material.emissiveIntensity = json.at("emissiveIntensity").get<float>();
	}

	// 露出光(ブルーム)のマテリアル別設定(キーが無い場合はデフォルト値)。
	if (json.contains("bloomIntensity") && json.at("bloomIntensity").is_number()) {
		material.bloomIntensity = json.at("bloomIntensity").get<float>();
	}
	if (json.contains("bloomThreshold") && json.at("bloomThreshold").is_number()) {
		material.bloomThreshold = json.at("bloomThreshold").get<float>();
	}
	if (json.contains("bloomSoftKnee") && json.at("bloomSoftKnee").is_number()) {
		material.bloomSoftKnee = json.at("bloomSoftKnee").get<float>();
	}

	// UVトランスフォーム(キーが無い旧Material JSONは変換なしのまま)。
	material.uvOffset = ReadVector2(json, "uvOffset", material.uvOffset);
	material.uvScale = ReadVector2(json, "uvScale", material.uvScale);
	if (json.contains("uvRotation") && json.at("uvRotation").is_number()) {
		material.uvRotation = json.at("uvRotation").get<float>();
	}

	bool readNewTextures = ReadTexturesObject(json, material);
	if (!readNewTextures) {
		std::string legacyAssetId = ReadString(json, "textureAssetId", "");
		std::string legacyTexturePath = ReadString(json, "texturePath", "");
		// 初期実装や旧Scene JSONではtextureという名前で保存していたため、互換用に読み取る。
		legacyTexturePath = ReadString(json, "texture", legacyTexturePath);
		if (!legacyAssetId.empty() || !legacyTexturePath.empty()) {
			SetTexture(material, MaterialTextureSlot::BaseColor, legacyAssetId, legacyTexturePath);
		}
	}

	return material;
}

void MaterialAsset::WriteJsonObject(nlohmann::json& json, const MaterialAssetData& material) {
	json["name"] = material.name;
	json["baseColor"] = {material.baseColor.x, material.baseColor.y, material.baseColor.z, material.baseColor.w};
	json["shaderModel"] = material.shaderModel;
	json["blendMode"] = material.blendMode;
	json["depthWrite"] = material.depthWrite;
	json["emissiveEnabled"] = material.emissiveEnabled;
	json["emissiveColor"] = {material.emissiveColor.x, material.emissiveColor.y, material.emissiveColor.z};
	json["emissiveIntensity"] = material.emissiveIntensity;
	json["bloomIntensity"] = material.bloomIntensity;
	json["bloomThreshold"] = material.bloomThreshold;
	json["bloomSoftKnee"] = material.bloomSoftKnee;
	json["uvOffset"] = {material.uvOffset.x, material.uvOffset.y};
	json["uvScale"] = {material.uvScale.x, material.uvScale.y};
	json["uvRotation"] = material.uvRotation;

	nlohmann::json texturesJson = nlohmann::json::object();
	for (MaterialTextureSlot slot : GetKnownTextureSlots()) {
		const MaterialTexture* texture = GetTexture(material, slot);
		nlohmann::json textureJson;
		if (texture) {
			textureJson["assetId"] = texture->assetId;
			textureJson["path"] = texture->path;
		} else {
			textureJson["assetId"] = "";
			textureJson["path"] = "";
		}
		texturesJson[ToString(slot)] = textureJson;
	}
	json["textures"] = texturesJson;
}

std::filesystem::path MaterialAsset::ResolveTexturePath(const MaterialAssetData& material, MaterialTextureSlot slot) {
	return ResolveMaterialTexturePath(GetTexture(material, slot), slot);
}

uint32_t MaterialAsset::ResolveTextureIndex(const MaterialAssetData& material, MaterialTextureSlot slot) {
	std::filesystem::path texturePath = ResolveTexturePath(material, slot);
	uint32_t textureIndex = 0;
	if (!texturePath.empty()) {
		if (TextureManager::GetInstance()->TryLoadTexture(texturePath.string(), textureIndex)) {
			return textureIndex;
		}
	}

	if (slot != MaterialTextureSlot::BaseColor && slot != MaterialTextureSlot::Emissive) {
		return 0;
	}

	return TextureManager::GetInstance()->GetDefaultWhiteTexture();
}

} // namespace KujataEngine
