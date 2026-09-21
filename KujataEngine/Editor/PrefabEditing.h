#pragma once

#include <filesystem>
#include <string>

namespace KujataEngine {

class Scene;
class GameObject;

/// <summary>
/// エディタでのプレハブ操作(作成・配置・Apply・Revert・Unpack)。
/// Hierarchy / Inspector のボタンと CUI の prefab.* コマンドは、どちらもここを呼ぶ(同じ処理・同じログ・同じUndo)。
/// PrefabAsset がファイルとの読み書きを受け持ち、ここはエディタとしての後始末(Undo・選択・ログ)を足す。
/// </summary>
namespace PrefabEditing {

struct Result {
	bool succeeded = false;
	// 作成・配置・Revert後のルート(配置やRevertでは新しいオブジェクトになる)。
	GameObject* object = nullptr;
	// プレハブファイルの場所(作成・Applyのとき)。
	std::filesystem::path prefabPath;
	std::string message;
};

/// <summary>オブジェクトと子階層をプレハブとして保存し、そのオブジェクトをプレハブのインスタンスにする。</summary>
Result Create(Scene& scene, GameObject& object, const std::string& undoLabel);

/// <summary>プレハブを配置して選択する。parentを渡すとその子にする。</summary>
Result Instantiate(Scene& scene, const std::filesystem::path& prefabPath, GameObject* parent, const std::string& undoLabel);

/// <summary>インスタンスの変更をプレハブファイルへ書き戻す(シーンの他のインスタンスも更新される)。</summary>
Result Apply(Scene& scene, GameObject& instance);

/// <summary>
/// インスタンスをプレハブの内容に戻す。既存のオブジェクトはそのまま使い回して中身を上書きし、
/// プレハブに無いオブジェクトは消え、足りないものは作られる。
/// </summary>
Result Revert(Scene& scene, GameObject& instance, const std::string& undoLabel);

/// <summary>インスタンスとプレハブのつながりを切り、普通のオブジェクトにする。</summary>
Result Unpack(Scene& scene, GameObject& instance, const std::string& undoLabel);

} // namespace PrefabEditing

} // namespace KujataEngine
