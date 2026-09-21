#pragma once

#include <filesystem>
#include <string>
#include <vector>

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

/// <summary>
/// 自作シェーダーのひな形を <プロジェクト>/Data/Shaders/<名前>.hlsl に作る(同じ名前があれば失敗。上書きしない)。
/// outRelativePath は Data からの相対パス(マテリアルの shaderPath にそのまま入れられる)。
/// Material の Inspector の New ボタンと CUI の shader.create は、どちらもここを呼ぶ。
/// </summary>
bool CreateCustomShaderFile(const std::string& name, std::string& outRelativePath, std::string& message);

/// <summary>プロジェクトの Data 配下の .hlsl を、Data からの相対パスで列挙する。</summary>
std::vector<std::string> ListCustomShaderFiles();

} // namespace KujataEngine
