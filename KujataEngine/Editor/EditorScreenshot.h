#pragma once

#include "../../externals/nlohmann/json.hpp"
#include <cstdint>
#include <d3d12.h>
#include <filesystem>
#include <optional>
#include <string>
#include <wrl.h>

namespace KujataEngine {

/// <summary>
/// CUIの view.screenshot の中身。Scene/Gameビューの描画結果(ポスト適用後のLDR)か、
/// エディタ全体(ImGuiを描いた後のバックバッファ)をPNGに書き出す。
///
/// 流れ(3段階。GPUの描画が終わってから読むため、1フレームまたぐ):
///   1. Request    : コマンドが撮影を予約する(フレームの頭)
///   2. Record*Copy: 描画の途中で、撮る対象を読み出し用バッファへコピーする命令を積む
///   3. Poll       : 次のフレームの頭で、読み出し用バッファをPNGに保存して結果を返す
///                   (DirectXCommon::PostDrawがGPUの完了を待つので、この時点でコピーは終わっている)
/// </summary>
class EditorScreenshot {
public:
	enum class Target {
		SceneView,
		GameView,
		Editor,
	};

	static EditorScreenshot& GetInstance();

	/// <summary>撮影を予約する。予約中の撮影があれば失敗する。</summary>
	bool Request(Target target, const std::filesystem::path& outputPath, std::string& error);

	/// <summary>
	/// このビューを撮影待ちか。タブが隠れていて描画を省くビューでも、撮影待ちなら描かせるために使う。
	/// </summary>
	bool WantsView(uint32_t viewIndex) const;

	/// <summary>PostProcess::Render(viewIndex, ...) の直後に呼ぶ。撮影待ちのビューならコピー命令を積む。</summary>
	void RecordViewCopy(uint32_t viewIndex);

	/// <summary>ImGuiを描き終えた後、PostDrawの前に呼ぶ。エディタ全体の撮影待ちならコピー命令を積む。</summary>
	void RecordEditorCopy();

	/// <summary>
	/// 撮影が終わっていれば結果(ok/保存先/大きさ)を返し、予約を解く。まだならstd::nullopt。
	/// 予約がないときに呼ばないこと。
	/// </summary>
	std::optional<nlohmann::json> Poll(std::string& error);

	static const char* ToName(Target target);

private:
	EditorScreenshot() = default;

	void RecordCopy(ID3D12Resource* source, D3D12_RESOURCE_STATES stateBefore);
	bool SavePng(std::string& error);
	void Reset();

	bool pending_ = false;
	bool recorded_ = false;
	// 予約してから何フレーム経ったか。対象が描かれないまま一定フレーム経ったら失敗にする。
	int waitedFrames_ = 0;
	Target target_ = Target::SceneView;
	std::filesystem::path outputPath_;

	Microsoft::WRL::ComPtr<ID3D12Resource> readbackBuffer_;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint_{};
	uint64_t readbackSize_ = 0;
};

} // namespace KujataEngine
