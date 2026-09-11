#pragma once

#include "../runtime/KujataApi.h"
#include "UIRect.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace KujataEngine {

/// <summary>
/// stb_truetypeでTTF/TTCフォントを**SDF(符号付き距離場)**アトラス化したもの。
/// グリフは**要求された文字(Unicodeコードポイント)ごとに動的にベイク**してアトラスへ追記する。
/// これによりASCIIだけでなく日本語等の任意の文字に対応する(使う文字だけ焼くので現実的なメモリで済む)。
/// SDFはスケール非依存なので、1枚のアトラスを全表示サイズで使い回せる(fontPathごとにキャッシュ)。
/// </summary>
class FontAtlas {
public:
	struct GlyphQuad {
		UIRect rect;                    // テキストローカル座標(左上原点・要求ピクセル)
		float u0 = 0.0f, v0 = 0.0f;     // UV左上
		float u1 = 0.0f, v1 = 0.0f;     // UV右下
	};

	/// <summary>指定フォントのSDFアトラスを取得(無ければ生成)。失敗時nullptr。</summary>
	static KUJATA_API FontAtlas* GetOrCreate(const std::string& fontPath);

	/// <summary>後方互換: サイズ付きでも呼べる(SDFではサイズはBuildTextで指定するため無視)。</summary>
	static KUJATA_API FontAtlas* GetOrCreate(const std::string& fontPath, float pixelHeight);

	/// <summary>
	/// UTF-8テキストに含まれる全文字のグリフをベイクし、必要ならGPUテクスチャを更新する。
	/// TextureManagerのGPU待ちを伴うため、必ず描画パス外(Prepare相当)で呼ぶこと。
	/// </summary>
	KUJATA_API void EnsureGlyphsForText(const std::string& utf8Text);

	/// <summary>UTF-8テキストを指定ピクセル高でグリフ矩形列にレイアウト(左揃え)。outTotalWidthに総横幅を返す。</summary>
	KUJATA_API std::vector<GlyphQuad> BuildText(const std::string& utf8Text, float pixelHeight, float& outTotalWidth) const;

	uint32_t GetTextureIndex() const { return textureIndex_; }
	float GetPixelHeight() const { return sdfBakePixelHeight_; }

private:
	struct Glyph {
		float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f; // アトラスUV
		float width = 0.0f, height = 0.0f;                // ベイクpx(パディング込みのSDFビットマップ寸法)
		float xoff = 0.0f, yoff = 0.0f;                   // ベイクpx(ペン原点=ベースラインからの左上オフセット)
		float xadvance = 0.0f;                            // ベイクpx
		bool valid = false;                               // 描画すべきグリフか(スペース/未収録はfalse)
	};

	KUJATA_API bool Initialize(const std::string& fontPath);
	// コードポイントのグリフを(未ベイクなら)ベイクしてアトラスへ追記する。ピクセルを書いたらtrueを返す。
	KUJATA_API bool BakeGlyph(uint32_t codepoint);
	// CPUアトラス(単色SDF)をRGBAへ展開してGPUへ再アップロードする。
	KUJATA_API void UploadAtlas();

	std::string fontPath_;
	std::vector<uint8_t> fontData_;   // stbtt_fontinfoが参照するので生存させ続ける
	std::vector<uint8_t> fontInfoStorage_; // stbtt_fontinfo実体(ヘッダ非公開のためbyte列で保持)
	bool fontReady_ = false;
	float scale_ = 1.0f;              // stbtt_ScaleForPixelHeight(bake)
	float ascent_ = 0.0f;             // ベイクpxのアセント(ベースライン位置)

	uint32_t textureIndex_ = 0;
	int atlasWidth_ = 2048;
	int atlasHeight_ = 2048;
	std::vector<uint8_t> atlasPixels_; // 単色SDF(atlasWidth_*atlasHeight_)
	int penX_ = 0;
	int penY_ = 0;
	int rowHeight_ = 0;
	bool atlasFull_ = false;

	std::unordered_map<uint32_t, Glyph> glyphs_; // codepoint -> glyph

	static constexpr float kSdfBakePixelHeightConst = 48.0f;
	float sdfBakePixelHeight_ = kSdfBakePixelHeightConst; // SDFベイク基準サイズ
	static constexpr int kPadding = 8;
};

} // namespace KujataEngine
