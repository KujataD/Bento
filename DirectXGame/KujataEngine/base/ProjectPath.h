#pragma once

#include "../runtime/KujataApi.h"
#include <filesystem>

namespace KujataEngine {

/// <summary>
/// パス表記揺れを減らした形へ正規化する
/// </summary>
KUJATA_API std::filesystem::path NormalizeEditorPath(const std::filesystem::path& path);

/// <summary>
/// エンジン一式(KujataEngine.vcxproj・エディタ用アイコン等)が置かれたフォルダを返す。
/// 起動時カレントから上へ辿って探し、見つからなければカレント(配布先ではexeの隣)になる。
/// </summary>
KUJATA_API std::filesystem::path GetEngineRoot();

/// <summary>
/// 開いているプロジェクトのフォルダを返す。起動引数 `--project <フォルダ>` で指定し、
/// 未指定ならエンジンのフォルダと同じ。GameModule / Data / Temp はすべてここが基準。
/// </summary>
KUJATA_API std::filesystem::path GetActiveProjectRoot();

/// <summary>
/// エディタ用アイコン画像のフォルダを返す。プロジェクトではなくエンジンの持ち物。
/// </summary>
KUJATA_API std::filesystem::path GetEditorIconDirectory();

/// <summary>
/// 実行中exeが置かれているディレクトリを返す(配布パッケージ判定用)。
/// </summary>
KUJATA_API std::filesystem::path GetExecutableDirectory();

/// <summary>
/// ProjectSettings/SceneJson/Materials/Prefabs/Resources/Animations をまとめて置く
/// "Data"フォルダを返す(GetActiveProjectRoot()直下)。ソリューション実行/exe単体実行の
/// どちらでも同じ相対構成(Data/配下)からロードできるようにするための唯一の参照点。
/// GameComponents(GameModule.dll)からも参照するためKUJATA_APIでエクスポートする。
/// </summary>
KUJATA_API std::filesystem::path GetProjectDataRoot();

} // namespace KujataEngine
