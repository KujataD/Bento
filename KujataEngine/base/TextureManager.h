#pragma once
#include "../runtime/KujataApi.h"
#include <cstdint>
#include <d3d12.h>
#include <deque>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include <wrl.h>

#include "../3d/Model.h" // VertexData, MaterialData を共有

#include "../../externals/DirectXTex/DirectXTex.h"
#include "../../externals/DirectXTex/d3dx12.h"
#include "../math/Matrix4x4.h"
#include "../math/Vector2.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"

namespace KujataEngine {

class KUJATA_API TextureManager {
public:
	struct TextureLoadEvent {
		std::string filePath;
		float loadMs = 0.0f;
		bool cacheHit = false;
	};

	struct TextureData {
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
		uint32_t index;
	};

	void Initialize();

	static TextureManager* GetInstance();

	uint32_t LoadTexture(const std::string& filePath);

	/// <summary>
	/// テクスチャを読み込む。失敗してもassertせずfalseを返す
	/// </summary>
	bool TryLoadTexture(const std::string& filePath, uint32_t& outIndex);

	D3D12_GPU_DESCRIPTOR_HANDLE GetSrvHandle(uint32_t index);

	/// <summary>
	/// RGBA8のメモリ画像からテクスチャを生成し、SRVインデックスを返す。
	/// keyでキャッシュする(同一keyは再生成しない)。フォントアトラス等の動的テクスチャ用。
	/// </summary>
	uint32_t CreateTextureFromMemory(const std::string& key, const uint8_t* rgbaPixels, uint32_t width, uint32_t height);

	/// <summary>
	/// 既存キーのテクスチャへRGBA8画像を再アップロード(SRVインデックスは維持)。動的に成長するフォントアトラス用。
	/// keyが未生成なら新規生成する。描画パス外(Prepare相当)で呼ぶこと(内部でGPU待ちを行う)。
	/// </summary>
	uint32_t UpdateTextureFromMemory(const std::string& key, const uint8_t* rgbaPixels, uint32_t width, uint32_t height);
	const std::deque<TextureLoadEvent>& GetRecentLoadEvents() const { return recentLoadEvents_; }

	uint32_t GetDefaultWhiteTexture() const { return defaultWhiteTextureIndex_; }

	/// <summary>
	/// SRVインデックスからテクスチャのピクセルサイズを取得する。未登録ならfalse。
	/// Pixels Per Unitでスプライトの大きさを決めるのに使う。
	/// </summary>
	bool TryGetTextureSize(uint32_t index, uint32_t& outWidth, uint32_t& outHeight) const;

private:
	TextureManager() = default;
	~TextureManager() = default;
	TextureManager(const TextureManager&) = delete;
	const TextureManager& operator=(const TextureManager&) = delete;

	bool LoadTextureInternal(const std::string& filePath, uint32_t& outIndex, bool assertOnFailure);
	ID3D12Resource* CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metadata);
	
	// nodiscard : 戻り値を破棄してはいけない（破棄でエラー）
	[[nodiscard]]
	ID3D12Resource* UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages, ID3D12GraphicsCommandList* commandList);

private:
	// srvのインデックス管理用（Imgui用に0は空けておく）
	uint32_t srvIndexCounter_ = 1;
	uint32_t defaultWhiteTextureIndex_;

	// テクスチャコンテナ
	std::unordered_map<std::string, TextureData> textures_;
	std::deque<TextureLoadEvent> recentLoadEvents_;
};

} // namespace KujataEngine
