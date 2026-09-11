#pragma once

#include "../3d/GraphicsPipeline.h"
#include "../math/Matrix4x4.h"
#include "../runtime/KujataApi.h"

namespace KujataEngine {

/// <summary>
/// UI描画の共通セットアップ。
/// Begin系で対象RTの解像度に合わせたビューポート/シザー/トポロジ/ヒープを積み、
/// 「UI頂点座標 → クリップ空間」の変換行列と使用パイプラインを用意する。
/// UIQuad::Draw()はこの2つを参照して描画する。
///
///   - Begin      : Screen Space - Overlay。頂点はピクセル座標、行列はオルソ、深度OFF(kUI)。
///   - BeginWorld : World Space。頂点はキャンバス単位、行列は Canvas配置 x カメラVP、
///                  深度テストあり/書き込みなし(kSprite2D)で3Dに遮蔽される。
/// </summary>
class UIRenderer {
public:
	/// <summary>
	/// スクリーン空間UIの描画開始。targetWidth/Heightは描画先RT(Scene/Game)のピクセルサイズ。
	/// </summary>
	static KUJATA_API void Begin(float targetWidth, float targetHeight);

	/// <summary>
	/// world空間UIの描画開始。canvasToClipは「キャンバス単位の頂点 → クリップ空間」の合成行列。
	/// </summary>
	static KUJATA_API void BeginWorld(const Matrix4x4& canvasToClip, float targetWidth, float targetHeight);

	static KUJATA_API void End();

	/// <summary>現在のUI変換行列(UI頂点座標→クリップ空間)。</summary>
	static KUJATA_API const Matrix4x4& GetTransform();

	/// <summary>現在のUI描画に使うパイプライン(kUI または kSprite2D)。</summary>
	static KUJATA_API PipelineType GetPipelineType();

	static KUJATA_API float GetTargetWidth();
	static KUJATA_API float GetTargetHeight();
};

} // namespace KujataEngine
