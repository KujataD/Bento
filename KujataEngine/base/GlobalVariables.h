#pragma once

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 26495)
#pragma warning(disable : 26819)
#endif
#include "../runtime/KujataApi.h"
#include "../../externals/nlohmann/json.hpp"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "../math/Vector3.h"
#include "ProjectPath.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <variant>

namespace KujataEngine {

/// <summary>
/// グローバル変数
/// </summary>
class KUJATA_API GlobalVariables {
public:
	static GlobalVariables* GetInstance();

	// 構造体宣言
	// ------------------------------------------

	// 項目
	using Item = std::variant<int32_t, float, KujataEngine::Vector3>;

	// グループ
	using Group = std::map<std::string, Item>;

	// 各機能
	// ------------------------------------------

	void Update();

	void CreateGroup(const std::string& groupName);

	// 値のセット(int)
	void SetValue(const std::string& groupName, const std::string& key, int32_t value);

	// 値のセット(float)
	void SetValue(const std::string& groupName, const std::string& key, float value);

	// 値のセット(Vector3)
	void SetValue(const std::string& groupName, const std::string& key, const KujataEngine::Vector3& value);

	// 項目の追加(int)
	void AddItem(const std::string& groupName, const std::string& key, int32_t value);

	// 項目の追加(float)
	void AddItem(const std::string& groupName, const std::string& key, float value);

	// 項目の追加(Vector3)
	void AddItem(const std::string& groupName, const std::string& key, const KujataEngine::Vector3& value);

	template<typename T> T GetValue(const std::string& groupName, const std::string& key) const {
		assert(datas_.find(groupName) != datas_.end());
		// グループの参照を取得
		const Group& group = datas_.at(groupName);

		// 指定グループに指定のキーが存在する
		assert(group.find(key) != group.end());

		// 指定グループから指定のキーの値を登録
		return std::get<T>(group.at(key));
	}


	void SaveFile(const std::string& groupName);

	/// <summary>
	/// ディレクトリの全ファイル読み込み
	/// </summary>
	void LoadFiles();

	void LoadFile(const std::string& groupName);

private:
	GlobalVariables() = default;
	~GlobalVariables() = default;
	GlobalVariables(const GlobalVariables&) = delete;
	const GlobalVariables& operator=(const GlobalVariables&) = delete;

private:
	// 全データ
	std::map<std::string, Group> datas_;

	// グローバル変数の保存先ファイルパス
	const std::string kDirectoryPath = (GetProjectDataRoot() / "Resources" / "GlobalVariables").generic_string() + "/";
};

} // namespace KujataEngine
