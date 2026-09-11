#pragma once

// ランタイムAPI一式(KujataEngine.h)に加えて、Editorモジュールを取り込むファサード。
// エディタ/アプリ(main等)向け。ランタイムやゲームコードはKujataEngine.hを使うこと。
#include "KujataEngine.h"

#include "Editor/ImGuiManager.h"
#include "Editor/AssetDatabase.h"
#include "Editor/EditorApplication.h"
#include "Editor/EditorSelection.h"
#include "Editor/PrefabAsset.h"
#include "Editor/SceneJsonExporter.h"
#include "Editor/SceneJsonImporter.h"
