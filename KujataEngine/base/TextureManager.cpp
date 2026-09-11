#include "TextureManager.h"
#include "../3d/GraphicsPipeline.h"
#include "DirectXCommon.h"
#include "ProjectPath.h"
#include <wincodec.h>

#include "WinApp.h"
#include <cctype>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace KujataEngine {
void TextureManager::Initialize() {
	defaultWhiteTextureIndex_ = LoadTexture((GetEngineDataRoot() / "white1x1.png").generic_string());
}

TextureManager* TextureManager::GetInstance() {
	static TextureManager instance;
	return &instance;
}

uint32_t TextureManager::LoadTexture(const std::string& filePath) {
	uint32_t textureIndex = 0;
	// 通常のゲーム用テクスチャ読み込みは従来通り、失敗時にassertして問題を見つけやすくする。
	LoadTextureInternal(filePath, textureIndex, true);
	return textureIndex;
}

bool TextureManager::TryLoadTexture(const std::string& filePath, uint32_t& outIndex) {
	// Project Windowは任意の画像ファイルを扱うため、読み込み失敗でエディタ全体を止めない。
	return LoadTextureInternal(filePath, outIndex, false);
}

bool TextureManager::LoadTextureInternal(const std::string& filePath, uint32_t& outIndex, bool assertOnFailure) {
	auto loadStart = std::chrono::steady_clock::now();
	// 既に読み込んだテクスチャはSRV番号だけ返す。画像プレビューでも毎フレーム読み直さない。
	if (textures_.contains(filePath)) {
		recentLoadEvents_.push_front({filePath, 0.0f, true});
		if (recentLoadEvents_.size() > 32) {
			recentLoadEvents_.pop_back();
		}
		outIndex = textures_[filePath].index;
		return true;
	}
	ID3D12Device* device = DirectXCommon::GetInstance()->GetDevice();

	DirectX::ScratchImage image{};
	// std::filesystem::path経由でネイティブなワイド文字列へ戻す。
	// バイト単位のコピーだと日本語を含むパス(例: デスクトップ配下)で必ず失敗する。
	std::filesystem::path texturePath(filePath);
	HRESULT hr = DirectX::LoadFromWICFile(texturePath.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
	if (FAILED(hr)) {
		std::string msg = "Failed to load texture: " + filePath + "\n";
		OutputDebugStringA(msg.c_str());
		// ゲーム本体の必須テクスチャはassert、Project Windowのプレビューはfalseで呼び出し元へ返す。
		if (assertOnFailure) {
			assert(false);
		}
		return false;
	}

	DirectX::ScratchImage mipImages{};
	// 1x1など最小サイズの場合はミップ生成をスキップ
	if (image.GetMetadata().width > 1 && image.GetMetadata().height > 1) {
		hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_SRGB, 0, mipImages);
		if (FAILED(hr)) {
			std::string msg = "Failed to generate mipmaps: " + filePath + "\n";
			OutputDebugStringA(msg.c_str());
			// プレビュー画像が壊れていてもProject Window側では通常ファイルアイコンへフォールバックできる。
			if (assertOnFailure) {
				assert(false);
			}
			return false;
		}
	} else {
		// そのままコピー
		hr = mipImages.InitializeFromImage(*image.GetImages());
		if (FAILED(hr)) {
			std::string msg = "Failed to initialize mip image: " + filePath + "\n";
			OutputDebugStringA(msg.c_str());
			// ここもassertOnFailureで、必須テクスチャと任意プレビューの挙動を分ける。
			if (assertOnFailure) {
				assert(false);
			}
			return false;
		}
	}

	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> textureResource;
	textureResource.Attach(CreateTextureResource(device, metadata));

	// データ転送
	auto intermediate = UploadTextureData(textureResource.Get(), mipImages, DirectXCommon::GetInstance()->GetCommandList());

	DirectXCommon::GetInstance()->ExecuteCommandAndWait();

	if (intermediate) {
		intermediate->Release();
		intermediate = nullptr;
	}

	// SRV生成
	ID3D12DescriptorHeap* srvHeap = DirectXCommon::GetInstance()->GetSrvDescriptorHeap();
	const UINT descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// SRV番号はDirectXCommonで一元管理する。
	// Gameウィンドウ用RenderTargetやImGuiフォント用SRVと、通常テクスチャの番号が衝突しないようにする。
	uint32_t srvIndex = DirectXCommon::GetInstance()->AllocateSrvIndex();

	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = srvHeap->GetCPUDescriptorHandleForHeapStart();
	cpuHandle.ptr += descriptorSizeSRV * srvIndex;

	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = srvHeap->GetGPUDescriptorHandleForHeapStart();
	gpuHandle.ptr += descriptorSizeSRV * srvIndex;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

	device->CreateShaderResourceView(textureResource.Get(), &srvDesc, cpuHandle);

	// 登録
	TextureData data{};
	data.resource = textureResource;
	data.cpuHandle = cpuHandle;
	data.gpuHandle = gpuHandle;
	data.index = srvIndex;

	textures_[filePath] = data;
	
	float loadMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - loadStart).count();
	recentLoadEvents_.push_front({filePath, loadMs, false});
	if (recentLoadEvents_.size() > 32) {
		recentLoadEvents_.pop_back();
	}

	outIndex = srvIndex;
	return true;
}

