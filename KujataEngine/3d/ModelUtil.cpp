#include "ModelUtil.h"
#include "../base/DirectXCommon.h"
#include "../base/StringUtil.h"
#include "../base/TextureManager.h"
#include "../base/WinApp.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace KujataEngine::ModelUtil {
namespace {

// textureFilePathはAssimpが返すUTF-8文字列。directoryPathはstd::filesystem::path由来のネイティブ表現。
// 両者はエンコーディングが異なりうるため、texture側だけUTF-8として復元してから結合する。
std::string ResolveTexturePath(const std::string& directoryPath, const std::string& utf8TextureFilePath) {
	// OBJ/MTLから見た相対パスは、モデルのディレクトリ基準に変換する。
	std::filesystem::path texturePath(StringUtil::ToWString(utf8TextureFilePath));
	if (texturePath.is_absolute()) {
		return texturePath.generic_string();
	}

	std::filesystem::path resolvedPath = std::filesystem::path(directoryPath) / texturePath;
	return resolvedPath.generic_string();
}

} // namespace

ModelData LoadModelFile(const std::string& directoryPath, const std::string& filename) {
	ModelData modelData;
	if (!TryLoadModelFile(directoryPath, filename, modelData)) {
		// 読み込み失敗でもクラッシュさせず、空モデルを返す(呼び出し側は何も描画しない)。
		std::string message = "LoadModelFile failed (empty model returned): " + directoryPath + "/" + filename + "\n";
		OutputDebugStringA(message.c_str());
	}
	return modelData;
}

bool TryLoadModelFile(const std::string& directoryPath, const std::string& filename, ModelData& outModelData) {
	outModelData = {};

	// Assimpを使ってモデルファイルを読む
	// ------------------------------------------
	Assimp::Importer importer;
	std::string filePath = directoryPath + "/" + filename;
	// AssimpのIOSystemはナローパスをUTF-8として解釈するため、明示的にUTF-8へ変換して渡す。
	// (path::string()はACP(日本語環境ではCP932)を返しうるので、そのままでは日本語パスで開けない)
	std::string utf8FilePath = StringUtil::ToString(std::filesystem::path(filePath).wstring());

	// aiProcess_FlipWindingOrder: 三角形の並び順を逆にする。
	// aiProcess_Triangulate: 四角形以上の面を三角形に分割する。
	// aiProcess_FlipUVs: UVをフリップする。
	// aiProcess_GenSmoothNormals: 法線がないモデルでも、可能ならAssimp側で法線を作ってプレビューできるようにする。
	const unsigned int importFlags = aiProcess_FlipWindingOrder | aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenSmoothNormals;
	const aiScene* scene = importer.ReadFile(utf8FilePath.c_str(), importFlags);
	if (!scene) {
		std::string message = "Failed to load model: " + filePath + "\n";
		OutputDebugStringA(message.c_str());
		return false;
	}
	if (!scene->HasMeshes()) {
		std::string message = "Model has no meshes: " + filePath + "\n";
		OutputDebugStringA(message.c_str());
		return false;
	}
	if (!scene->mRootNode) {
		std::string message = "Model has no root node: " + filePath + "\n";
		OutputDebugStringA(message.c_str());
		return false;
	}

	// RootNodeのローカル行列を保持し、描画時のモデル固有補正として利用する。
	outModelData.rootNode = ReadNode(scene->mRootNode);

	// 各マテリアルのDiffuseテクスチャパスを事前に解決する(mesh->mMaterialIndexで参照)。
	std::vector<std::string> materialTexturePaths(scene->mNumMaterials);
	for (uint32_t materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex) {
		aiMaterial* material = scene->mMaterials[materialIndex];
		if (material->GetTextureCount(aiTextureType_DIFFUSE) != 0) {
			aiString textureFilePath;
			material->GetTexture(aiTextureType_DIFFUSE, 0, &textureFilePath);
			materialTexturePaths[materialIndex] = ResolveTexturePath(directoryPath, textureFilePath.C_Str());
		}
	}

	// meshを解析する。各サブメッシュを個別に保持しつつ、後方互換用に統合頂点も作る。
	for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
		aiMesh* mesh = scene->mMeshes[meshIndex];
		// ProjectのプレビューではUVなしモデルも表示したいので、足りない情報は既定値で補う。
		bool hasNormals = mesh->HasNormals();
		bool hasTextureCoords = mesh->HasTextureCoords(0);

		MeshData subMesh{};
		// このメッシュが参照するマテリアルのテクスチャを設定する。
		if (mesh->mMaterialIndex < materialTexturePaths.size()) {
			subMesh.material.textureFilePath = materialTexturePaths[mesh->mMaterialIndex];
		}

		// faceを解析する
		for (uint32_t faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex) {
			aiFace& face = mesh->mFaces[faceIndex];
			if (face.mNumIndices != 3) {
				std::string message = "Model face is not triangle: " + filePath + "\n";
				OutputDebugStringA(message.c_str());
				return false;
			}

			// vertexを解析する
			for (uint32_t element = 0; element < face.mNumIndices; ++element) {
				uint32_t vertexIndex = face.mIndices[element];
				aiVector3D& position = mesh->mVertices[vertexIndex];

				VertexData vertex;
				vertex.position = { position.x, position.y, position.z, 1.0f };
				if (hasNormals) {
					aiVector3D& normal = mesh->mNormals[vertexIndex];
					vertex.normal = { normal.x, normal.y, normal.z };
				} else {
					vertex.normal = {0.0f, 1.0f, 0.0f};
				}
				if (hasTextureCoords) {
					aiVector3D& texcoord = mesh->mTextureCoords[0][vertexIndex];
					vertex.texcoord = { texcoord.x, texcoord.y };
				} else {
					vertex.texcoord = {0.0f, 0.0f};
				}

				// aiProcess_MakeLeftHandedはZ反転なので、既存のX反転に合わせて手動変換する。
				vertex.position.x *= -1.0f;
				vertex.normal.x *= -1.0f;
				subMesh.vertices.push_back(vertex);
				outModelData.vertices.push_back(vertex); // 後方互換(統合)
			}
		}

		outModelData.meshes.push_back(std::move(subMesh));
	}

	// 後方互換: 代表マテリアルのテクスチャ(従来通り、最後の有効なテクスチャを採用)。
	for (const std::string& texturePath : materialTexturePaths) {
		if (!texturePath.empty()) {
			outModelData.material.textureFilePath = texturePath;
		}
	}

	return true;
}

