#include "GameViewWindow.h"

#include "../../externals/imgui/imgui.h"
#include "../base/DirectXCommon.h"
#include "../postprocess/PostProcess.h"
#include "../runtime/PlayState.h"
#include "../runtime/UIInput.h"
#include <d3d12.h>

namespace KujataEngine {

void GameViewWindow::Draw(bool* pOpen) {
#ifdef USE_IMGUI
	// Begin の戻り値で可視性(タブ非アクティブ/折り畳み時はfalse)を判定し、非表示ならGameパスをスキップさせる。
	bool visible = ImGui::Begin("Game", pOpen);
	SetGameViewVisible(visible);
	if (!visible) {
		ImGui::End();
		return;
	}

	// Dock内側の描画可能領域(タブや枠を除いた分)。
	ImVec2 contentSize = ImGui::GetContentRegionAvail();
	if (contentSize.x < 1.0f) {
		contentSize.x = 1.0f;
	}
	if (contentSize.y < 1.0f) {
		contentSize.y = 1.0f;
	}

	// メインカメラで描いたGameビューの、ポストプロセス(ブルーム/トーンマップ)適用済みSRVをImGuiへ渡す。
	// 描画本体はEditorApplicationのBeginGameRender〜EndGameRender+PostProcess::Renderで行われる。
	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	D3D12_GPU_DESCRIPTOR_HANDLE handle = PostProcess::GetInstance()->GetDisplaySrvHandle(DirectXCommon::kGameViewIndex);

	// Game用RenderTextureは固定解像度。アスペクト比を保ってレターボックス表示する。
	float gameAspect = 16.0f / 9.0f;
	if (dxCommon->GetGameRenderHeight() > 0) {
		gameAspect = static_cast<float>(dxCommon->GetGameRenderWidth()) / static_cast<float>(dxCommon->GetGameRenderHeight());
	}

	ImVec2 imageSize = contentSize;
	ImVec2 imageOffset = {0.0f, 0.0f};
	float contentAspect = contentSize.x / contentSize.y;
	if (contentAspect > gameAspect) {
		imageSize.y = contentSize.y;
		imageSize.x = imageSize.y * gameAspect;
		imageOffset.x = (contentSize.x - imageSize.x) * 0.5f;
	} else {
		imageSize.x = contentSize.x;
		imageSize.y = imageSize.x / gameAspect;
		imageOffset.y = (contentSize.y - imageSize.y) * 0.5f;
	}

	ImVec2 contentPosition = ImGui::GetCursorScreenPos();
	ImVec2 contentEnd = {contentPosition.x + contentSize.x, contentPosition.y + contentSize.y};
	ImVec2 imagePosition = {contentPosition.x + imageOffset.x, contentPosition.y + imageOffset.y};
	ImVec2 imageEnd = {imagePosition.x + imageSize.x, imagePosition.y + imageSize.y};

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddRectFilled(contentPosition, contentEnd, IM_COL32(0, 0, 0, 255));
	drawList->AddImage(static_cast<ImTextureID>(handle.ptr), imagePosition, imageEnd, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
	ImGui::Dummy(contentSize);

	// マウス位置をGame RTピクセル空間へ変換してUIのポインタ入力へ渡す(UIボタンの当たり判定用)。
	{
		UIPointerState pointer;
		const ImVec2 mousePos = ImGui::GetMousePos();
		const float localX = mousePos.x - imagePosition.x;
		const float localY = mousePos.y - imagePosition.y;
		const bool inside = ImGui::IsWindowHovered() && localX >= 0.0f && localY >= 0.0f && localX <= imageSize.x && localY <= imageSize.y;
		const float rtWidth = static_cast<float>(dxCommon->GetGameRenderWidth());
		const float rtHeight = static_cast<float>(dxCommon->GetGameRenderHeight());
		pointer.x = imageSize.x > 0.0f ? (localX / imageSize.x) * rtWidth : 0.0f;
		pointer.y = imageSize.y > 0.0f ? (localY / imageSize.y) * rtHeight : 0.0f;
		pointer.inside = inside;
		pointer.held = ImGui::IsMouseDown(ImGuiMouseButton_Left);
		pointer.pressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
		pointer.released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
		SetUIPointer(pointer);
	}

	ImGui::End();
#else
	(void)pOpen;
#endif // USE_IMGUI
}

} // namespace KujataEngine
