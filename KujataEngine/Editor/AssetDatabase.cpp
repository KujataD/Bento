#include "AssetDatabase.h"
#include "../base/ProjectPath.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 26495)
#pragma warning(disable : 26819)
#endif
#include "../../externals/nlohmann/json.hpp"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <system_error>

namespace KujataEngine {

namespace {

using json = nlohmann::json;

std::string ToLower(std::string text) {
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) {
		return static_cast<char>(std::tolower(character));
	});
	return text;
}

AssetType AssetTypeFromString(const std::string& typeName) {
	if (typeName == "Texture") {
		return AssetType::Texture;
	}
	if (typeName == "Model") {
		return AssetType::Model;
	}
	if (typeName == "Scene") {
		return AssetType::Scene;
	}
	if (typeName == "Prefab") {
		return AssetType::Prefab;
	}
	if (typeName == "Material") {
		return AssetType::Material;
	}
	return AssetType::Unknown;
}

std::string ReadString(const json& value, const char* key, const std::string& defaultValue) {
	if (!value.contains(key)) {
		return defaultValue;
	}
	if (!value.at(key).is_string()) {
		return defaultValue;
	}
	return value.at(key).get<std::string>();
}

} // namespace

AssetDatabase& AssetDatabase::GetInstance() {
	static AssetDatabase instance;
	return instance;
}

void AssetDatabase::Initialize(const std::filesystem::path& projectRoot) {
	projectRoot_ = NormalizePath(projectRoot);
	if (projectRoot_.empty()) {
		projectRoot_ = GetProjectDataRoot();
	}

	initialized_ = true;
	Refresh();
}

void AssetDatabase::Refresh() {
	EnsureInitialized();

	assetsById_.clear();
	assetIdByPath_.clear();

	std::error_code error;
	if (!std::filesystem::exists(projectRoot_, error)) {
		return;
	}

	std::filesystem::recursive_directory_iterator iterator(projectRoot_, error);
	if (error) {
		return;
	}

	for (const std::filesystem::directory_entry& entry : iterator) {
		if (entry.is_directory(error)) {
			continue;
		}

		std::filesystem::path path = NormalizePath(entry.path());
		if (!IsMetaFile(path)) {
			continue;
		}

		AssetInfo info{};
		if (!ReadMeta(path, info)) {
			continue;
		}
		if (!std::filesystem::exists(info.assetPath, error)) {
			continue;
		}
		if (!IsAssetFile(info.assetPath)) {
			continue;
		}

		RegisterAssetInfo(info);
	}
}

std::string AssetDatabase::GetOrCreateAssetId(const std::filesystem::path& assetPath) {
	EnsureInitialized();

	std::filesystem::path normalizedAssetPath = ResolveProjectPath(assetPath);
	if (!IsAssetFile(normalizedAssetPath)) {
		return "";
	}

	std::error_code error;
	if (!std::filesystem::exists(normalizedAssetPath, error)) {
		return "";
	}
	if (std::filesystem::is_directory(normalizedAssetPath, error)) {
		return "";
	}

	std::string pathKey = MakePathKey(normalizedAssetPath);
	auto foundPath = assetIdByPath_.find(pathKey);
	if (foundPath != assetIdByPath_.end()) {
		return foundPath->second;
	}

	std::filesystem::path metaPath = GetMetaPath(normalizedAssetPath);
	AssetInfo info{};
	bool loadedMeta = ReadMeta(metaPath, info);
	if (!loadedMeta) {
		info.assetId = GenerateAssetId();
		info.assetPath = normalizedAssetPath;
		info.metaPath = metaPath;
		info.type = ClassifyAssetType(normalizedAssetPath);
		info.relativePath = MakeProjectRelativePath(normalizedAssetPath);
		WriteMeta(info);
	}

	if (info.assetId.empty()) {
		info.assetId = GenerateAssetId();
	}

	info.assetPath = normalizedAssetPath;
	info.metaPath = metaPath;
	info.type = ClassifyAssetType(normalizedAssetPath);
	info.relativePath = MakeProjectRelativePath(normalizedAssetPath);

	auto duplicated = assetsById_.find(info.assetId);
	if (duplicated != assetsById_.end()) {
		if (MakePathKey(duplicated->second.assetPath) != pathKey) {
			info.assetId = GenerateAssetId();
			WriteMeta(info);
		}
	} else if (!loadedMeta) {
		WriteMeta(info);
	}

	RegisterAssetInfo(info);
	return info.assetId;
}