Node ReadNode(aiNode* node) {
	Node result;
	aiMatrix4x4 aiLocalMatrix = node->mTransformation; // nodeのlocalMatrixを取得
	aiLocalMatrix.Transpose();                         // 列ベクトル形式を行ベクトル形式に転置
	for (uint32_t i = 0; i < 4; ++i) {
		for (uint32_t j = 0; j < 4; ++j) {
			result.localMatrix.m[i][j] = aiLocalMatrix[i][j];
		}
	}
	result.name = node->mName.C_Str();          // Node名を格納
	result.children.resize(node->mNumChildren); // 子供の数だけ確保
	for (uint32_t childIndex = 0; childIndex < node->mNumChildren; ++childIndex) {
		// 再帰的に読んで階層構造を作っていく
		result.children[childIndex] = ReadNode(node->mChildren[childIndex]);
	}
	return result;
}

MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename) {
	MaterialData materialData;
	std::string line;
	// ナロー文字列のままifstreamへ渡すと日本語を含むパスで開けないため、pathで受け渡す。
	std::ifstream file(std::filesystem::path(directoryPath) / filename);
	assert(file.is_open());

	// MTLのmap_Kdからディフューズテクスチャを取得する。
	while (std::getline(file, line)) {
		std::string identifier;
		std::istringstream s(line);
		s >> identifier;

		if (identifier == "map_Kd") {
			std::string textureFilename;
			s >> textureFilename;
			// 絶対パスならそのまま、相対パスならモデルディレクトリ基準に変換する。
			materialData.textureFilePath = ResolveTexturePath(directoryPath, textureFilename);
		}
	}

	return materialData;
}

void ResolveTextureIndex(MaterialData& material) {
	TextureManager* textureManager = TextureManager::GetInstance();
	// テクスチャ指定がないモデルは、白1x1テクスチャを使って描画可能にする。
	if (!material.textureFilePath.empty()) {
		material.textureIndex = textureManager->LoadTexture(material.textureFilePath);
		return;
	}

	material.textureIndex = textureManager->GetDefaultWhiteTexture();
}

MaterialData CreateTexturedMaterial(const std::string& textureFilePath, int32_t enableLighting) {
	// プリミティブ生成用の基本マテリアルを作成する。
	MaterialData material;
	material.enableLighting = enableLighting;
	material.textureFilePath = textureFilePath;
	ResolveTextureIndex(material);
	return material;
}

void SetCommonRenderState() {
	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();

	// 描画用のDescriptorHeapの設定
	ID3D12DescriptorHeap* descriptorHeaps[] = { dxCommon->GetSrvDescriptorHeap() };
	commandList->SetDescriptorHeaps(1, descriptorHeaps);

	// ビューポートの設定
	D3D12_VIEWPORT viewport{};
	viewport.Width = static_cast<float>(WinApp::kWindowWidth);
	viewport.Height = static_cast<float>(WinApp::kWindowHeight);
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	commandList->RSSetViewports(1, &viewport);

	// シザー矩形の設定
	D3D12_RECT scissorRect{};
	scissorRect.left = 0;
	scissorRect.right = WinApp::kWindowWidth;
	scissorRect.top = 0;
	scissorRect.bottom = WinApp::kWindowHeight;
	commandList->RSSetScissorRects(1, &scissorRect);

	// プリミティブトポロジの設定（main.cppで毎フレームセットしていたものに対応）
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

} // namespace KujataEngine::ModelUtil
