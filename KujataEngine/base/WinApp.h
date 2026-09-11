#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>

namespace KujataEngine {

/// <summary>
/// ウィンドウ管理クラス
/// </summary>
class WinApp {
public: // 静的メンバ変数
	// クライアント領域のサイズ
	static const int32_t kWindowWidth = 1280;
	static const int32_t kWindowHeight = 720;

	// ウィンドウクラス名
	static const wchar_t kWindowClassName[];

public: // 静的メンバ関数
	/// <summary>
	/// シングルトンインスタンスの取得
	/// </summary>
	static WinApp* GetInstance();

	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

public: // メンバ関数
	/// <param name="title">ウィンドウタイトル</param>
	/// <param name="clientWidth">クライアント領域の幅</param>
	/// <param name="clientHeight">クライアント領域の高さ</param>
	void CreateGameWindow(const std::wstring& title = L"KujataEngine", int32_t clientWidth = kWindowWidth, int32_t clientHeight = kWindowHeight);

	void TerminateGameWindow();

	/// <returns>trueで終了リクエスト</returns>
	bool ProcessMessage();

	// ゲッター
	HWND GetHwnd() const { return hwnd_; }
	HINSTANCE GetHInstance() const { return wndClass_.hInstance; }

private:
	WinApp() = default;
	~WinApp() = default;
	WinApp(const WinApp&) = delete;
	const WinApp& operator=(const WinApp&) = delete;

private: // メンバ変数
	HWND hwnd_ = nullptr;
	WNDCLASSEX wndClass_ = {};
};

} // namespace KujataEngine