const AssetInfo* AssetDatabase::FindById(const std::string& assetId) {
	EnsureInitialized();
	if (assetId.empty()) {
		return nullptr;
	}

	auto found = assetsById_.find(assetId);
	if (found == assetsById_.end()) {
		return nullptr;
	}
	return &found->second;
}

const AssetInfo* AssetDatabase::FindByPath(const std::filesystem::path& assetPath) {
	EnsureInitialized();

	std::filesystem::path normalizedAssetPath = ResolveProjectPath(assetPath);
	std::string pathKey = MakePathKey(normalizedAssetPath);
	auto foundPath = assetIdByPath_.find(pathKey);
	if (foundPath == assetIdByPath_.end()) {
		return nullptr;
	}

	return FindById(foundPath->second);
}

std::filesystem::path AssetDatabase::ResolveAssetPath(const std::string& assetId, const std::filesystem::path& fallbackPath) {
	EnsureInitialized();

	const AssetInfo* info = FindById(assetId);
	if (info) {
		return info->assetPath;
	}

	if (fallbackPath.empty()) {
		return {};
	}
	return ResolveProjectPath(fallbackPath);
}

std::string AssetDatabase::MakeProjectRelativePath(const std::filesystem::path& assetPath) const {
	std::error_code error;
	std::filesystem::path normalizedAssetPath = NormalizePath(assetPath);
	std::filesystem::path relative = std::filesystem::relative(normalizedAssetPath, projectRoot_, error);
	if (error || relative.empty()) {
		return normalizedAssetPath.generic_string();
	}
	return relative.generic_string();
}

AssetType AssetDatabase::ClassifyAssetType(const std::filesystem::path& assetPath) const {
	std::string fileName = ToLower(assetPath.filename().string());
	std::string extension = ToLower(assetPath.extension().string());

	if (fileName.ends_with(".material.json")) {
		return AssetType::Material;
	}
	if (fileName.ends_with(".prefab.json")) {
		return AssetType::Prefab;
	}
	if (fileName.ends_with(".scene.json")) {
		return AssetType::Scene;
	}

	if (extension == ".png") {
		return AssetType::Texture;
	}
	if (extension == ".jpg") {
		return AssetType::Texture;
	}
	if (extension == ".jpeg") {
		return AssetType::Texture;
	}

	if (extension == ".obj") {
		return AssetType::Model;
	}
	if (extension == ".gltf") {
		return AssetType::Model;
	}
	if (extension == ".glb") {
		return AssetType::Model;
	}

	return AssetType::Unknown;
}

bool AssetDatabase::IsAssetFile(const std::filesystem::path& assetPath) const {
	if (IsMetaFile(assetPath)) {
		return false;
	}

	return ClassifyAssetType(assetPath) != AssetType::Unknown;
}

bool AssetDatabase::IsMetaFile(const std::filesystem::path& path) const {
	std::string extension = ToLower(path.extension().string());
	return extension == ".meta";
}

const char* AssetDatabase::ToString(AssetType type) {
	if (type == AssetType::Texture) {
		return "Texture";
	}
	if (type == AssetType::Model) {
		return "Model";
	}
	if (type == AssetType::Scene) {
		return "Scene";
	}
	if (type == AssetType::Prefab) {
		return "Prefab";
	}
	if (type == AssetType::Material) {
		return "Material";
	}
	return "Unknown";
}

