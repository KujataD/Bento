#pragma once

#include <string>

namespace KujataEngine {

// ProjectWindowからドロップされたMaterial Assetを受け取れるComponentが実装するインターフェース。
// 実装するComponentだけが継承し、呼び出し側はdynamic_castで能力を問い合わせる。
class IMaterialTarget {
public:
	virtual ~IMaterialTarget() = default;

	// ドロップされたMaterial Assetを適用する。適用できたらtrue。
	virtual bool ApplyMaterialAsset(const std::string& materialPath) = 0;

	// 指定Material Assetを参照しているかどうか。
	virtual bool UsesMaterialAsset(const std::string& materialPath) const = 0;
};

} // namespace KujataEngine
