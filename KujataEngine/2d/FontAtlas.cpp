#include "FontAtlas.h"

#include "../base/ProjectPath.h"
#include "../base/TextureManager.h"
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244 4245 4456 4457 4701 4996 26451 26495 6262)
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../externals/stb/stb_truetype.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace KujataEngine {
namespace {

// fontPathでキャッシュ(SDFはスケール非依存なのでサイズ別に持たない)。
std::map<std::string, std::unique_ptr<FontAtlas>> gFontCache;

/// <summary>Windowsのフォントフォルダ。%WINDIR% から組み立てる(Cドライブ決め打ちにしない)。</summary>
std::filesystem::path SystemFontDirectory() {
	char* windowsRoot = nullptr;
	std::size_t length = 0;
	if (_dupenv_s(&windowsRoot, &length, "WINDIR") == 0 && windowsRoot) {
		std::filesystem::path path = std::filesystem::path(windowsRoot) / "Fonts";
		std::free(windowsRoot);
		return path;
	}
	return std::filesystem::path("C:/Windows/Fonts");
}

/// <summary>
/// 同梱フォントが見つからなかったときに、どのシステムフォントで代用するかを決める。
/// **見た目を変えないための振り分け**で、明朝を求めている表示にゴシックを返したり、
/// 太字の見出しを細字にしたりしないようにファイル名から読み取る。
/// </summary>
const std::vector<const char*>& SystemFallbackCandidates(const std::string& lowerName) {
	// **末尾は必ず「どのWindowsにも入っているゴシック」で締める。** 明朝(游明朝・MS明朝)は
	// 日本語以外のロケールでは省かれることがあり、そこで打ち切ると文字が丸ごと消える。
	static const std::vector<const char*> kSerif = {"yumin.ttf", "msmincho.ttc", "yugothm.ttc", "meiryo.ttc", "YuGothR.ttc", "msgothic.ttc"};
	static const std::vector<const char*> kSansBold = {"YuGothB.ttc", "meiryob.ttc", "YuGothR.ttc", "msgothic.ttc"};
	static const std::vector<const char*> kSans = {"YuGothR.ttc", "meiryo.ttc", "msgothic.ttc", "YuGothB.ttc"};

	if (lowerName.find("bold") != std::string::npos) {
		return kSansBold;
	}
	if (lowerName.find("serif") != std::string::npos || lowerName.find("mincho") != std::string::npos) {
		return kSerif;
	}
	return kSans;
}

/// <summary>
/// **フォントの置き場所を解決する。** JSONには論理名(`Fonts/JapaneseSerif.ttf` など)を書き、
/// 実体をどこから拾うかはここだけで決める。探す順は:
///   1. 書かれたパスそのもの(絶対パスをまだ使っている古いデータのため)
///   2. `Data/<書かれたパス>` / `Data/Fonts/<ファイル名>` — 置けば使う、任意の差し替え口
///   3. **システムのフォント**(明朝/ゴシック/太字の別だけ合わせる) ← 通常はここに落ちる
///
/// **配布物にフォントは同梱していない。** 配布先はWindows前提で、日本語フォントは
/// OSに入っているものを使う。候補を複数並べてあるのは、**明朝が入っていない環境
/// (日本語以外のロケールのWindowsでは游明朝・MS明朝が省かれることがある)でも
/// ゴシックへ落ちて文字が消えないようにする**ため。
/// 特定の見た目を確実に出したくなったら `Data/Fonts/` へ実体を置けばそちらが優先される
/// (その場合は再配布が許諾されたフォントを使うこと)。
/// </summary>
std::string ResolveFontPath(const std::string& requested) {
	if (requested.empty()) {
		return requested;
	}
	std::error_code error;

	const std::filesystem::path asWritten(requested);
	if (std::filesystem::exists(asWritten, error)) {
		return requested;
	}

	const std::filesystem::path dataRoot = GetProjectDataRoot();
	const std::filesystem::path underData = dataRoot / requested;
	if (std::filesystem::exists(underData, error)) {
		return underData.string();
	}

	const std::filesystem::path fileName = asWritten.filename();
	const std::filesystem::path underFonts = dataRoot / "Fonts" / fileName;
	if (std::filesystem::exists(underFonts, error)) {
		return underFonts.string();
	}

	// ここから先はシステム頼み。**同梱せずに配布された状態でも文字が消えないため**の保険。
	std::string lowerName = fileName.string();
	for (char& character : lowerName) {
		character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
	}
	const std::filesystem::path systemFonts = SystemFontDirectory();
	for (const char* candidate : SystemFallbackCandidates(lowerName)) {
		const std::filesystem::path path = systemFonts / candidate;
		if (std::filesystem::exists(path, error)) {
			return path.string();
		}
	}
	return requested; // 見つからない。Initializeが失敗して呼び出し側が諦める。
}

std::vector<uint8_t> ReadFile(const std::string& path) {
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file) {
		return {};
	}
	std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);
	std::vector<uint8_t> buffer(static_cast<size_t>(size));
	if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
		return {};
	}
	return buffer;
}

