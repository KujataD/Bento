#pragma once

#include "../math/Vector2.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include "../runtime/KujataApi.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 26495)
#pragma warning(disable : 26819)
#endif
#include "../../externals/nlohmann/json.hpp"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace KujataEngine {

/// <summary>
/// Materialが持つTexture用途。
/// </summary>
enum class MaterialTextureSlot {
	BaseColor,
	Normal,
	Environment,
	// 自己発光の分布(エミッションマップ)。emissiveColor×emissiveIntensityへ乗算されるので、
	// 黒い箇所は光らず白い箇所だけが光る。面の一部だけを光らせたい時に使う。
	Emissive,
};

/// <summary>
/// Material内のTexture参照情報。
/// </summary>
struct MaterialTexture {
	MaterialTextureSlot slot = MaterialTextureSlot::BaseColor;
	std::string assetId;
	std::string path;
};

/// <summary>
/// Material Assetが保持する描画パラメータ。
/// </summary>
struct MaterialAssetData {
	std::string name = "New Material";
	Vector4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
	// シェーダー方式(ShaderModel enum値)。0=None(Unlit/ライティングなし)..4=Blinn-Phong。
	// 既定はBlinn-Phong(4)。UI等でライティングを無効にしたい場合は0を選ぶ。
	int shaderModel = 4;
	// 合成方法(KujataEngine::BlendMode の値)。既定は1=kNormal(通常のαブレンド)。
	// 2=kAdd(加算)にすると光・炎・魔法のように「重ねるほど明るくなる」表現になる。
	int blendMode = 1;
	// 深度バッファへ書き込むか。**半透明/加算はOFFにする** —
	// ONのままだと後から描かれる背後の物を隠し、自分の裏面とも喧嘩する(深度テスト自体は常に行う)。
	bool depthWrite = true;
	// エミッション(自己発光)。emissiveEnabledがtrueのマテリアルだけ、
	// ライティングと無関係に emissiveColor × emissiveIntensity を加算する(Unityの Emission チェックと同じ)。
	// 既定はOFFで既存アセットと互換。強度>1でHDR輝度になりブルームが乗る。
	bool emissiveEnabled = false;
	Vector3 emissiveColor = {1.0f, 1.0f, 1.0f};
	float emissiveIntensity = 1.0f;
	// 露出光(ブルーム)のマテリアル別設定。エミッションの滲み方だけを制御する。
	float bloomIntensity = 1.0f; // 滲みの強さ(0で発光はするが滲まない)
	float bloomThreshold = 0.0f; // この輝度以上のエミッションだけが滲む(0=全て滲む)
	float bloomSoftKnee = 0.5f;  // 閾値の柔らかさ(0=ハード)
	// UVトランスフォーム(タイリング・スクロール・回転)。既定は変換なしで既存アセットと互換。
	// 適用順はSprite/UIと同じ Scale → RotateZ → Translate。
	Vector2 uvOffset = {0.0f, 0.0f};
	Vector2 uvScale = {1.0f, 1.0f};
	float uvRotation = 0.0f; // ラジアン
	std::vector<MaterialTexture> textures;
};

/// <summary>
/// Unity風のProject管理Materialを読み書きするUtility。
/// </summary>
class MaterialAsset {
public:
	static KUJATA_API MaterialAssetData CreateDefault();

	static KUJATA_API bool IsMaterialFile(const std::filesystem::path& path);

	static KUJATA_API std::string GetDisplayName(const std::filesystem::path& path);

	/// <summary>
	/// ファイル名として使えるMaterial名へ整形します。
	/// </summary>
	static KUJATA_API std::string SanitizeName(const std::string& name);

	/// <summary>
	/// Material名を変更し、Material JSONと.metaも合わせて更新します。
	/// </summary>
	static KUJATA_API bool Rename(const std::filesystem::path& path, const std::string& newName, std::filesystem::path& outNewPath, std::string& message);

	/// <summary>
	/// TextureSlotをJSON保存用の文字列へ変換します。
	/// </summary>
	static KUJATA_API const char* ToString(MaterialTextureSlot slot);

	static KUJATA_API bool TryGetTextureSlotFromString(const std::string& slotName, MaterialTextureSlot& outSlot);

	/// <summary>
	/// 指定SlotのTexture参照を取得します。存在しない場合はnullptrを返します。
	/// </summary>
	static KUJATA_API const MaterialTexture* GetTexture(const MaterialAssetData& material, MaterialTextureSlot slot);

	static KUJATA_API std::string GetTexturePath(const MaterialAssetData& material, MaterialTextureSlot slot);

	static KUJATA_API void SetTexture(MaterialAssetData& material, MaterialTextureSlot slot, const std::string& assetId, const std::string& path);

	static KUJATA_API bool Load(const std::filesystem::path& path, MaterialAssetData& outMaterial, std::string& message);

	static KUJATA_API bool Save(const std::filesystem::path& path, const MaterialAssetData& material, std::string& message);

	static KUJATA_API bool CreateDefaultFile(const std::filesystem::path& path, std::string& message);

	static KUJATA_API MaterialAssetData ReadJsonObject(const nlohmann::json& json, const MaterialAssetData& defaultMaterial);

	static KUJATA_API void WriteJsonObject(nlohmann::json& json, const MaterialAssetData& material);

	static KUJATA_API std::filesystem::path ResolveTexturePath(const MaterialAssetData& material, MaterialTextureSlot slot = MaterialTextureSlot::BaseColor);

	/// <summary>
	/// Materialが参照するTextureをTextureManagerへ読み込み、SRVインデックスを返します。
	/// </summary>
	static KUJATA_API uint32_t ResolveTextureIndex(const MaterialAssetData& material, MaterialTextureSlot slot = MaterialTextureSlot::BaseColor);
};

} // namespace KujataEngine
