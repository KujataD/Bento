#pragma once

#include <KujataEngine.h>

#include <algorithm>
#include <cmath>
#include <vector>

/// <summary>
/// **攻撃の予告板。** 敵が「これから・どこに・どんな形の危険を出すか」を掲示し、
/// 味方(AllyAIBrain)がそれを読んで避ける。
///
/// なぜ「宣言」なのか:
///   - 姿勢から推測させると、攻撃を1つ足すたびに受け側にも分岐が増え、攻撃の知識が2箇所に散る。
///   - 当たり判定(EnemyWeapon)を先読みさせようにも、**判定は当たる瞬間にしか存在しない**ので
///     先行時間がゼロになり、構造的に回避できない。
///   - 攻撃側は既に中心も半径も持続も知っている(StartShockwaveの引数がまさにそれ)。
///     その数値を予備動作の開始時に先出しするだけなら、新しい真実を作らずに済む。
///
/// ダメージには唯一の入口 PlayerHealth::ReceiveHit がある。こちらは**その鏡像**で、
/// 「これから起きるダメージ」の唯一の入口として置いてある。
///
/// 置き場が GameModule 内の静的変数なのは GameSession / PartySelection と同じ流儀。
/// 発行側(敵)も購読側(味方)も同じDLLに居るので、KUJATA_API の輸出問題を丸ごと回避できる。
///
/// 時計はこの板が自分で持つ(Advanceで進める絶対時刻)。**スケール済みの時間**を使うこと —
/// 攻撃側のフェーズ管理(AIContext::deltaTime)も同じスケール時間で動いているので、
/// ヒットストップで世界が止まれば予告も一緒に止まり、命中予定時刻がずれない。
/// </summary>
namespace Threat {

/// <summary>危険域の形。</summary>
enum class Shape {
	Circle, ///< 円(衝撃波・踏みつけ・着地)。origin中心・radius半径。
	Line,   ///< 線(ビーム)。originから directionへ length 伸びる、半幅 radius の帯。
};

/// <summary>攻撃の属性。どちらのガードが有効かの判断に使う。</summary>
enum class Element {
	Physical, ///< 剣士の盾が完全に防ぐ。術師のバリアは半減。
	Magic,    ///< 術師のバリアが完全に防ぐ。剣士の盾は半減。
};

/// <summary>掲示1件。</summary>
struct Notice {
	KujataEngine::GameObject* source = nullptr;
	Shape shape = Shape::Circle;
	Element element = Element::Physical;

	// 円の中心 / 線の始点(ワールド)。followSourceがtrueならsourceからの相対として毎フレーム作り直される。
	KujataEngine::Vector3 origin = {0.0f, 0.0f, 0.0f};
	// 線の向き(正規化。Circleでは未使用)。
	KujataEngine::Vector3 direction = {0.0f, 0.0f, 1.0f};
	// 円の最終半径 / 線の半幅。
	float radius = 1.0f;
	// 線の長さ(Circleでは未使用)。
	float length = 0.0f;

	// 当たる予定の時刻[板の絶対秒]。Announce時は「今から何秒後か」を入れておくと Announce が絶対時刻へ直す。
	float hitTime = 0.0f;
	// 危険が消える時刻[板の絶対秒]。同上。
	float clearTime = 0.0f;

	// 中心を発生源へ追従させるか(コマ回転のように動きながら当てる攻撃)。
	bool followSource = false;
	// 無敵(0.45秒)では覆えない持続型か。trueなら受け側は回避ではなく離脱を選ぶ。
	bool sustained = false;