// UTF-8文字列をUnicodeコードポイント列へデコードする(不正バイトはU+FFFD)。
std::vector<uint32_t> DecodeUtf8(const std::string& s) {
	std::vector<uint32_t> out;
	const size_t n = s.size();
	size_t i = 0;
	while (i < n) {
		const unsigned char c = static_cast<unsigned char>(s[i]);
		uint32_t cp = 0;
		int len = 1;
		if (c < 0x80) {
			cp = c;
			len = 1;
		} else if ((c >> 5) == 0x6) {
			cp = c & 0x1Fu;
			len = 2;
		} else if ((c >> 4) == 0xE) {
			cp = c & 0x0Fu;
			len = 3;
		} else if ((c >> 3) == 0x1E) {
			cp = c & 0x07u;
			len = 4;
		} else {
			out.push_back(0xFFFDu);
			++i;
			continue;
		}
		if (i + static_cast<size_t>(len) > n) {
			out.push_back(0xFFFDu);
			break;
		}
		bool ok = true;
		for (int k = 1; k < len; ++k) {
			const unsigned char cc = static_cast<unsigned char>(s[i + k]);
			if ((cc >> 6) != 0x2) {
				ok = false;
				break;
			}
			cp = (cp << 6) | (cc & 0x3Fu);
		}
		if (!ok) {
			out.push_back(0xFFFDu);
			++i;
			continue;
		}
		out.push_back(cp);
		i += static_cast<size_t>(len);
	}
	return out;
}

} // namespace

FontAtlas* FontAtlas::GetOrCreate(const std::string& fontPath) {
	// **キャッシュは要求されたパスで引く。** 解決は毎回ファイルを叩くので、
	// 同じ名前で何度も呼ばれる描画側にその手間を負わせない。
	auto found = gFontCache.find(fontPath);
	if (found != gFontCache.end()) {
		return found->second.get();
	}

	auto atlas = std::make_unique<FontAtlas>();
	if (!atlas->Initialize(ResolveFontPath(fontPath))) {
		return nullptr;
	}
	FontAtlas* result = atlas.get();
	gFontCache[fontPath] = std::move(atlas);
	return result;
}

FontAtlas* FontAtlas::GetOrCreate(const std::string& fontPath, float /*pixelHeight*/) {
	// SDFではサイズはBuildTextで指定するため、ベイクはサイズ非依存。
	return GetOrCreate(fontPath);
}

bool FontAtlas::Initialize(const std::string& fontPath) {
	fontPath_ = fontPath;
	fontData_ = ReadFile(fontPath);
	if (fontData_.empty()) {
		return false;
	}

	// stbtt_fontinfoをbyte列で保持(ヘッダに実体を露出させないため)。fontData_を参照するので生存必須。
	fontInfoStorage_.assign(sizeof(stbtt_fontinfo), 0);
	stbtt_fontinfo* font = reinterpret_cast<stbtt_fontinfo*>(fontInfoStorage_.data());

	// .ttc(コレクション)はインデックス0のサブフォントのオフセットを使う。.ttfは0。
	const int fontOffset = stbtt_GetFontOffsetForIndex(fontData_.data(), 0);
	if (fontOffset < 0 || !stbtt_InitFont(font, fontData_.data(), fontOffset)) {
		return false;
	}

	scale_ = stbtt_ScaleForPixelHeight(font, sdfBakePixelHeight_);
	int ascent = 0, descent = 0, lineGap = 0;
	stbtt_GetFontVMetrics(font, &ascent, &descent, &lineGap);
	ascent_ = ascent * scale_;

	atlasPixels_.assign(static_cast<size_t>(atlasWidth_) * atlasHeight_, 0);
	penX_ = kPadding;
	penY_ = kPadding;
	rowHeight_ = 0;
	atlasFull_ = false;
	fontReady_ = true;

	// ASCII(32..126)は先にベイクしておく(Latinが即描画でき、動的更新も減る)。
	for (uint32_t cp = 32; cp <= 126; ++cp) {
		BakeGlyph(cp);
	}
	UploadAtlas(); // 初回のテクスチャ生成 + textureIndex_確定。
	return true;
}