uint32_t TextureManager::CreateTextureFromMemory(const std::string& key, const uint8_t* rgbaPixels, uint32_t width, uint32_t height) {
	if (textures_.contains(key)) {
		return textures_[key].index;
	}

	ID3D12Device* device = DirectXCommon::GetInstance()->GetDevice();

	// RGBA8のScratchImageを用意し、行ごとにコピー。ミップは作らない(フォントは1ミップ固定=にじみ防止)。
	DirectX::ScratchImage image{};
	HRESULT hr = image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1);
	assert(SUCCEEDED(hr));
	const DirectX::Image* destImage = image.GetImage(0, 0, 0);
	for (uint32_t y = 0; y < height; ++y) {
		std::memcpy(destImage->pixels + destImage->rowPitch * y, rgbaPixels + static_cast<size_t>(width) * 4 * y, static_cast<size_t>(width) * 4);
	}

	const DirectX::TexMetadata& metadata = image.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> textureResource;
	textureResource.Attach(CreateTextureResource(device, metadata));

	auto intermediate = UploadTextureData(textureResource.Get(), image, DirectXCommon::GetInstance()->GetCommandList());
	DirectXCommon::GetInstance()->ExecuteCommandAndWait();
	if (intermediate) {
		intermediate->Release();
		intermediate = nullptr;
	}

	ID3D12DescriptorHeap* srvHeap = DirectXCommon::GetInstance()->GetSrvDescriptorHeap();
	const UINT descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	uint32_t srvIndex = DirectXCommon::GetInstance()->AllocateSrvIndex();

	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = srvHeap->GetCPUDescriptorHandleForHeapStart();
	cpuHandle.ptr += descriptorSizeSRV * srvIndex;
	D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = srvHeap->GetGPUDescriptorHandleForHeapStart();
	gpuHandle.ptr += descriptorSizeSRV * srvIndex;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView(textureResource.Get(), &srvDesc, cpuHandle);

	TextureData data{};
	data.resource = textureResource;
	data.cpuHandle = cpuHandle;
	data.gpuHandle = gpuHandle;
	data.index = srvIndex;
	textures_[key] = data;
	return srvIndex;
}

uint32_t TextureManager::UpdateTextureFromMemory(const std::string& key, const uint8_t* rgbaPixels, uint32_t width, uint32_t height) {
	auto found = textures_.find(key);
	if (found == textures_.end()) {
		// 未生成なら新規生成にフォールバック。
		return CreateTextureFromMemory(key, rgbaPixels, width, height);
	}

	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	ID3D12Resource* resource = found->second.resource.Get();

	// RGBA8のScratchImageを用意し、行ごとにコピー。
	DirectX::ScratchImage image{};
	HRESULT hr = image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1);
	assert(SUCCEEDED(hr));
	const DirectX::Image* destImage = image.GetImage(0, 0, 0);
	for (uint32_t y = 0; y < height; ++y) {
		std::memcpy(destImage->pixels + destImage->rowPitch * y, rgbaPixels + static_cast<size_t>(width) * 4 * y, static_cast<size_t>(width) * 4);
	}

	// 既存リソースはGENERIC_READ状態なので、コピー前にCOPY_DESTへ戻す(UploadTextureDataがCOPY_DEST→GENERIC_READに戻す)。
	D3D12_RESOURCE_BARRIER toCopyDest{};
	toCopyDest.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	toCopyDest.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	toCopyDest.Transition.pResource = resource;
	toCopyDest.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	toCopyDest.Transition.StateBefore = D3D12_RESOURCE_STATE_GENERIC_READ;
	toCopyDest.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
	dxCommon->GetCommandList()->ResourceBarrier(1, &toCopyDest);

	auto intermediate = UploadTextureData(resource, image, dxCommon->GetCommandList());
	dxCommon->ExecuteCommandAndWait();
	if (intermediate) {
		intermediate->Release();
		intermediate = nullptr;
	}

	return found->second.index;
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetSrvHandle(uint32_t index) {
	DirectXCommon* dxCommon = DirectXCommon::GetInstance();

	ID3D12DescriptorHeap* heap = dxCommon->GetSrvDescriptorHeap();
	UINT size = dxCommon->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	D3D12_GPU_DESCRIPTOR_HANDLE handle = heap->GetGPUDescriptorHandleForHeapStart();
	handle.ptr += size * index;
	return handle;
}