std::filesystem::path AssetDatabase::NormalizePath(const std::filesystem::path& path) const {
	return NormalizeEditorPath(path);
}

std::filesystem::path AssetDatabase::ResolveProjectPath(const std::filesystem::path& path) const {
	if (path.empty()) {
		return {};
	}
	if (path.is_absolute()) {
		return NormalizePath(path);
	}
	return NormalizePath(projectRoot_ / path);
}

std::string AssetDatabase::MakePathKey(const std::filesystem::path& path) const {
	return NormalizePath(path).generic_string();
}

std::filesystem::path AssetDatabase::GetMetaPath(const std::filesystem::path& assetPath) const {
	std::filesystem::path metaPath = assetPath;
	metaPath += ".meta";
	return metaPath;
}

bool AssetDatabase::ReadMeta(const std::filesystem::path& metaPath, AssetInfo& outInfo) const {
	std::ifstream file(metaPath, std::ios::binary);
	if (!file) {
		return false;
	}

	json metaJson;
	try {
		file >> metaJson;
	} catch (const json::exception&) {
		return false;
	}

	std::filesystem::path assetPath = metaPath;
	assetPath.replace_extension("");
	outInfo.assetPath = NormalizePath(assetPath);
	outInfo.metaPath = NormalizePath(metaPath);
	outInfo.assetId = ReadString(metaJson, "assetId", "");
	outInfo.type = AssetTypeFromString(ReadString(metaJson, "assetType", ""));
	if (outInfo.type == AssetType::Unknown) {
		outInfo.type = ClassifyAssetType(outInfo.assetPath);
	}
	outInfo.relativePath = MakeProjectRelativePath(outInfo.assetPath);
	return !outInfo.assetId.empty();
}

bool AssetDatabase::WriteMeta(const AssetInfo& info) const {
	if (info.metaPath.empty()) {
		return false;
	}

	std::error_code error;
	std::filesystem::create_directories(info.metaPath.parent_path(), error);
	if (error) {
		return false;
	}

	json metaJson;
	metaJson["version"] = 1;
	metaJson["assetId"] = info.assetId;
	metaJson["assetType"] = ToString(info.type);

	std::ofstream file(info.metaPath, std::ios::binary);
	if (!file) {
		return false;
	}
	file << metaJson.dump(2);
	file << "\n";
	return static_cast<bool>(file);
}

void AssetDatabase::RegisterAssetInfo(const AssetInfo& info) {
	if (info.assetId.empty()) {
		return;
	}

	std::string pathKey = MakePathKey(info.assetPath);
	auto duplicated = assetsById_.find(info.assetId);
	if (duplicated != assetsById_.end()) {
		if (MakePathKey(duplicated->second.assetPath) != pathKey) {
			AssetInfo newInfo = info;
			newInfo.assetId = GenerateAssetId();
			WriteMeta(newInfo);
			RegisterAssetInfo(newInfo);
			return;
		}
	}

	assetsById_[info.assetId] = info;
	assetIdByPath_[pathKey] = info.assetId;
}

std::string AssetDatabase::GenerateAssetId() const {
	static std::atomic<uint64_t> counter = 0;
	static std::random_device randomDevice;
	static std::mt19937_64 randomEngine(randomDevice());

	const uint64_t now = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
	const uint64_t sequence = ++counter;
	const uint64_t randomValue = randomEngine();

	std::ostringstream os;
	os << "asset_";
	os << std::hex << std::setfill('0');
	os << std::setw(16) << (now ^ randomValue);
	os << std::setw(16) << (sequence ^ (randomValue >> 1));
	return os.str();
}

void AssetDatabase::EnsureInitialized() {
	if (initialized_) {
		return;
	}

	projectRoot_ = GetProjectDataRoot();
	initialized_ = true;
	Refresh();
}

} // namespace KujataEngine
