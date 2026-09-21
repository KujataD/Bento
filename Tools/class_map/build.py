"""クラス地図(.claude/class-map.html)を作り直す。

使い方: python Tools/class_map/build.py
  1. extract.py でソースから全クラスと関係を抜き出す(class_map.json)
  2. template.html にそのデータと、下のモジュールの説明を埋め込んで .claude/class-map.html を書く
"""
import datetime
import json
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
OUTPUT = REPO / ".claude" / "class-map.html"

# モジュール(フォルダ)の並び順と一行の説明。上から「入口 → 土台 → 見た目 → 下回り」の順に読めるように並べる。
# 新しいフォルダを作ったらここにも足す(無ければ末尾に説明なしで並ぶ)。
MODULES = [
    ("KujataEngine", "エンジンの入口。初期化・毎フレームの更新・終了の順番を決める"),
    ("runtime", "エディタとゲームの境界。ゲーム DLL の読み込み、シーンの切り替え、Play 中かどうか、タグ"),
    ("scene", "コンポーネント指向の中心。GameObject・Component・Scene と、その保存・物理・当たり判定・編集用の表示"),
    ("components", "組み込みのコンポーネント(Transform・描画・物理・カメラ・ライト・UI・アニメーション・音など)"),
    ("Editor", "ImGui のエディタ。各ウィンドウ、Undo、プレハブ、CUI の受付、スクリーンショット、ログ"),
    ("Editor/Commands", "CUI(kujata・Console の入力欄)で使えるコマンドの共通処理"),
    ("3d", "3D の描画。モデル・カメラ・ライト・描画パイプライン・線の描画"),
    ("2d", "2D と UI の描画。スプライト・フォント・Canvas・UI のイベントとフォーカス移動"),
    ("postprocess", "ポストエフェクト。HDR・ブルーム・フォグ・トーンマップと、場所ごとに効果を切り替える Volume"),
    ("vfx", "パーティクルとインスタンシング描画"),
    ("assets", "エディタで作るアセット(アニメーションクリップ・マテリアル・ノイズテクスチャ)"),
    ("base", "下回り。D3D12・ウィンドウ・テクスチャ・音・時間・ログ・パス"),
    ("input", "キーボード・マウス・パッドの入力"),
    ("math", "ベクトル・行列・クォータニオン・イージング・ノイズ・乱数"),
    ("shapes", "当たり判定の形(球・箱・カプセル・レイ)と判定の計算"),
    ("DirectXGame/GameModule", "このリポジトリのゲーム(ゲーム DLL)。エンジンのクラスを継承・利用する側"),
]


def main():
    subprocess.run([sys.executable, str(HERE / "extract.py")], check=True)
    data = json.loads((HERE / "class_map.json").read_text(encoding="utf-8"))
    data["generatedAt"] = datetime.date.today().isoformat()
    data["modules"] = [{"name": name, "description": description} for name, description in MODULES]
    template = (HERE / "template.html").read_text(encoding="utf-8")
    # </script> がデータに混ざってもページが壊れないようにする
    payload = json.dumps(data, ensure_ascii=False, separators=(",", ":")).replace("</", "<\\/")
    OUTPUT.write_text(template.replace("/*__DATA__*/null", payload), encoding="utf-8")
    print(f"wrote {OUTPUT.relative_to(REPO)} ({OUTPUT.stat().st_size // 1024} KB)")


if __name__ == "__main__":
    main()