	float damage = 0.0f;
};

/// <summary>ある地点から見た「今いちばん危ない予告」の評価結果。</summary>
struct Assessment {
	bool valid = false;
	// 命中まで何秒か(負なら既に危険が出ている)。
	float timeToHit = 0.0f;
	// 危険域へ何m食い込んでいるか。そのまま「逃げるべき距離」になる。
	float penetration = 0.0f;
	// 最短で出られる向き(正規化。円なら外向き、線なら垂直)。
	KujataEngine::Vector3 escape = {0.0f, 0.0f, 1.0f};
	bool sustained = false;
	Element element = Element::Physical;
	// 同じ予告に何度も回避を切らないための識別子。
	int id = 0;
};

namespace Detail {

/// <summary>敵1体ぶんの「今攻めてよいか」。予告板と同じ時計で動く。</summary>
struct Stance {
	KujataEngine::GameObject* source = nullptr;
	// この時刻までは攻撃モーション中(踏み込むと相打ちになる)。
	float committedUntil = 0.0f;
	// この時刻までは後隙(攻め込んでよい)。committedより優先する。
	float openUntil = 0.0f;
	// 今この敵が狙っている相手(ヘイト)。**パーティで共有するために敵自身が申告する。**
	KujataEngine::GameObject* aggro = nullptr;
};

struct Entry {
	int id = 0;
	Notice notice;
	// followSource用: 発生源から見た中心のオフセット(掲示時に固定する)。
	KujataEngine::Vector3 followOffset = {0.0f, 0.0f, 0.0f};
};

inline std::vector<Entry>& Entries() {
	static std::vector<Entry> entries;
	return entries;
}

inline std::vector<Stance>& Stances() {
	static std::vector<Stance> stances;
	return stances;
}

inline Stance& StanceFor(KujataEngine::GameObject* source) {
	for (Stance& stance : Stances()) {
		if (stance.source == source) {
			return stance;
		}
	}
	Stances().push_back(Stance{source, 0.0f, 0.0f, nullptr});
	return Stances().back();
}

inline float& Now() {
	static float now = 0.0f;
	return now;
}

inline int& NextId() {
	static int nextId = 1;
	return nextId;
}

// エンジンの Dot/Normalize の一部は KUJATA_API が無くDLLからリンクできないので、
// 必要な最小限をここへ持つ(GuardianRigMathと同じ理由)。名前に2を付けているのはADL衝突を避けるため。
inline float Dot2(const KujataEngine::Vector3& a, const KujataEngine::Vector3& b) { return a.x * b.x + a.z * b.z; }

inline KujataEngine::Vector3 Horizontal(const KujataEngine::Vector3& v) { return {v.x, 0.0f, v.z}; }

inline float HorizontalLength(const KujataEngine::Vector3& v) { return std::sqrt(v.x * v.x + v.z * v.z); }

inline KujataEngine::Vector3 SafeDirection(const KujataEngine::Vector3& v, const KujataEngine::Vector3& fallback) {
	float length = HorizontalLength(v);
	if (length <= 1.0e-5f) {
		return fallback;
	}
	return {v.x / length, 0.0f, v.z / length};
}

/// <summary>掲示の現在の中心(followSourceなら発生源へ追従した位置)。</summary>
inline KujataEngine::Vector3 CurrentOrigin(const Entry& entry) {
	if (entry.notice.followSource && entry.notice.source) {
		return entry.notice.source->GetTransform().translation_ + entry.followOffset;
	}
	return entry.notice.origin;
}

} // namespace Detail

/// <summary>予告を掲示する。hitTime/clearTimeには「今から何秒後か」を入れて渡すこと。戻り値は取り下げ用のID。</summary>
inline int Announce(const Notice& notice) {
	Detail::Entry entry;
	entry.id = Detail::NextId()++;
	entry.notice = notice;
	entry.notice.hitTime = Detail::Now() + notice.hitTime;
	entry.notice.clearTime = Detail::Now() + notice.clearTime;
	if (notice.followSource && notice.source) {
		entry.followOffset = notice.origin - notice.source->GetTransform().translation_;
	}
	Detail::Entries().push_back(entry);
	return entry.id;
}

/// <summary>
/// 掲示内容を差し替える(着弾点が確定したときなど)。
/// hitTime/clearTimeは Announce と同じく「今から何秒後か」。IDが無ければ何もしない。
/// </summary>
inline void Amend(int id, const Notice& notice) {
	if (id <= 0) {
		return;
	}
	for (Detail::Entry& entry : Detail::Entries()) {
		if (entry.id != id) {
			continue;
		}
		entry.notice = notice;
		entry.notice.hitTime = Detail::Now() + notice.hitTime;
		entry.notice.clearTime = Detail::Now() + notice.clearTime;
		if (notice.followSource && notice.source) {
			entry.followOffset = notice.origin - notice.source->GetTransform().translation_;
		}
		return;
	}
}

/// <summary>
/// 掲示を取り下げる。**予告を出した者が取り下げる責任を持つ。**
/// スタンで攻撃が消えたのに危険域が残ると、味方AIは存在しない攻撃から永久に逃げ続ける。
/// </summary>
inline void Withdraw(int id) {
	if (id <= 0) {
		return;
	}
	std::vector<Detail::Entry>& entries = Detail::Entries();
	for (size_t index = 0; index < entries.size(); ++index) {
		if (entries[index].id == id) {
			entries.erase(entries.begin() + static_cast<long long>(index));
			return;
		}
	}
}

/// <summary>指定の発生源が出している掲示をすべて取り下げる(スタン・Play停止の保険)。</summary>
inline void WithdrawAllFrom(KujataEngine::GameObject* source) {
	std::vector<Detail::Entry>& entries = Detail::Entries();
	entries.erase(
	    std::remove_if(entries.begin(), entries.end(), [source](const Detail::Entry& entry) { return entry.notice.source == source; }),
	    entries.end());
}

/// <summary>時計を進め、期限切れの掲示を捨てる。**毎フレーム1回だけ**呼ぶこと。</summary>
inline void Advance(float deltaTime) {
	Detail::Now() += deltaTime;
	float now = Detail::Now();
	std::vector<Detail::Entry>& entries = Detail::Entries();
	entries.erase(
	    std::remove_if(entries.begin(), entries.end(), [now](const Detail::Entry& entry) { return entry.notice.clearTime <= now; }),
	    entries.end());
}

/// <summary>Play開始時に捨てる。コンポーネントは使い回されるので、前回Playの掲示を持ち越さない。</summary>
inline void Clear() {
	Detail::Entries().clear();
	Detail::Stances().clear();
	Detail::Now() = 0.0f;
	Detail::NextId() = 1;
}

/// <summary>掲示件数(デバッグ表示用)。</summary>
inline int Count() { return static_cast<int>(Detail::Entries().size()); }

// ---------------------------------------------------------------------------
// 構え(隙)の申告
//
// **死にゲーの間合いは「常に殴る」ではなく「隙を突く」。**
// 攻撃モーション中に踏み込めば、避けたはずの相手と相打ちになる。
// かといって受け側に「今のモーションは何フレーム目か」を推測させるのは、
// 予告を姿勢から推測させるのと同じ筋の悪さになる(攻撃側の知識が2箇所に散る)。
//
// そこで予告板と同じ流儀にする: **攻撃側が「今は踏み込むな / 今が隙だ」を申告する。**
// 攻撃側は自分の後隙の長さを知っているので、渡すだけで済む。
// ---------------------------------------------------------------------------

/// <summary>
/// 攻撃モーション中であることを申告する(踏み込ませない)。
/// **毎Tick少しずつ延長する使い方**を想定していて、フェーズの継ぎ目で途切れない。
/// </summary>
inline void SetCommitted(KujataEngine::GameObject* source, float seconds) {
	if (!source) {
		return;
	}
	Detail::Stance& stance = Detail::StanceFor(source);
	stance.committedUntil = (std::max)(stance.committedUntil, Detail::Now() + seconds);
}

/// <summary>
/// 後隙に入ったことを申告する(ここが攻め込む窓)。**committedより優先する。**
/// 攻撃の終わり際やスタン開始で呼ぶ。
/// </summary>
inline void SetOpen(KujataEngine::GameObject* source, float seconds) {
	if (!source) {
		return;
	}
	Detail::Stance& stance = Detail::StanceFor(source);
	stance.openUntil = (std::max)(stance.openUntil, Detail::Now() + seconds);
}

/// <summary>
/// 今この相手へ攻め込んでよいか。
/// 後隙なら当然よい。何も申告が無い(歩いているだけ)ときもよい —
/// **「攻撃していない敵」は隙**であって、そこまで待つと誰も殴れなくなる。
/// </summary>
inline bool IsOpen(KujataEngine::GameObject* source) {
	if (!source) {
		return false;
	}
	float now = Detail::Now();
	for (const Detail::Stance& stance : Detail::Stances()) {
		if (stance.source != source) {
			continue;
		}
		if (now < stance.openUntil) {
			return true; // 後隙。
		}
		return now >= stance.committedUntil; // 攻撃モーション中でなければ攻めてよい。
	}
	return true; // 一度も申告していない相手(雑魚など)は従来どおり攻めてよい。
}

// ---------------------------------------------------------------------------
// ヘイトの共有
//
// **狙われている者と狙われていない者では、取るべき行動が正反対になる。**
// 狙われている側が攻撃を差し込めば、そのまま相打ちになる。
// 狙われていない側が身構えていても、誰もHPを削らない。
//
// 敵は自分が誰を狙っているかを知っているので、それを申告してもらう。
// 味方は「自分か、相方か」を見るだけで役割を決められる。
// ---------------------------------------------------------------------------

/// <summary>今この敵が狙っている相手を申告する(毎フレーム上書きしてよい)。</summary>
inline void SetAggro(KujataEngine::GameObject* source, KujataEngine::GameObject* target) {
	if (!source) {
		return;
	}
	Detail::StanceFor(source).aggro = target;
}

/// <summary>この者が誰かに狙われているか。**狙われている側は避けに専念する。**</summary>
inline bool IsTargeted(const KujataEngine::GameObject* who) {
	if (!who) {
		return false;
	}
	for (const Detail::Stance& stance : Detail::Stances()) {
		if (stance.aggro == who) {
			return true;
		}
	}
	return false;
}

/// <summary>
/// positionにいる者にとって、horizon秒以内に当たる掲示のうち**最も差し迫ったもの**を返す。
/// bodyRadiusは体の太さ(この分だけ危険域を広く見る)。
///
/// 選び方は「命中が早い順」ではなく「**今から逃げないと間に合わない順**」——
/// 具体的には timeToHit が小さいものを優先しつつ、既に外に出ているものは候補にしない。
/// </summary>
inline Assessment Assess(const KujataEngine::Vector3& position, float bodyRadius, float horizon) {
	Assessment best;
	float bestTime = horizon;

	for (const Detail::Entry& entry : Detail::Entries()) {
		const Notice& notice = entry.notice;
		float timeToHit = notice.hitTime - Detail::Now();
		if (timeToHit > horizon) {
			continue; // まだ遠い先の話。
		}

		KujataEngine::Vector3 origin = Detail::CurrentOrigin(entry);
		float penetration = 0.0f;
		KujataEngine::Vector3 escape = {0.0f, 0.0f, 1.0f};

		if (notice.shape == Shape::Circle) {
			KujataEngine::Vector3 toSelf = Detail::Horizontal(position - origin);
			float distance = Detail::HorizontalLength(toSelf);
			float danger = notice.radius + bodyRadius;
			if (distance >= danger) {
				continue; // 既に輪の外。
			}
			penetration = danger - distance;
			// 中心に重なっているときは適当な向きへ逃がす(そのまま止まるよりましなので)。
			escape = Detail::SafeDirection(toSelf, {0.0f, 0.0f, 1.0f});
		} else {
			KujataEngine::Vector3 dir = Detail::SafeDirection(notice.direction, {0.0f, 0.0f, 1.0f});
			KujataEngine::Vector3 toSelf = Detail::Horizontal(position - origin);
			float along = Detail::Dot2(toSelf, dir);
			if (along < -bodyRadius || along > notice.length + bodyRadius) {
				continue; // 線の前後の外。
			}
			// 線に垂直な成分。左右どちらへ抜けるのが近いかもここで決まる。
			KujataEngine::Vector3 perpendicular = {-dir.z, 0.0f, dir.x};
			float side = Detail::Dot2(toSelf, perpendicular);
			float danger = notice.radius + bodyRadius;
			if (std::fabs(side) >= danger) {
				continue; // 既に帯の外。
			}
			penetration = danger - std::fabs(side);
			float sign = (side >= 0.0f) ? 1.0f : -1.0f;
			escape = {perpendicular.x * sign, 0.0f, perpendicular.z * sign};
		}

		if (!best.valid || timeToHit < bestTime) {
			best.valid = true;
			best.timeToHit = timeToHit;
			best.penetration = penetration;
			best.escape = escape;
			best.sustained = notice.sustained;
			best.element = notice.element;
			best.id = entry.id;
			bestTime = timeToHit;
		}
	}

	return best;
}

} // namespace Threat
