#pragma once

#include "../math/Matrix4x4.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include "../runtime/KujataApi.h"
#include "../shapes/ShapeUtil.h"
#include <cstdint>
#include <d3d12.h>
#include <vector>
#include <wrl.h>

namespace KujataEngine {

class Camera;

/// <summary>
/// デバッグやゲーム用の一時Line描画を管理する。
/// DrawLineで登録した線はRender後にクリアされる。
/// </summary>
class KUJATA_API LineRenderer {
public:
	static LineRenderer* GetInstance();

	/// <summary>
	/// 始点と終点からLineを登録します。
	/// </summary>
	static void DrawLine(const Vector3& start, const Vector3& end, const Vector4& color);

	/// <summary>
	/// SegmentからLineを登録します。
	/// </summary>
	static void DrawLine(const Segment& segment, const Vector4& color);

	/// <summary>
	/// 登録済みLineをCameraで描画し、描画後にクリアします。
	/// </summary>
	void Render(const Camera& camera);

	/// <summary>
	/// 登録済みLineを破棄します。
	/// </summary>
	void Clear();

private:
	LineRenderer() = default;
	~LineRenderer() = default;
	LineRenderer(const LineRenderer&) = delete;
	LineRenderer& operator=(const LineRenderer&) = delete;

	struct LineVertex {
		Vector3 position;
		Vector4 color;
	};

	// ビュー(Scene/Game)ごとのGPUバッファ。DirectXCommon::kRenderViewCount と同じ数。
	// 同じフレームで両方のビューに描くので、1つを共有すると後のビューの中身(そのカメラのWVP・線)で上書きされ、
	// 先に積んだビューもそれで描かれてずれる。
	static constexpr uint32_t kViewCount = 2;
	struct ViewBuffers {
		uint32_t vertexCapacity = 0;
		LineVertex* vertexMap = nullptr;
		Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
		D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
		Matrix4x4* wvpMap = nullptr;
		Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource;
	};

	void EnsureVertexCapacity(ViewBuffers& buffers, uint32_t vertexCount);

	void EnsureConstantBuffer(ViewBuffers& buffers);

	std::vector<LineVertex> vertices_;
	ViewBuffers views_[kViewCount];
};

/// <summary>
/// 始点と終点からLineを登録します。
/// </summary>
KUJATA_API void DrawLine(const Vector3& start, const Vector3& end, const Vector4& color);

/// <summary>
/// SegmentからLineを登録します。
/// </summary>
KUJATA_API void DrawLine(const Segment& segment, const Vector4& color);

} // namespace KujataEngine
