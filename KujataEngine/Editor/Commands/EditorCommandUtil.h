#pragma once

#include "../EditorCommand.h"
#include "../../../externals/nlohmann/json.hpp"
#include <cstddef>
#include <filesystem>
#include <string>

namespace KujataEngine {

class Scene;
class GameObject;
class Component;

/// <summary>
/// CUI のコマンドが共通で使う関数(オブジェクトの探し方・値の読み方・Undo の取り方)。
/// コマンドごとに書き方がばらつかないよう、探す・読む・返す処理はここに揃える。
/// </summary>
namespace EditorCommandUtil {

/// <summary>今エディタで開いているシーン(無ければ nullptr)。</summary>
Scene* GetScene();

/// <summary>親をたどって "Stage/Enemies/Guardian" の形にする。</summary>
std::string MakeObjectPath(const GameObject* gameObject);

/// <summary>
/// "親/子" のパス、または instanceId("go_...")でオブジェクトを探す。
/// 同じパスのオブジェクトが複数あるときは、黙って先頭を選ばずにエラーにする。
/// </summary>
GameObject* ResolveObject(Scene& scene, const std::string& spec, std::string& error);

/// <summary>"ColliderComponent" または "ColliderComponent#1"(同じ型のうち何番目か。0始まり)でコンポーネントを探す。</summary>
Component* ResolveComponent(GameObject& gameObject, const std::string& spec, std::string& error);

/// <summary>コンポーネントの型名・有効/無効・全フィールドの値。sameTypeIndex が 1 以上なら型名に "#番号" を付ける。</summary>
nlohmann::json DescribeComponent(const Component& component, size_t sameTypeIndex);

/// <summary>同じ型のうち何番目か。</summary>
size_t SameTypeIndexOf(const GameObject& gameObject, const Component* target);

/// <summary>"true/1/on" と "false/0/off" を読む。読めなければ false を返す。</summary>
bool ParseBool(const std::string& text, bool& value);

/// <summary>文字列全体が数値として読めるときだけ true。</summary>
bool ParseFloat(const std::string& text, float& value);

/// <summary>変更の前に Undo を記録する(ラベルの頭に "[CUI] " を付ける)。</summary>
void CaptureUndo(Scene& scene, const std::string& label);

/// <summary>ファイル名に使える "年月日_時分秒"。</summary>
std::string MakeFileTimestamp();

/// <summary>相対パスはプロジェクトのフォルダ基準にする(起動したカレントフォルダに左右されないように)。</summary>
std::filesystem::path ResolveOutputPath(const std::string& text);

/// <summary>型情報(schema)の配列から key のフィールドを探す(無ければ nullptr)。</summary>
const nlohmann::json* FindFieldSchema(const nlohmann::json& fields, const std::string& key);

/// <summary>型情報に範囲があれば、値(数値、または数値の配列の各要素)が範囲内かを確かめる。</summary>
bool CheckRange(const nlohmann::json& fieldSchema, const nlohmann::json& value, std::string& error);

} // namespace EditorCommandUtil

} // namespace KujataEngine
