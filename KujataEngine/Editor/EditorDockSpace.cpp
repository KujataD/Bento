#include "EditorDockSpace.h"

#include "../../externals/imgui/imgui.h"
#include "../../externals/imgui/imgui_internal.h"

namespace KujataEngine {

void EditorDockSpace::Draw(const std::function<void()>& drawMenuBarContent, const std::function<void()>& drawToolbarContent) {
#ifdef USE_IMGUI
	// MainViewport全体を覆う透明な親ウィンドウを作り、その中にDockSpaceを置く。
	// これによりGame/Hierarchy/Inspector/ConsoleをUnity風に分割配置できる。
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);
	ImGui::SetNextWindowViewport(viewport->ID);

	// DockSpace用の親ウィンドウは移動・リサイズ・タイトルバー表示をさせず、画面全体の土台として使う。
	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
	                               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

	// 親ウィンドウの余白や角丸を消して、アプリ全体がエディタ領域になるようにする。
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("KujataEditorDockSpace", nullptr, windowFlags);
	ImGui::PopStyleVar(3);

	if (ImGui::BeginMenuBar()) {
		// メニュー内容(File/Edit/GameObject/Window)はImGuiManager側から注入する。
		if (drawMenuBarContent) {
			drawMenuBarContent();
		}
		ImGui::EndMenuBar();
	}

	// メニューバーの下に、再生・編集コントロール用の独立したツールバー帯を作る。
	if (drawToolbarContent) {
		ImGuiStyle& style = ImGui::GetStyle();
		const float toolbarHeight = ImGui::GetFrameHeight() + style.FramePadding.y * 2.0f;
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
		ImGui::BeginChild("##KujataToolbar", ImVec2(0.0f, toolbarHeight), ImGuiChildFlags_None,
		    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleColor();
		// 左端に少し余白を入れてから中身を描く。
		ImGui::Dummy(ImVec2(style.ItemSpacing.x, 0.0f));
		ImGui::SameLine();
		drawToolbarContent();
		ImGui::EndChild();
	}

	// DockSpaceのIDは毎フレーム同じ値にする必要がある。IDが変わるとドッキング状態を維持できない。
	ImGuiID dockspaceId = ImGui::GetID("KujataEditorMainDockSpace");
	ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;
	ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);

	// DockBuilderによる初期配置は初回だけ。以降はユーザーがドラッグした配置を尊重する。
	if (!dockLayoutInitialized_) {
		SetupInitialLayout(dockspaceId);
	}

	ImGui::End();
#else
	(void)drawMenuBarContent;
	(void)drawToolbarContent;
#endif // USE_IMGUI
}

void EditorDockSpace::SetupInitialLayout(unsigned int dockspaceId) {
#ifdef USE_IMGUI
	ImGuiViewport* viewport = ImGui::GetMainViewport();

	// 既存ノードを一度消してから、Unityの2 by 3 Layoutに近い初期Dock構成を作る。
	ImGui::DockBuilderRemoveNode(dockspaceId);
	ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodePos(dockspaceId, viewport->WorkPos);
	ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

	// gameNodeを少しずつ分割して、Game / Hierarchy / Project / Inspector / Consoleを作る。
	ImGuiID hierarchyNode = 0;
	ImGuiID projectNode = 0;
	ImGuiID inspectorNode = 0;
	ImGuiID consoleNode = 0;
	ImGuiID gameNode = dockspaceId;

	// 右側にInspector、Project、Hierarchyの順で列を作る。
	// 画面上ではGame | Hierarchy | Project | Inspectorとなり、ProjectがHierarchyとInspectorの間に入る。
	ImGui::DockBuilderSplitNode(gameNode, ImGuiDir_Right, 0.25f, &inspectorNode, &gameNode);
	ImGui::DockBuilderSplitNode(gameNode, ImGuiDir_Right, 0.20f, &projectNode, &gameNode);
	ImGui::DockBuilderSplitNode(gameNode, ImGuiDir_Right, 0.20f, &hierarchyNode, &gameNode);
	ImGui::DockBuilderSplitNode(gameNode, ImGuiDir_Down, 0.28f, &consoleNode, &gameNode);

	// ウィンドウ名とDock先を紐づける。名前は各ImGui::Beginの文字列と一致させる。
	ImGui::DockBuilderDockWindow("Inspector", inspectorNode);
	ImGui::DockBuilderDockWindow("Project", projectNode);
	ImGui::DockBuilderDockWindow("Hierarchy", hierarchyNode);
	ImGui::DockBuilderDockWindow("Console", consoleNode);
	ImGui::DockBuilderDockWindow("Performance", consoleNode);
	// SceneとGameは中央ノードにタブとして重ねる(Unity同様)。ユーザーがドラッグで分割可能。
	ImGui::DockBuilderDockWindow("Scene", gameNode);
	ImGui::DockBuilderDockWindow("Game", gameNode);
	ImGui::DockBuilderFinish(dockspaceId);

	// ここをtrueにして、次フレーム以降は初期配置を作り直さない。
	dockLayoutInitialized_ = true;
#endif // USE_IMGUI
}

} // namespace KujataEngine
