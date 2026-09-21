#include "EditorScreenshot.h"

#include "../../externals/DirectXTex/DirectXTex.h"
#include "../base/DirectXCommon.h"
#include "../postprocess/PostProcess.h"
#include <cassert>
#include <cstring>
#include <system_error>

namespace KujataEngine {

namespace {

// 予約したのに対象が描かれない(ウィンドウが最小化されている等)まま、これだけ経ったら諦める。
constexpr int kMaxWaitFrames = 30;

void TransitionResource(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = resource;
	barrier.Transition.StateBefore = before;
	barrier.Transition.StateAfter = after;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	commandList->ResourceBarrier(1, &barrier);
}

} // namespace

EditorScreenshot& EditorScreenshot::GetInstance() {
	static EditorScreenshot instance;
	return instance;
}

const char* EditorScreenshot::ToName(Target target) {
	switch (target) {
	case Target::GameView:
		return "game";
	case Target::Editor:
		return "editor";
	case Target::SceneView:
	default:
		return "scene";
	}
}

bool EditorScreenshot::Request(Target target, const std::filesystem::path& outputPath, std::string& error) {
	if (pending_) {
		error = "前の撮影がまだ終わっていません。";
		return false;
	}
	Reset();
	pending_ = true;
	target_ = target;
	outputPath_ = outputPath;
	return true;
}

bool EditorScreenshot::WantsView(uint32_t viewIndex) const {
	if (!pending_ || recorded_) {
		return false;
	}
	if (target_ == Target::SceneView) {
		return viewIndex == DirectXCommon::kSceneViewIndex;
	}
	if (target_ == Target::GameView) {
		return viewIndex == DirectXCommon::kGameViewIndex;
	}
	return false;
}

void EditorScreenshot::RecordViewCopy(uint32_t viewIndex) {
	if (!WantsView(viewIndex)) {
		return;
	}
	// ポスト適用後のLDR RT。PostProcess::Renderの後はPIXEL_SHADER_RESOURCE状態(ImGui::Imageが読む)。
	ID3D12Resource* source = PostProcess::GetInstance()->GetDisplayResource(viewIndex);
	if (source) {
		RecordCopy(source, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}
}

void EditorScreenshot::RecordEditorCopy() {
	if (!pending_ || recorded_ || target_ != Target::Editor) {
		return;
	}
	// ImGuiまで描き終えたバックバッファ。PostDrawでPRESENTへ遷移する前なのでRENDER_TARGET状態。
	RecordCopy(DirectXCommon::GetInstance()->GetCurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET);
}

void EditorScreenshot::RecordCopy(ID3D12Resource* source, D3D12_RESOURCE_STATES stateBefore) {
	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	ID3D12Device* device = dxCommon->GetDevice();
	ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();

	// テクスチャは行ごとに256バイト境界へ揃えてしかバッファへコピーできないので、その並び(footprint)を求める。
	const D3D12_RESOURCE_DESC sourceDesc = source->GetDesc();
	UINT rowCount = 0;
	UINT64 rowSize = 0;
	device->GetCopyableFootprints(&sourceDesc, 0, 1, 0, &footprint_, &rowCount, &rowSize, &readbackSize_);

	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_READBACK;
	D3D12_RESOURCE_DESC bufferDesc{};
	bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferDesc.Width = readbackSize_;
	bufferDesc.Height = 1;
	bufferDesc.DepthOrArraySize = 1;
	bufferDesc.MipLevels = 1;
	bufferDesc.SampleDesc.Count = 1;
	bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	HRESULT hr = device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readbackBuffer_));
	if (FAILED(hr)) {
		readbackBuffer_.Reset();
		return;
	}

	D3D12_TEXTURE_COPY_LOCATION destination{};
	destination.pResource = readbackBuffer_.Get();
	destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	destination.PlacedFootprint = footprint_;

	D3D12_TEXTURE_COPY_LOCATION sourceLocation{};
	sourceLocation.pResource = source;
	sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	sourceLocation.SubresourceIndex = 0;

