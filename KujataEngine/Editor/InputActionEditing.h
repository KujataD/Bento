#pragma once

#include "../input/InputActions.h"
#include <string>

namespace KujataEngine {

/// <summary>
/// アクション定義の編集(追加・削除・名前・種類・割り当て)。
/// Project Settings の Input Actions と CUI の action.* コマンドは、どちらもここを呼ぶ。
/// 変更はその場で `InputActions.json` に保存され、次の入力の読み取りから使われる。
/// </summary>
namespace InputActionEditing {

struct Result {
	bool succeeded = false;
	std::string message;
};

/// <summary>アクションを足す。名前が空・重複なら失敗。</summary>
Result AddAction(const std::string& name, InputActionType type);

Result RemoveAction(const std::string& name);

Result RenameAction(const std::string& name, const std::string& newName);

/// <summary>種類を変える。新しい種類で使えない割り当ては外す。</summary>
Result SetType(const std::string& name, InputActionType type);

/// <summary>割り当てを足す。種類に合わない名前・重複なら失敗。</summary>
Result AddBinding(const std::string& name, const std::string& binding);

/// <summary>index 番目の割り当てを差し替える。</summary>
Result SetBinding(const std::string& name, size_t index, const std::string& binding);

Result RemoveBinding(const std::string& name, const std::string& binding);

/// <summary>アクションの並びを入れ替える(一覧の上下)。</summary>
Result MoveAction(const std::string& name, int offset);

/// <summary>割り当てに書く既定の名前(その種類の選択肢の先頭)。</summary>
std::string GetDefaultBinding(InputActionType type);

} // namespace InputActionEditing

} // namespace KujataEngine
