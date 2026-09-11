#include "SceneManager.h"

namespace KujataEngine {

namespace {
// KujataEngine.dll内で共有される切り替え要求の状態。
// ChangeScene(GameModule/Editorから呼ばれる)とConsume(EditorApplication)は同一DLL内なので共有される。
bool g_hasPendingSceneChange = false;
std::string g_pendingSceneName;
std::string g_activeSceneName;
} // namespace

void ChangeScene(const std::string& sceneName) {
	g_hasPendingSceneChange = true;
	g_pendingSceneName = sceneName;
}

std::string GetActiveSceneName() { return g_activeSceneName; }

bool ConsumePendingSceneChange(std::string& outName) {
	if (!g_hasPendingSceneChange) {
		return false;
	}
	outName = g_pendingSceneName;
	g_hasPendingSceneChange = false;
	g_activeSceneName = g_pendingSceneName; // 切り替え確定としてアクティブ名を更新する。
	return true;
}

} // namespace KujataEngine