	TransitionResource(commandList, source, stateBefore, D3D12_RESOURCE_STATE_COPY_SOURCE);
	commandList->CopyTextureRegion(&destination, 0, 0, 0, &sourceLocation, nullptr);
	TransitionResource(commandList, source, D3D12_RESOURCE_STATE_COPY_SOURCE, stateBefore);
	recorded_ = true;
}

std::optional<nlohmann::json> EditorScreenshot::Poll(std::string& error) {
	assert(pending_);
	if (!recorded_) {
		if (++waitedFrames_ > kMaxWaitFrames) {
			error = std::string(ToName(target_)) + " が描画されませんでした(エディタのウィンドウが最小化されていないか確認してください)。";
			Reset();
			return nlohmann::json(nullptr);
		}
		return std::nullopt;
	}

	// 前のフレームのPostDrawでGPUの完了を待っているので、コピーは終わっている。
	const bool saved = SavePng(error);
	nlohmann::json result = nullptr;
	if (saved) {
		result["path"] = outputPath_.string();
		result["target"] = ToName(target_);
		result["width"] = footprint_.Footprint.Width;
		result["height"] = footprint_.Footprint.Height;
	}
	Reset();
	return result;
}

bool EditorScreenshot::SavePng(std::string& error) {
	if (!readbackBuffer_) {
		error = "読み出し用のバッファを作れませんでした。";
		return false;
	}

	const uint32_t width = footprint_.Footprint.Width;
	const uint32_t height = footprint_.Footprint.Height;
	if (footprint_.Footprint.Format != DXGI_FORMAT_R8G8B8A8_UNORM && footprint_.Footprint.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
		error = "想定外のピクセル形式です(RGBA8 以外)。";
		return false;
	}

	D3D12_RANGE readRange{0, static_cast<SIZE_T>(readbackSize_)};
	void* mapped = nullptr;
	if (FAILED(readbackBuffer_->Map(0, &readRange, &mapped))) {
		error = "読み出し用のバッファを開けませんでした。";
		return false;
	}

	// 行の詰め物(256バイト境界)を外して詰め直す。アルファは描画に使われない値が入っているので不透明にする。
	DirectX::ScratchImage image;
	HRESULT hr = image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1);
	if (SUCCEEDED(hr)) {
		const uint8_t* sourceBase = static_cast<const uint8_t*>(mapped) + footprint_.Offset;
		const DirectX::Image* destination = image.GetImage(0, 0, 0);
		for (uint32_t y = 0; y < height; ++y) {
			const uint8_t* sourceRow = sourceBase + static_cast<size_t>(y) * footprint_.Footprint.RowPitch;
			uint8_t* destinationRow = destination->pixels + static_cast<size_t>(y) * destination->rowPitch;
			std::memcpy(destinationRow, sourceRow, static_cast<size_t>(width) * 4);
			for (uint32_t x = 0; x < width; ++x) {
				destinationRow[x * 4 + 3] = 0xff;
			}
		}
	}
	D3D12_RANGE writtenRange{0, 0};
	readbackBuffer_->Unmap(0, &writtenRange);
	if (FAILED(hr)) {
		error = "画像のメモリを確保できませんでした。";
		return false;
	}

	std::error_code errorCode;
	if (outputPath_.has_parent_path()) {
		std::filesystem::create_directories(outputPath_.parent_path(), errorCode);
	}
	// RTの中身はすでにsRGBで符号化された値なので、変換せずにそのままPNGへ書く。
	hr = DirectX::SaveToWICFile(*image.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE, DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), outputPath_.wstring().c_str());
	if (FAILED(hr)) {
		error = "PNG を書き出せませんでした: " + outputPath_.string();
		return false;
	}
	return true;
}

void EditorScreenshot::Reset() {
	pending_ = false;
	recorded_ = false;
	waitedFrames_ = 0;
	readbackBuffer_.Reset();
	footprint_ = {};
	readbackSize_ = 0;
}

} // namespace KujataEngine