bool TextureManager::TryGetTextureSize(uint32_t index, uint32_t& outWidth, uint32_t& outHeight) const {
	// 登録数は多くないので線形探索で十分(テクスチャ差し替え時にしか呼ばれない)。
	for (const auto& [key, data] : textures_) {
		if (data.index != index || !data.resource) {
			continue;
		}
		D3D12_RESOURCE_DESC desc = data.resource->GetDesc();
		outWidth = static_cast<uint32_t>(desc.Width);
		outHeight = static_cast<uint32_t>(desc.Height);
		return outWidth > 0 && outHeight > 0;
	}
	return false;
}

ID3D12Resource* TextureManager::CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metadata) {
	// 1.metadataを基にResourceの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = UINT(metadata.width);                             // Textureの幅
	resourceDesc.Height = UINT(metadata.height);                           // Textureの高さ
	resourceDesc.MipLevels = UINT16(metadata.mipLevels);                   // mipmapの数
	resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize);            // 奥行き or 配列Textureの配列数
	resourceDesc.Format = metadata.format;                                 // Texture@Format
	resourceDesc.SampleDesc.Count = 1;                                     // サンプリングカウント。1固定。
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension); // Textureの次元数。普段使っているのは2次元

	// 2.利用するHeapの設定。非常に特殊な運用。02_04exで一般的なケース版がある
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT; // 細かい設定を行う

	// 3.Resourceを生成する
	ID3D12Resource* resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(
	    &heapProperties,                // Heapの設定
	    D3D12_HEAP_FLAG_NONE,           // Heapの特殊な設定。特になし。
	    &resourceDesc,                  // Resourceの設定
	    D3D12_RESOURCE_STATE_COPY_DEST, // データ転送される設定
	    nullptr,                        // Clear最適値。使わないのでnullptr
	    IID_PPV_ARGS(&resource));       // 作成するResourceポインタへのポインタ
	assert(SUCCEEDED(hr));
	return resource;
}

ID3D12Resource* TextureManager::UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages, ID3D12GraphicsCommandList* commandList) {

	DirectXCommon* dxCommon = DirectXCommon::GetInstance();

	// PrepareUploadを利用して、読み込んだデータからDirectX12用のSubresourceの配列を作成する
	// Subresourceは、MipMapの1枚1枚ぐらいのイメージ
	std::vector<D3D12_SUBRESOURCE_DATA> subresources;
	// Subresourceの数を基に、コピー元となるIntermediateResourceに必要なサイズを計算する
	DirectX::PrepareUpload(dxCommon->GetDevice(), mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);
	// 計算したサイズでIntermediateResourceを作る
	uint64_t intermediateSize = GetRequiredIntermediateSize(texture, 0, UINT(subresources.size()));
	// CPUとGPUを取り持つためのResourceなので、IntermediateResource(中間リソース)と呼ぶ
	ID3D12Resource* intermediateResource = dxCommon->CreateBufferResource(intermediateSize);

	// ResourceStateを変更し、IntermediateResourceを返す
	// UpdateSubresourcesを利用して、IntermediateResourceにSubresourceのデータを書き込み、textureに転送するコマンドを積む
	UpdateSubresources(commandList, texture, intermediateResource, 0, 0, UINT(subresources.size()), subresources.data());
	// Textureへの転送後は利用できるよう、D3D12_RESOURCE_STATE_COPY_DESTからD3D12_RESOURCE_STATE_GENERIC_READへResourceStateを変更する
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = texture;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
	commandList->ResourceBarrier(1, &barrier);
	return intermediateResource;
}

} // namespace KujataEngine
