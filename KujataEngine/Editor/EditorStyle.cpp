#include "EditorStyle.h"
#include "../../externals/imgui/imgui.h"

namespace KujataEngine {

void EditorStyle::Apply() {
	ImGuiStyle& style = ImGui::GetStyle(); // 現在のImGuiスタイル設定を参照で取得する
	style = ImGuiStyle();                  // スタイル設定をデフォルト状態にリセットする

	style.WindowMinSize = ImVec2(160.0f, 20.0f); // ウィンドウの最小サイズ
	style.FramePadding = ImVec2(4.0f, 2.0f);     // ボタンや入力欄などの内側余白
	style.ItemSpacing = ImVec2(6.0f, 2.0f);      // UI要素同士の間隔
	style.ItemInnerSpacing = ImVec2(2.0f, 4.0f); // 複合UI要素内のパーツ同士の間隔
	style.Alpha = 1.00f;                         // UI全体の透明度(メニュー等を完全不透明にするため1.0)
	style.WindowRounding = 4.0f;                 // ウィンドウ角の丸み
	style.FrameRounding = 2.0f;                  // ボタンや入力欄などの角の丸み
	style.ChildRounding = 5.0f;                  // 子ウィンドウ角の丸み
	style.PopupRounding = 5.0f;                  // ポップアップウィンドウ角の丸み
	style.IndentSpacing = 6.0f;                  // ツリーなどでインデントする幅
	style.ColumnsMinSpacing = 50.0f;             // カラム同士の最小間隔
	style.GrabMinSize = 14.0f;                   // スライダーやスクロールバーのつまみの最小サイズ
	style.GrabRounding = 16.0f;                  // スライダーやスクロールバーのつまみの角の丸み
	style.ScrollbarSize = 12.0f;                 // スクロールバーの太さ
	style.ScrollbarRounding = 16.0f;             // スクロールバー角の丸み
	style.TabRounding = 5.0f;                    // タブ角の丸み

	ImVec4* colors = style.Colors;

	const ImVec4 textColor = ImVec4(0.86f, 0.93f, 0.89f, 0.78f);
	const ImVec4 textDisabledColor = ImVec4(0.86f, 0.93f, 0.89f, 0.28f);
	const ImVec4 mainBgColor = ImVec4(0.02f, 0.02f, 0.02f, 1.00f);
	const ImVec4 mainBgTransparentColor = ImVec4(0.02f, 0.02f, 0.02f, 0.73f);
	const ImVec4 popupBgColor = ImVec4(0.02f, 0.02f, 0.02f, 1.00f);   // メニュー/ポップアップ背景は完全不透明で視認性を確保
	const ImVec4 titleCollapsedColor = ImVec4(0.02f, 0.02f, 0.02f, 0.75f);
	const ImVec4 menuBarBgColor = ImVec4(0.02f, 0.02f, 0.02f, 1.00f); // メニューバーも完全不透明

	const ImVec4 frameBgColor = ImVec4(0.110f, 0.110f, 0.110f, 1.00f);
	const ImVec4 tabBgColor = ImVec4(0.110f, 0.110f, 0.110f, 0.92f);
	const ImVec4 tableRowAltColor = ImVec4(0.110f, 0.110f, 0.110f, 0.25f);

	const ImVec4 accentColor = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
	const ImVec4 accentHoveredColor = ImVec4(0.92f, 0.18f, 0.29f, 0.78f);
	const ImVec4 accentHoveredStrong = ImVec4(0.92f, 0.18f, 0.29f, 0.86f);
	const ImVec4 accentHeaderColor = ImVec4(0.92f, 0.18f, 0.29f, 0.76f);
	const ImVec4 accentPreviewColor = ImVec4(0.92f, 0.18f, 0.29f, 0.50f);
	const ImVec4 accentSelectedBgColor = ImVec4(0.92f, 0.18f, 0.29f, 0.43f);
	const ImVec4 accentDropTargetColor = ImVec4(0.92f, 0.18f, 0.29f, 0.90f);

	const ImVec4 cyanColor = ImVec4(0.27f, 0.9f, 1.0f, 0.78f);
	const ImVec4 cyanWeakColor = ImVec4(0.27f, 0.9f, 1.0f, 0.14f);
	const ImVec4 cyanVeryWeakColor = ImVec4(0.27f, 0.9f, 1.0f, 0.04f);
	const ImVec4 cyanLineColor = ImVec4(0.27f, 0.9f, 1.0f, 0.80f);
	const ImVec4 cyanLineDimmedColor = ImVec4(0.27f, 0.9f, 1.0f, 0.40f);

	const ImVec4 borderColor = ImVec4(0.31f, 0.31f, 1.00f, 0.00f);
	const ImVec4 borderShadowColor = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	const ImVec4 scrollbarGrabColor = ImVec4(0.09f, 0.15f, 0.16f, 1.00f);
	const ImVec4 checkMarkColor = ImVec4(0.71f, 0.22f, 0.27f, 1.00f);
	const ImVec4 separatorColor = ImVec4(0.14f, 0.16f, 0.19f, 1.00f);
	const ImVec4 separatorLightColor = ImVec4(0.14f, 0.16f, 0.19f, 0.75f);
	const ImVec4 transparentRowColor = ImVec4(0.02f, 0.02f, 0.02f, 0.00f);
	const ImVec4 textHighlightColor = ImVec4(0.86f, 0.93f, 0.89f, 0.70f);
	const ImVec4 plotColor = ImVec4(0.86f, 0.93f, 0.89f, 0.63f);

	colors[ImGuiCol_Text] = textColor;                                // 通常テキストの色
	colors[ImGuiCol_TextDisabled] = textDisabledColor;                // 無効状態のテキスト色
	colors[ImGuiCol_WindowBg] = mainBgColor;                          // 通常ウィンドウ背景色
	colors[ImGuiCol_ChildBg] = mainBgColor;                           // 子ウィンドウ背景色
	colors[ImGuiCol_PopupBg] = popupBgColor;                          // ポップアップ背景色
	colors[ImGuiCol_Border] = borderColor;                            // 枠線の色
	colors[ImGuiCol_BorderShadow] = borderShadowColor;                // 枠線の影色
	colors[ImGuiCol_FrameBg] = frameBgColor;                          // 入力欄やチェックボックスなどの背景色
	colors[ImGuiCol_FrameBgHovered] = accentHoveredColor;             // 入力欄などにマウスを重ねた時の背景色
	colors[ImGuiCol_FrameBgActive] = accentColor;                     // 入力欄などを操作中の背景色
	colors[ImGuiCol_TitleBg] = mainBgColor;                           // 非アクティブなタイトルバー背景色
	colors[ImGuiCol_TitleBgActive] = accentColor;                     // アクティブなタイトルバー背景色
	colors[ImGuiCol_TitleBgCollapsed] = titleCollapsedColor;          // 折りたたみ状態のタイトルバー背景色
	colors[ImGuiCol_MenuBarBg] = menuBarBgColor;                      // メニューバー背景色
	colors[ImGuiCol_ScrollbarBg] = mainBgColor;                       // スクロールバー背景色
	colors[ImGuiCol_ScrollbarGrab] = scrollbarGrabColor;              // スクロールバーのつまみ色
	colors[ImGuiCol_ScrollbarGrabHovered] = accentHoveredColor;       // スクロールバーつまみにマウスを重ねた時の色
	colors[ImGuiCol_ScrollbarGrabActive] = accentColor;               // スクロールバーつまみを操作中の色
	colors[ImGuiCol_CheckMark] = checkMarkColor;                      // チェックマークの色
	colors[ImGuiCol_SliderGrab] = cyanWeakColor;                      // スライダーつまみの色
	colors[ImGuiCol_SliderGrabActive] = accentColor;                  // スライダーつまみを操作中の色
	colors[ImGuiCol_Button] = frameBgColor;                           // 通常ボタンの色
	colors[ImGuiCol_ButtonHovered] = accentHoveredStrong;             // ボタンにマウスを重ねた時の色
	colors[ImGuiCol_ButtonActive] = accentColor;                      // ボタンを押している時の色
	colors[ImGuiCol_Header] = accentHeaderColor;                      // ヘッダーや選択項目の通常色
	colors[ImGuiCol_HeaderHovered] = accentHoveredStrong;             // ヘッダーや選択項目にマウスを重ねた時の色
	colors[ImGuiCol_HeaderActive] = accentColor;                      // ヘッダーや選択項目を操作中の色
	colors[ImGuiCol_Separator] = separatorColor;                      // 区切り線の通常色
	colors[ImGuiCol_SeparatorHovered] = accentHoveredColor;           // 区切り線にマウスを重ねた時の色
	colors[ImGuiCol_SeparatorActive] = accentColor;                   // 区切り線を操作中の色
	colors[ImGuiCol_ResizeGrip] = cyanVeryWeakColor;                  // ウィンドウリサイズつまみの通常色
	colors[ImGuiCol_ResizeGripHovered] = accentHoveredColor;          // リサイズつまみにマウスを重ねた時の色
	colors[ImGuiCol_ResizeGripActive] = accentColor;                  // リサイズつまみを操作中の色
	colors[ImGuiCol_Tab] = tabBgColor;                                // 非選択タブの色
	colors[ImGuiCol_TabHovered] = accentHoveredStrong;                // タブにマウスを重ねた時の色
	colors[ImGuiCol_TabSelected] = accentColor;                       // 選択中タブの色
	colors[ImGuiCol_TabSelectedOverline] = cyanLineColor;             // 選択中タブ上部ラインの色
	colors[ImGuiCol_TabDimmed] = popupBgColor;                        // 暗く表示された非選択タブの色
	colors[ImGuiCol_TabDimmedSelected] = frameBgColor;                // 暗く表示された選択中タブの色
	colors[ImGuiCol_TabDimmedSelectedOverline] = cyanLineDimmedColor; // 暗く表示された選択中タブ上部ラインの色
	colors[ImGuiCol_DockingPreview] = accentPreviewColor;             // ドッキング先プレビューの色
	colors[ImGuiCol_DockingEmptyBg] = mainBgColor;                    // ドッキング領域の空背景色
	colors[ImGuiCol_PlotLines] = plotColor;                           // 折れ線グラフの線色
	colors[ImGuiCol_PlotLinesHovered] = accentColor;                  // 折れ線グラフにマウスを重ねた時の色
	colors[ImGuiCol_PlotHistogram] = plotColor;                       // ヒストグラムの色
	colors[ImGuiCol_PlotHistogramHovered] = accentColor;              // ヒストグラムにマウスを重ねた時の色
	colors[ImGuiCol_TableHeaderBg] = frameBgColor;                    // テーブルヘッダー背景色
	colors[ImGuiCol_TableBorderStrong] = separatorColor;              // テーブルの強い境界線色
	colors[ImGuiCol_TableBorderLight] = separatorLightColor;          // テーブルの弱い境界線色
	colors[ImGuiCol_TableRowBg] = transparentRowColor;                // テーブル行の通常背景色
	colors[ImGuiCol_TableRowBgAlt] = tableRowAltColor;                // テーブル交互行の背景色
	colors[ImGuiCol_TextSelectedBg] = accentSelectedBgColor;          // テキスト選択時の背景色
	colors[ImGuiCol_DragDropTarget] = accentDropTargetColor;          // ドラッグ＆ドロップ先の強調色
	colors[ImGuiCol_NavCursor] = cyanColor;                           // キーボード・ゲームパッド操作時のカーソル色
	colors[ImGuiCol_NavWindowingHighlight] = textHighlightColor;      // ナビゲーション時のウィンドウ強調色
	colors[ImGuiCol_NavWindowingDimBg] = mainBgTransparentColor;      // ナビゲーション時の背景暗転色
	colors[ImGuiCol_ModalWindowDimBg] = mainBgTransparentColor;       // モーダルウィンドウ表示時の背景暗転色
}

} // namespace KujataEngine
