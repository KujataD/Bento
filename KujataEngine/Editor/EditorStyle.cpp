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

	// 配色はセージグリーン1色で統一する。
	// 各色はOKLCHの明るさ(L)を元の配色(赤アクセント+黒)から変えずに、色相をセージ(h=128°、#9CAF88と同じ)へ揃えたもの。
	// 彩度はアクセントでも0.06〜0.085に抑え、背景・区切り線はほぼ無彩色に近い薄い緑にしている。
	// ただしアクセントだけは、上に載る文字(ヘッダー・選択タブ等)が読めるよう暗くしている(画面上で #4F6236 前後)。
	// 描画先がsRGBフォーマットなので、ここに書く値はリニア値として扱われ、画面では明るく表示される。
	// 色を調整するときは画面上の色で考え、sRGB→リニア変換した値を書くこと(文字とのコントラスト比は約5)。

	const ImVec4 textColor = ImVec4(0.891f, 0.924f, 0.855f, 0.78f);
	const ImVec4 textDisabledColor = ImVec4(0.891f, 0.924f, 0.855f, 0.28f);
	const ImVec4 mainBgColor = ImVec4(0.018f, 0.022f, 0.014f, 1.00f);
	const ImVec4 mainBgTransparentColor = ImVec4(0.018f, 0.022f, 0.014f, 0.73f);
	const ImVec4 popupBgColor = ImVec4(0.018f, 0.022f, 0.014f, 1.00f);   // メニュー/ポップアップ背景は完全不透明で視認性を確保
	const ImVec4 titleCollapsedColor = ImVec4(0.018f, 0.022f, 0.014f, 0.75f);
	const ImVec4 menuBarBgColor = ImVec4(0.018f, 0.022f, 0.014f, 1.00f); // メニューバーも完全不透明

	const ImVec4 frameBgColor = ImVec4(0.103f, 0.115f, 0.088f, 1.00f);
	const ImVec4 tabBgColor = ImVec4(0.103f, 0.115f, 0.088f, 0.92f);
	const ImVec4 tableRowAltColor = ImVec4(0.103f, 0.115f, 0.088f, 0.25f);

	const ImVec4 accentColor = ImVec4(0.079f, 0.123f, 0.038f, 1.00f);
	const ImVec4 accentHoveredColor = ImVec4(0.079f, 0.123f, 0.038f, 0.78f);
	const ImVec4 accentHoveredStrong = ImVec4(0.079f, 0.123f, 0.038f, 0.86f);
	const ImVec4 accentHeaderColor = ImVec4(0.079f, 0.123f, 0.038f, 0.76f);
	const ImVec4 accentPreviewColor = ImVec4(0.079f, 0.123f, 0.038f, 0.50f);
	const ImVec4 accentSelectedBgColor = ImVec4(0.079f, 0.123f, 0.038f, 0.43f);
	const ImVec4 accentDropTargetColor = ImVec4(0.079f, 0.123f, 0.038f, 0.90f);

	const ImVec4 lightAccentColor = ImVec4(0.765f, 0.842f, 0.678f, 0.78f);
	const ImVec4 lightAccentWeakColor = ImVec4(0.765f, 0.842f, 0.678f, 0.14f);
	const ImVec4 lightAccentVeryWeakColor = ImVec4(0.765f, 0.842f, 0.678f, 0.04f);
	const ImVec4 lightAccentLineColor = ImVec4(0.765f, 0.842f, 0.678f, 0.80f);
	const ImVec4 lightAccentLineDimmedColor = ImVec4(0.765f, 0.842f, 0.678f, 0.40f);

	const ImVec4 borderColor = ImVec4(0.403f, 0.470f, 0.321f, 0.00f);
	const ImVec4 borderShadowColor = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	const ImVec4 scrollbarGrabColor = ImVec4(0.127f, 0.146f, 0.106f, 1.00f);
	const ImVec4 checkMarkColor = ImVec4(0.765f, 0.842f, 0.678f, 1.00f); // アクセント(ホバー時の枠)より暗いと見えなくなるため明るいセージ
	const ImVec4 separatorColor = ImVec4(0.151f, 0.164f, 0.135f, 1.00f);
	const ImVec4 separatorLightColor = ImVec4(0.151f, 0.164f, 0.135f, 0.75f);
	const ImVec4 transparentRowColor = ImVec4(0.018f, 0.022f, 0.014f, 0.00f);
	const ImVec4 textHighlightColor = ImVec4(0.891f, 0.924f, 0.855f, 0.70f);
	const ImVec4 plotColor = ImVec4(0.891f, 0.924f, 0.855f, 0.63f);

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
	colors[ImGuiCol_SliderGrab] = lightAccentWeakColor;                      // スライダーつまみの色
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
	colors[ImGuiCol_ResizeGrip] = lightAccentVeryWeakColor;                  // ウィンドウリサイズつまみの通常色
	colors[ImGuiCol_ResizeGripHovered] = accentHoveredColor;          // リサイズつまみにマウスを重ねた時の色
	colors[ImGuiCol_ResizeGripActive] = accentColor;                  // リサイズつまみを操作中の色
	colors[ImGuiCol_Tab] = tabBgColor;                                // 非選択タブの色
	colors[ImGuiCol_TabHovered] = accentHoveredStrong;                // タブにマウスを重ねた時の色
	colors[ImGuiCol_TabSelected] = accentColor;                       // 選択中タブの色
	colors[ImGuiCol_TabSelectedOverline] = lightAccentLineColor;             // 選択中タブ上部ラインの色
	colors[ImGuiCol_TabDimmed] = popupBgColor;                        // 暗く表示された非選択タブの色
	colors[ImGuiCol_TabDimmedSelected] = frameBgColor;                // 暗く表示された選択中タブの色
	colors[ImGuiCol_TabDimmedSelectedOverline] = lightAccentLineDimmedColor; // 暗く表示された選択中タブ上部ラインの色
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
	colors[ImGuiCol_NavCursor] = lightAccentColor;                           // キーボード・ゲームパッド操作時のカーソル色
	colors[ImGuiCol_NavWindowingHighlight] = textHighlightColor;      // ナビゲーション時のウィンドウ強調色
	colors[ImGuiCol_NavWindowingDimBg] = mainBgTransparentColor;      // ナビゲーション時の背景暗転色
	colors[ImGuiCol_ModalWindowDimBg] = mainBgTransparentColor;       // モーダルウィンドウ表示時の背景暗転色
}

} // namespace KujataEngine
