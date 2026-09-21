// エディタの CUI のコマンド: マテリアル(保存と反映は SaveMaterialAsset。Material の Inspector と同じもの)。
// 設計と一覧は .claude/editor-automation.md。共通の関数は EditorCommandUtil にある。
#include "EditorCommandUtil.h"
#include "../AssetDatabase.h"
#include "../MaterialInspector.h"
#include "../../assets/MaterialAsset.h"
#include "../../base/ProjectPath.h"
#include <filesystem>
#include <string>
#include <system_error>

namespace KujataEngine {

namespace {

using namespace EditorCommandUtil;
using nlohmann::json;

// "Materials/Toon.material.json" のような Data 基準の相対パス、または絶対パスでマテリアルを探す。
bool ResolveMaterialPath(const std::string& text, std::filesystem::path& outPath, std::string& error) {
	if (text.empty()) {
		error = "マテリアルのパスを指定してください(material.list で一覧)。";
		return false;
	}
	std::filesystem::path path(text);
	if (path.is_relative()) {
		path = GetProjectDataRoot() / path;
	}
	std::error_code errorCode;
	if (!MaterialAsset::IsMaterialFile(path) || !std::filesystem::exists(path, errorCode)) {
		error = "マテリアルが見つかりません: " + text + "(*.material.json を Data からの相対パスで指定。material.list で一覧)";
		return false;
	}
	outPath = path;
	return true;
}

json DescribeMaterial(const std::filesystem::path& path, const MaterialAssetData& material) {
	json value;
	std::error_code errorCode;
	value["path"] = std::filesystem::relative(path, GetProjectDataRoot(), errorCode).generic_string();
	json fields = json::object();
	MaterialAsset::WriteJsonObject(fields, material);
	value["fields"] = fields;
	return value;
}

EditorCommandResult CommandMaterialList(const EditorCommandArgs&) {
	json materials = json::array();
	const std::filesystem::path dataRoot = GetProjectDataRoot();
	std::error_code errorCode;
	for (auto it = std::filesystem::recursive_directory_iterator(dataRoot, errorCode); !errorCode && it != std::filesystem::recursive_directory_iterator(); it.increment(errorCode)) {
		if (it->is_regular_file() && MaterialAsset::IsMaterialFile(it->path())) {
			materials.push_back(std::filesystem::relative(it->path(), dataRoot, errorCode).generic_string());
		}
	}
	return EditorCommandResult::Success(materials);
}

EditorCommandResult CommandMaterialCreate(const EditorCommandArgs& args) {
	const std::string name = MaterialAsset::SanitizeName(args.Count() > 0 ? args.Get(0) : "New Material");
	const std::filesystem::path directory = GetProjectDataRoot() / "Materials";
	std::error_code errorCode;
	std::filesystem::create_directories(directory, errorCode);
	const std::filesystem::path path = directory / (name + ".material.json");
	if (std::filesystem::exists(path, errorCode)) {
		return EditorCommandResult::Failure("同じ名前のマテリアルがあります: " + path.generic_string());
	}
	std::string message;
	if (!MaterialAsset::CreateDefaultFile(path, message)) {
		return EditorCommandResult::Failure(message);
	}
	AssetDatabase::GetInstance().GetOrCreateAssetId(path);
	MaterialAssetData material = MaterialAsset::CreateDefault();
	MaterialAsset::Load(path, material, message);
	return EditorCommandResult::Success(DescribeMaterial(path, material));
}

EditorCommandResult CommandMaterialGet(const EditorCommandArgs& args) {
	std::filesystem::path path;
	std::string error;
	if (!ResolveMaterialPath(args.Get(0), path, error)) {
		return EditorCommandResult::Failure(error);
	}
	MaterialAssetData material = MaterialAsset::CreateDefault();
	if (!MaterialAsset::Load(path, material, error)) {
		return EditorCommandResult::Failure(error);
	}
	return EditorCommandResult::Success(DescribeMaterial(path, material));
}

EditorCommandResult CommandMaterialSet(const EditorCommandArgs& args) {
	if (args.Count() < 3) {
		return EditorCommandResult::Failure("使い方: material.set <マテリアルのパス> <キー> <値>(例: material.set Materials/Toon.material.json toonSteps 4)");
	}
	std::filesystem::path path;
	std::string error;
	if (!ResolveMaterialPath(args.Get(0), path, error)) {
		return EditorCommandResult::Failure(error);
	}
	MaterialAssetData material = MaterialAsset::CreateDefault();
	if (!MaterialAsset::Load(path, material, error)) {
		return EditorCommandResult::Failure(error);
	}

	// 値は行末までを JSON として読む(field.set と同じ)。読めなければ文字列として扱う。
	const std::string& key = args.Get(1);
	const std::string rawValue = args.RestFrom(2);
	json value = json::parse(rawValue, nullptr, false);
	if (value.is_discarded()) {
		value = rawValue;
	}

	// 今の全フィールドを JSON にして 1 つだけ差し替え、読み戻す(保存形式と同じ手順なので、キー名はファイルと同じ)。
	json fields = json::object();
	MaterialAsset::WriteJsonObject(fields, material);
	if (key == "textures" || key == "name" || !fields.contains(key)) {
		std::string keys;
		for (auto it = fields.begin(); it != fields.end(); ++it) {
			if (it.key() != "textures" && it.key() != "name") {
				keys += " " + it.key();
			}
		}
		return EditorCommandResult::Failure("書き換えられないキーです: " + key + "。書き換えられるキー:" + keys);
	}
	const json before = fields[key];
	const bool sameKind = (before.is_number() && value.is_number()) || (before.is_boolean() && value.is_boolean()) ||
	                      (before.is_array() && value.is_array() && value.size() == before.size());
	if (!sameKind) {
		return EditorCommandResult::Failure(key + " は " + before.dump() + " と同じ形の値で指定してください。");
	}
	// 整数の欄(shaderModel / toonSteps など)に 4.5 のような小数を入れても読み飛ばされないよう、整数にそろえる。
	if (before.is_number_integer() && value.is_number_float()) {
		value = static_cast<int>(value.get<double>());
	}
	fields[key] = value;
	material = MaterialAsset::ReadJsonObject(fields, material);

	if (!SaveMaterialAsset(path, material, error)) {
		return EditorCommandResult::Failure(error);
	}
	json result = DescribeMaterial(path, material);
	result["key"] = key;
	result["before"] = before;
	result["after"] = value;
	return EditorCommandResult::Success(result);
}

} // namespace

void RegisterMaterialCommands(EditorCommandRegistry& registry) {
	registry.Register("material.list", "material.list", "マテリアルファイルの一覧(Data からの相対パス)", CommandMaterialList);
	registry.Register("material.create", "material.create [名前]", "Data/Materials/<名前>.material.json を作る", CommandMaterialCreate);
	registry.Register("material.get", "material.get <マテリアルのパス>", "マテリアルの全フィールド(キー名はファイルと同じ)", CommandMaterialGet);
	registry.Register("material.set", "material.set <マテリアルのパス> <キー> <値>",
	                  "マテリアルのフィールドを書き換えて保存し、使っているオブジェクトへ反映する(Undo 不可。例: material.set Materials/Toon.material.json shaderModel 8)",
	                  CommandMaterialSet);
}

} // namespace KujataEngine
