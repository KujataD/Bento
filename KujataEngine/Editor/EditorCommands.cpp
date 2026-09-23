// エディタの CUI の標準コマンドを登録する。コマンドの中身は Commands/ に分野ごとに置いてある。
// 一覧と使い方は .claude/editor-automation.md の §5、またはエディタで help。
#include "EditorCommand.h"

namespace KujataEngine {

void RegisterCoreCommands(EditorCommandRegistry& registry);
void RegisterSceneCommands(EditorCommandRegistry& registry);
void RegisterSchemaCommands(EditorCommandRegistry& registry);
void RegisterViewCommands(EditorCommandRegistry& registry);
void RegisterPrefabCommands(EditorCommandRegistry& registry);
void RegisterAnimationCommands(EditorCommandRegistry& registry);
void RegisterMaterialCommands(EditorCommandRegistry& registry);
void RegisterActionCommands(EditorCommandRegistry& registry);

void RegisterBuiltinEditorCommands() {
	EditorCommandRegistry& registry = EditorCommandRegistry::GetInstance();
	RegisterCoreCommands(registry);
	RegisterSceneCommands(registry);
	RegisterSchemaCommands(registry);
	RegisterViewCommands(registry);
	RegisterPrefabCommands(registry);
	RegisterAnimationCommands(registry);
	RegisterMaterialCommands(registry);
	RegisterActionCommands(registry);
}

} // namespace KujataEngine