bool FontAtlas::BakeGlyph(uint32_t codepoint) {
	if (!fontReady_) {
		return false;
	}
	if (glyphs_.find(codepoint) != glyphs_.end()) {
		return false; // 既にベイク済み。
	}

	stbtt_fontinfo* font = reinterpret_cast<stbtt_fontinfo*>(fontInfoStorage_.data());

	Glyph glyph;
	int advance = 0, leftSideBearing = 0;
	stbtt_GetCodepointHMetrics(font, static_cast<int>(codepoint), &advance, &leftSideBearing);
	glyph.xadvance = advance * scale_;

	bool wrotePixels = false;
	int w = 0, h = 0, xoff = 0, yoff = 0;
	unsigned char* sdf = stbtt_GetCodepointSDF(font, scale_, static_cast<int>(codepoint), kPadding, 128, 128.0f / static_cast<float>(kPadding), &w, &h, &xoff, &yoff);
	if (sdf && w > 0 && h > 0) {
		// 行が足りなければ次の行へ折り返す。
		if (penX_ + w + kPadding > atlasWidth_) {
			penX_ = kPadding;
			penY_ += rowHeight_ + kPadding;
			rowHeight_ = 0;
		}
		if (penY_ + h + kPadding <= atlasHeight_) {
			for (int y = 0; y < h; ++y) {
				for (int x = 0; x < w; ++x) {
					atlasPixels_[static_cast<size_t>(penY_ + y) * atlasWidth_ + (penX_ + x)] = sdf[y * w + x];
				}
			}
			glyph.u0 = static_cast<float>(penX_) / atlasWidth_;
			glyph.v0 = static_cast<float>(penY_) / atlasHeight_;
			glyph.u1 = static_cast<float>(penX_ + w) / atlasWidth_;
			glyph.v1 = static_cast<float>(penY_ + h) / atlasHeight_;
			glyph.width = static_cast<float>(w);
			glyph.height = static_cast<float>(h);
			glyph.xoff = static_cast<float>(xoff);
			glyph.yoff = static_cast<float>(yoff);
			glyph.valid = true;

			penX_ += w + kPadding;
			rowHeight_ = rowHeight_ > h ? rowHeight_ : h; // windows.hのmaxマクロ回避のため三項で。
			wrotePixels = true;
		} else {
			atlasFull_ = true; // これ以上入らない。以降のグリフは描画されない(送りだけ有効)。
		}
	}
	if (sdf) {
		stbtt_FreeSDF(sdf, nullptr);
	}

	glyphs_[codepoint] = glyph;
	return wrotePixels;
}

void FontAtlas::UploadAtlas() {
	// 単色SDFをRGBA全チャンネルへ複製(シェーダは.rを距離として読む)。
	std::vector<uint8_t> rgba(static_cast<size_t>(atlasWidth_) * atlasHeight_ * 4);
	for (size_t i = 0; i < atlasPixels_.size(); ++i) {
		const uint8_t v = atlasPixels_[i];
		rgba[i * 4 + 0] = v;
		rgba[i * 4 + 1] = v;
		rgba[i * 4 + 2] = v;
		rgba[i * 4 + 3] = v;
	}
	const std::string textureKey = "__sdf__" + fontPath_;
	textureIndex_ = TextureManager::GetInstance()->UpdateTextureFromMemory(textureKey, rgba.data(), static_cast<uint32_t>(atlasWidth_), static_cast<uint32_t>(atlasHeight_));
}

void FontAtlas::EnsureGlyphsForText(const std::string& utf8Text) {
	if (!fontReady_) {
		return;
	}
	const std::vector<uint32_t> codepoints = DecodeUtf8(utf8Text);
	bool changed = false;
	for (uint32_t cp : codepoints) {
		if (glyphs_.find(cp) == glyphs_.end()) {
			if (BakeGlyph(cp)) {
				changed = true;
			}
		}
	}
	if (changed) {
		UploadAtlas();
	}
}

std::vector<FontAtlas::GlyphQuad> FontAtlas::BuildText(const std::string& utf8Text, float pixelHeight, float& outTotalWidth) const {
	std::vector<GlyphQuad> quads;
	outTotalWidth = 0.0f;
	if (!fontReady_) {
		return quads;
	}
	if (pixelHeight < 1.0f) {
		pixelHeight = 1.0f;
	}

	// ベイクサイズ基準でレイアウトし、要求サイズへ拡縮する(SDFなので拡大してもくっきり)。
	const float renderScale = pixelHeight / sdfBakePixelHeight_;
	const float baseline = ascent_;
	float x = 0.0f;

	const std::vector<uint32_t> codepoints = DecodeUtf8(utf8Text);
	for (uint32_t cp : codepoints) {
		auto it = glyphs_.find(cp);
		if (it == glyphs_.end()) {
			// 未ベイク(Prepareがまだ焼いていない)。次フレームで焼かれる。とりあえず送りだけ。
			x += sdfBakePixelHeight_ * 0.5f;
			continue;
		}
		const Glyph& glyph = it->second;
		if (glyph.valid) {
			GlyphQuad quad;
			quad.rect.x = (x + glyph.xoff) * renderScale;
			quad.rect.y = (baseline + glyph.yoff) * renderScale;
			quad.rect.width = glyph.width * renderScale;
			quad.rect.height = glyph.height * renderScale;
			quad.u0 = glyph.u0;
			quad.v0 = glyph.v0;
			quad.u1 = glyph.u1;
			quad.v1 = glyph.v1;
			quads.push_back(quad);
		}
		x += glyph.xadvance;
	}

	outTotalWidth = x * renderScale;
	return quads;
}

} // namespace KujataEngine
