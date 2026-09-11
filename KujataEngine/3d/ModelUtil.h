#pragma once
#include "../runtime/KujataApi.h"

#include "GraphicsPipeline.h"
#include <cstdint>
#include <string>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace KujataEngine::ModelUtil {

/// <summary>
/// モデルファイルを読み込み、エンジン用のモデルデータに変換する
/// </summary>
KUJATA_API ModelData LoadModelFile(const std::string& directoryPath, const std::string& filename);
KUJATA_API bool TryLoadModelFile(const std::string& directoryPath, const std::string& filename, ModelData& outModelData);

/// <summary>
/// aiNodeをNodeに変換する。
/// </summary>
KUJATA_API Node ReadNode(aiNode* node);

/// <summary>
/// MTLファイルからマテリアル情報を読み込む
/// </summary>
KUJATA_API MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);

/// <summary>
/// マテリアルのテクスチャパスからSRVインデックス 
/// </summary>
KUJATA_API void ResolveTextureIndex(MaterialData& material);

/// <summary>
/// 指定テクスチャを使う基本マテリアルを作成する
/// </summary>
KUJATA_API MaterialData CreateTexturedMaterial(const std::string& textureFilePath, int32_t enableLighting);

/// <summary>
/// 3Dモデル描画で共通のビューポート・シザー・トポロジ 
/// </summary>
KUJATA_API void SetCommonRenderState();

} // namespace KujataEngine::ModelUtil
