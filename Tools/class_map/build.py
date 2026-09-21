"""クラス地図(.claude/class-map.html)を作り直す。

使い方: python Tools/class_map/build.py
  1. extract.py でソースから全クラスと関係を抜き出す(class_map.json)
  2. template.html にそのデータを埋め込んで .claude/class-map.html を書く
"""
import datetime
import json
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
OUTPUT = REPO / ".claude" / "class-map.html"


def main():
    subprocess.run([sys.executable, str(HERE / "extract.py")], check=True)
    data = json.loads((HERE / "class_map.json").read_text(encoding="utf-8"))
    data["generatedAt"] = datetime.date.today().isoformat()
    template = (HERE / "template.html").read_text(encoding="utf-8")
    # </script> がデータに混ざってもページが壊れないようにする
    payload = json.dumps(data, ensure_ascii=False, separators=(",", ":")).replace("</", "<\\/")
    OUTPUT.write_text(template.replace("/*__DATA__*/null", payload), encoding="utf-8")
    print(f"wrote {OUTPUT.relative_to(REPO)} ({OUTPUT.stat().st_size // 1024} KB)")


if __name__ == "__main__":
    main()
