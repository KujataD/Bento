#pragma once

namespace KujataEngine {

// Edit中のScene表示で、実体を持たないComponentの位置をアイコン(ビルボード)で示す能力。
// このインターフェースを実装するComponentがビルボード対象となる。
// 呼び出し側はdynamic_castで能力を問い合わせる。
class IEditorBillboard {
public:
	virtual ~IEditorBillboard() = default;

	// Editor用ビルボードに使う画像ファイル名。KujataEngine/resources/images から読み込む。
	virtual const char* GetEditorBillboardIconName() const = 0;

	// Editor用ビルボードをRayCastで選択するためのワールド半径。
	virtual float GetEditorBillboardPickRadius() const { return 0.5f; }
};

} // namespace KujataEngine
