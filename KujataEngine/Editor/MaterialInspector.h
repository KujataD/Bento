#pragma once

#include <filesystem>
#include <string>

namespace KujataEngine {

class ProjectWindow;
struct MaterialAssetData;

// ProjectウィンドウでMaterialアセットを選択した時に、Inspectorへ出す編集UIを描画する。
void DrawMaterialAssetInspector(ProjectWindow& projectWindow);

/// <summary>
/// Material Asset を保存し、今のシーンでそのマテリアルを使っている物へ反映する。
/// Inspector での編集と CUI の material.set は、どちらもここを通る(同じ処理)。
/// マテリアルはシーンとは別のファイルなので、シーンの Undo では戻らない。
/// </summary>
bool SaveMaterialAsset(const std::filesystem::path& materialPath, const MaterialAssetData& material, std::string& message);

} // namespace KujataEngine
