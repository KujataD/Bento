#include "WinApp.h"
#include "DirectXCommon.h"
#include "../../externals/imgui/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace KujataEngine {

// 静的メンバ変数の実体
const wchar_t WinApp::kWindowClassName[] = L"KujataEngineWindowClass";

WinApp* WinApp::GetInstance() {
	static WinApp instance;
	return &instance;
}

LRESULT CALLBACK WinApp::WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
#ifdef USE_IMGUI
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
		return true;
	}
#endif // USE_IMGUI
	
	// メッセージに応じてゲーム固有の処理を行う
	switch (msg) {
	case WM_SIZE:
		if (wparam != SIZE_MINIMIZED) {
			DirectXCommon* dxCommon = DirectXCommon::GetInstance();
			if (dxCommon->IsInitialized()) {
				int32_t width = static_cast<int32_t>(LOWORD(lparam));
				int32_t height = static_cast<int32_t>(HIWORD(lparam));
				dxCommon->ResizeBackBuffer(width, height);
			}
		}
		return 0;

		// ウィンドウが破棄された
	case WM_DESTROY:
		// OSに対して、アプリの終了を伝える
		PostQuitMessage(0);
		return 0;
	}
	// 標準のメッセージ処理を行う
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

void WinApp::CreateGameWindow(const std::wstring& title, int32_t clientWidth, int32_t clientHeight) {
	// 構造体のサイズ指定
	wndClass_.cbSize = sizeof(WNDCLASSEX);
	// ウィンドウプロシージャ
	wndClass_.lpfnWndProc = WindowProc;
	// ウィンドウクラス名
	wndClass_.lpszClassName = kWindowClassName;
	// インスタンスハンドル
	wndClass_.hInstance = GetModuleHandle(nullptr);
	// カーソル
	wndClass_.hCursor = LoadCursor(nullptr, IDC_ARROW);
	// ウィンドウクラスを登録する
	RegisterClassEx(&wndClass_);

	// クライアント領域のサイズからウィンドウサイズを計算
	RECT wrc = {0, 0, clientWidth, clientHeight};
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	// ウィンドウの生成
	hwnd_ = CreateWindow(
	    wndClass_.lpszClassName, // 利用するクラス名
	    title.c_str(),           // タイトルバーの文字
	    WS_OVERLAPPEDWINDOW,     // よく見るウィンドウスタイル
	    CW_USEDEFAULT,           // 表示X座標(windowsOSに任せる)
	    CW_USEDEFAULT,           // 表示Y座標(WindowsOSに任せる)
	    wrc.right - wrc.left,    // ウィンドウ横幅
	    wrc.bottom - wrc.top,    // ウィンドウ縦幅
	    nullptr,                 // 親ウィンドウハンドル
	    nullptr,                 // メニューハンドル
	    wndClass_.hInstance,     // インスタンスハンドル
	    nullptr);                // オプション

	// ウィンドウを表示する
	ShowWindow(hwnd_, SW_SHOW);
}

bool WinApp::ProcessMessage() {
	MSG msg{};
	// Windowにメッセージが来てたら最優先で処理させる
	if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return msg.message == WM_QUIT;
}

void WinApp::TerminateGameWindow() {
	CloseWindow(hwnd_);
	// ウィンドウクラスの削除/解放
	UnregisterClass(wndClass_.lpszClassName, wndClass_.hInstance);
	// COMの終了処理
	CoUninitialize();
}

} // namespace KujataEngine
