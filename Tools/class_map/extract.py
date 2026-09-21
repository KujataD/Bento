"""KujataEngine のソースから、全クラス(と関数をまとめた名前空間)とその関係を抜き出す。

使い方: python Tools/class_map/extract.py  → Tools/class_map/class_map.json を書く
C++ を正規表現と括弧の対応だけで読む簡易な解析なので、テンプレートの込み入った書き方などは取りこぼすことがある。

関係の種類:
  inherits : 継承(基底クラス)
  owns     : 値・unique_ptr・コンテナで持つメンバ(寿命を持っている)
  refers   : ポインタ・参照・ObjectRef などで指すメンバ(寿命は持たない)
  uses     : メソッドの中で名前を使っている(呼び出し・生成・GetInstance など)
  nested   : 外側のクラスの中で定義された型(外側 → 内側)
"""
import json
import pathlib
import re
from collections import defaultdict

REPO = pathlib.Path(__file__).resolve().parents[2]
SOURCE_ROOTS = [REPO / "KujataEngine", REPO / "DirectXGame"]
SKIP_PARTS = {"Temp", "externals", "build", "bin", "obj", "logs", "EngineData", "resources", "Data"}
OUTPUT = pathlib.Path(__file__).resolve().parent / "class_map.json"

# 「使っている」の対象から外すありふれた名前(入れ子の型で、同じ名前が何か所にもあるもの)
GENERIC_NAMES = {"Entry", "Result", "Request", "Command", "Snapshot", "Settings", "Data", "Item", "Node", "State", "Info", "Config", "Options"}
# 型のように見えるが、クラスとして数えないもの
IGNORED_CLASS_NAMES = {"T", "TObject", "Args"}


def strip_code(text):
    """コメント・文字列・文字リテラル・プリプロセッサ行を空白にする(改行は残して行番号を保つ)。"""
    result = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            result.append(" " * (j - i))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            result.append("".join("\n" if ch == "\n" else " " for ch in text[i:j]))
            i = j
        elif c == '"' or c == "'":
            # 生文字列 R"(...)" も含めて読み飛ばす
            if c == '"' and i > 0 and text[i - 1] == "R":
                end_marker = text.find("(", i)
                delimiter = text[i + 1:end_marker]
                j = text.find(")" + delimiter + '"', end_marker)
                j = n if j < 0 else j + len(delimiter) + 2
            else:
                j = i + 1
                while j < n and text[j] != c:
                    j += 2 if text[j] == "\\" else 1
                j += 1
            result.append("".join("\n" if ch == "\n" else " " for ch in text[i:j]))
            i = j
        elif c == "#" and (i == 0 or text[i - 1] == "\n" or text[:i].rstrip(" \t").endswith("\n")):
            # 行末の \ で続く行もまとめて消す
            j = i
            while True:
                k = text.find("\n", j)
                if k < 0:
                    k = n
                    break
                if text[k - 1] == "\\":
                    j = k + 1
                    continue
                break
            result.append("".join("\n" if ch == "\n" else " " for ch in text[i:k]))
            i = k
        else:
            result.append(c)
            i += 1
    return "".join(result)


def find_matching(text, open_index, open_char="{", close_char="}"):
    depth = 0
    for i in range(open_index, len(text)):
        if text[i] == open_char:
            depth += 1
        elif text[i] == close_char:
            depth -= 1
            if depth == 0:
                return i
    return len(text) - 1


def module_of(path):
    relative = path.relative_to(REPO)
    parts = relative.parts
    if parts[0] == "DirectXGame":
        return "DirectXGame/" + parts[1] if len(parts) > 2 else "DirectXGame"
    if len(parts) == 2:
        return "KujataEngine"
    if parts[1] == "Editor" and len(parts) > 3:
        return "Editor/" + parts[2]
    return parts[1]


def line_of(text, index):
    return text.count("\n", 0, index) + 1


DOC_TAG_RE = re.compile(r"</?(summary|param[^>]*|returns|remarks)>")


def doc_above(raw, index):
    """宣言の直前にある // や /// のコメント行をまとめて、説明文として返す(無ければ空)。"""
    lines = raw[:index].split("\n")[:-1]  # 宣言と同じ行の手前(インデント等)は捨てる
    collected = []
    while lines:
        line = lines[-1].strip()
        if line.startswith("template") and not collected:
            lines.pop()
            continue
        if line.startswith("//"):
            body = DOC_TAG_RE.sub("", line.lstrip("/").strip()).strip()
            if body:
                collected.append(body)
            lines.pop()
            continue
        break
    collected.reverse()
    return re.sub(r"\s+", " ", " ".join(collected)).strip()[:400]


CLASS_RE = re.compile(r"\b(class|struct)\s+((?:KUJATA_API\s+|alignas\s*\([^)]*\)\s+)*)([A-Za-z_]\w*)\s*(final\s*)?(:(?!:)[^;{]*)?\{")
NAMESPACE_RE = re.compile(r"\bnamespace\s+([A-Za-z_][\w:]*)?\s*\{")


def scan_scopes(clean):
    """テキスト中の { } を、namespace / class / その他(関数本体など)に分類して返す。"""
    openers = {}
    for m in CLASS_RE.finditer(clean):
        before = clean[max(0, m.start() - 12):m.start()]
        if re.search(r"\benum\s*$", before):
            continue
        # template<class T> の引数や friend class は CLASS_RE の { まで届かないので自然に除外される
        brace = m.end() - 1
        bases = []
        if m.group(5):
            base_text = m.group(5)[1:]
            depth = 0
            current = ""
            for ch in base_text:
                if ch == "<":
                    depth += 1
                elif ch == ">":
                    depth -= 1
                if ch == "," and depth == 0:
                    bases.append(current)
                    current = ""
                else:
                    current += ch
            bases.append(current)
            bases = [re.sub(r"\b(public|protected|private|virtual)\b", "", b).strip() for b in bases if b.strip()]
        openers[brace] = ("class", m.group(3), m.group(1), bases, m.start())
    for m in NAMESPACE_RE.finditer(clean):
        brace = m.end() - 1
        openers.setdefault(brace, ("namespace", m.group(1) or "", None, None, m.start()))
    return openers


def main():
    files = []
    for root in SOURCE_ROOTS:
        for path in sorted(root.rglob("*")):
            if path.suffix not in (".h", ".cpp", ".hpp") or set(path.relative_to(REPO).parts) & SKIP_PARTS:
                continue
            files.append(path)

    classes = {}          # id -> info
    class_bodies = {}     # id -> (clean text, start, end)
    namespace_nodes = {}  # id -> info
    code_regions = defaultdict(list)  # owner id -> [code text]
    cleaned = {}
    raws = {}

    # --- 1 回目: クラスと名前空間を見つける ---
    for path in files:
        raw = path.read_text(encoding="utf-8", errors="replace")
        clean = strip_code(raw)
        cleaned[path] = clean
        raws[path] = raw
        openers = scan_scopes(clean)
        stack = []  # (kind, qualified name or None, close index)
        i = 0
        while i < len(clean):
            ch = clean[i]
            if ch == "{":
                opener = openers.get(i)
                close = find_matching(clean, i)
                if opener and opener[0] == "class":
                    _, name, keyword, bases, start = opener
                    outer = [s[1] for s in stack if s[0] == "class"]
                    qualified = "::".join(outer + [name])
                    if name not in IGNORED_CLASS_NAMES:
                        node_id = qualified
                        if node_id in classes and classes[node_id]["file"] != str(path.relative_to(REPO)).replace("\\", "/"):
                            node_id = f"{qualified}@{path.stem}"  # 別ファイルに同名(無名名前空間の中など)
                        body_text = clean[i:close + 1]
                        tags = []
                        if re.search(r"\bstatic\b[^;{]*\bGetInstance\s*\(", body_text):
                            tags.append("singleton")
                        if re.match(r"I[A-Z]", name) and "virtual" in body_text and "= 0" in body_text:
                            tags.append("interface")
                        classes[node_id] = {
                            "id": node_id, "name": name, "qualified": qualified, "kind": keyword,
                            "module": module_of(path), "file": str(path.relative_to(REPO)).replace("\\", "/"),
                            "line": line_of(clean, start), "bases": bases, "outer": "::".join(outer) or None,
                            "doc": doc_above(raw, start), "tags": tags,
                        }
                        class_bodies[node_id] = (clean, i, close)
                    stack.append(("class", name, close))
                elif opener and opener[0] == "namespace":
                    stack.append(("namespace", opener[1], close))
                else:
                    stack.append(("block", None, close))
                    i = close  # 関数本体などの中は 2 回目で読む
                    stack.pop()
            elif ch == "}":
                if stack:
                    stack.pop()
            i += 1

    simple_to_ids = defaultdict(list)
    for node_id, info in classes.items():
        simple_to_ids[info["name"]].append(node_id)

    # --- 2 回目: 関数の本体を持ち主(クラス / 名前空間)へ振り分ける ---
    FUNC_SIG_RE = re.compile(r"([A-Za-z_][\w:]*)::(~?[A-Za-z_]\w*|operator\s*\S+)\s*\($")
    for path in files:
        clean = cleaned[path]
        openers = scan_scopes(clean)
        stack = []
        i = 0
        last_boundary = 0
        while i < len(clean):
            ch = clean[i]
            if ch == "{":
                opener = openers.get(i)
                close = find_matching(clean, i)
                if opener and opener[0] in ("class", "namespace"):
                    stack.append((opener[0], opener[1], close))
                    last_boundary = i + 1
                    i += 1
                    continue
                signature = clean[last_boundary:i]
                body = clean[i:close + 1]
                owner = None
                enclosing_classes = [s[1] for s in stack if s[0] == "class"]
                named_namespaces = [s[1] for s in stack if s[0] == "namespace" and s[1] and s[1] != "KujataEngine"]
                paren = signature.rfind("(")
                head = re.sub(r"\s+", " ", signature[:paren + 1]) if paren >= 0 else ""
                m = FUNC_SIG_RE.search(head)
                if enclosing_classes:
                    owner = ("class", "::".join(enclosing_classes))
                elif m:
                    qualifier = m.group(1).split("::")[-1]
                    owner = ("class", qualifier) if qualifier in simple_to_ids else ("namespace", qualifier)
                elif named_namespaces and paren >= 0:
                    owner = ("namespace", "::".join(named_namespaces))
                if owner:
                    kind, name = owner
                    if kind == "class":
                        ids = [cid for cid in simple_to_ids.get(name.split("::")[-1], []) if classes[cid]["qualified"].endswith(name)]
                        if len(ids) > 1:
                            same_file = [cid for cid in ids if classes[cid]["file"] == str(path.relative_to(REPO)).replace("\\", "/")]
                            ids = same_file or ids
                        if ids:
                            code_regions[ids[0]].append(body)
                    else:
                        # "KujataEngine::InspectorUI" と "InspectorUI" を同じものとして扱う。detail は内部用なので数えない。
                        name = name.removeprefix("KujataEngine::")
                        ns_id = "ns:" + name
                        if name not in simple_to_ids and name != "detail":
                            namespace_nodes.setdefault(ns_id, {
                                "id": ns_id, "name": name, "qualified": name, "kind": "namespace",
                                "module": module_of(path), "file": str(path.relative_to(REPO)).replace("\\", "/"),
                                "line": line_of(clean, i), "bases": [], "outer": None, "doc": "", "tags": [],
                            })
                            code_regions[ns_id].append(body)
                i = close + 1
                last_boundary = i
                continue
            if ch == "}":
                if stack:
                    stack.pop()
                last_boundary = i + 1
            elif ch == ";":
                last_boundary = i + 1
            i += 1

    # 名前空間の説明: "namespace 名前 {" の直前のコメント(ヘッダを優先。無ければ .cpp)
    for info in namespace_nodes.values():
        pattern = re.compile(r"namespace\s+(?:KujataEngine::)?" + re.escape(info["name"]) + r"\s*\{")
        for path in sorted(raws, key=lambda p: p.suffix != ".h"):
            m = pattern.search(raws[path])
            if m:
                doc = doc_above(raws[path], m.start())
                if doc:
                    info["doc"] = doc
                    info["file"] = str(path.relative_to(REPO)).replace("\\", "/")
                    info["line"] = line_of(raws[path], m.start())
                    break

    # 関数が 1 つも見つからなかった名前空間(型の宣言だけ等)は除く
    nodes = dict(classes)
    for ns_id, info in namespace_nodes.items():
        if code_regions.get(ns_id):
            nodes[ns_id] = info

    def resolve_name(name, context_id):
        """型名を node id へ。入れ子の型は、使っている側の外側のクラスから探す。"""
        candidates = simple_to_ids.get(name, [])
        if not candidates:
            return None
        if len(candidates) == 1:
            if name in GENERIC_NAMES and classes[candidates[0]]["outer"] and not (context_id or "").startswith(classes[candidates[0]]["outer"]):
                return None
            return candidates[0]
        if context_id:
            context_qualified = nodes[context_id]["qualified"] if context_id in nodes else ""
            for cid in candidates:
                outer = classes[cid]["outer"]
                if outer and (context_qualified == outer or context_qualified.startswith(outer + "::")):
                    return cid
            for cid in candidates:
                if classes[cid]["outer"] is None:
                    return cid
        return None

    edges = {}

    def add_edge(source, target, kind):
        if not source or not target or source == target:
            return
        key = (source, target)
        # 同じ組に複数の関係があるときは強いほうを残す(inherits > owns > refers > uses)
        order = {"inherits": 0, "nested": 1, "owns": 2, "refers": 3, "uses": 4}
        if key not in edges or order[kind] < order[edges[key]]:
            edges[key] = kind

    identifier_re = re.compile(r"\b[A-Z][A-Za-z0-9_]*\b")

    for node_id, info in classes.items():
        # 継承
        for base in info["bases"]:
            base_name = re.sub(r"<.*", "", base).split("::")[-1].strip()
            add_edge(node_id, resolve_name(base_name, node_id), "inherits")

        # メンバ(クラスの本体の直下の宣言)
        clean, start, end = class_bodies[node_id]
        body = clean[start + 1:end]
        flat = []
        depth = 0
        for ch in body:
            if ch == "{":
                depth += 1
                if depth == 1:
                    flat.append(";")
                continue
            if ch == "}":
                depth -= 1
                continue
            if depth == 0:
                flat.append(ch)
        for statement in "".join(flat).split(";"):
            statement = re.sub(r"\b(public|protected|private)\s*:", "", statement).strip()
            if not statement or statement.startswith(("using ", "friend ", "typedef ", "static_assert", "enum ")):
                continue
            no_templates = re.sub(r"<[^<>]*>", "", re.sub(r"<[^<>]*>", "", re.sub(r"<[^<>]*>", "", statement)))
            if "(" in no_templates.split("=")[0]:
                continue  # 関数の宣言
            declaration = statement.split("=")[0].split("{")[0]
            for name in identifier_re.findall(declaration):
                target = resolve_name(name, node_id)
                if not target:
                    continue
                after = declaration[declaration.find(name) + len(name):]
                pointer_like = re.match(r"\s*(>\s*)*[*&]", after) is not None
                referring_wrappers = re.search(r"\b(ComponentRef|weak_ptr|shared_ptr)\s*<\s*" + name, declaration)
                owns_wrappers = re.search(r"\b(unique_ptr|vector|array|map|unordered_map|deque|optional|list)\s*<[^;]*\b" + name, declaration)
                if referring_wrappers or (pointer_like and not re.search(r"unique_ptr\s*<\s*" + name, declaration)):
                    add_edge(node_id, target, "refers")
                elif owns_wrappers or not pointer_like:
                    add_edge(node_id, target, "owns")

    # 使っている(関数の本体・クラス内のインライン関数)
    for node_id in nodes:
        regions = list(code_regions.get(node_id, []))
        if node_id in class_bodies:
            clean, start, end = class_bodies[node_id]
            body = clean[start + 1:end]
            # インライン関数の本体だけを拾う(メンバ宣言は上で扱った)
            depth = 0
            buffer = []
            for ch in body:
                if ch == "{":
                    depth += 1
                if depth > 0:
                    buffer.append(ch)
                if ch == "}":
                    depth -= 1
            regions.append("".join(buffer))
        seen = set()
        for region in regions:
            for name in identifier_re.findall(region):
                if name in seen:
                    continue
                seen.add(name)
                add_edge(node_id, resolve_name(name, node_id), "uses")

    # 入れ子の型は、外側のクラスとの関係(nested)も持たせる(図で親子が分かるように)
    for node_id, info in classes.items():
        if info["outer"]:
            outer_candidates = [cid for cid in classes if classes[cid]["qualified"] == info["outer"] and classes[cid]["file"] == info["file"]]
            if outer_candidates:
                edges[(outer_candidates[0], node_id)] = "nested"

    data = {
        "generatedFrom": [str(r.relative_to(REPO)) for r in SOURCE_ROOTS],
        "nodes": sorted(nodes.values(), key=lambda n: (n["module"], n["qualified"])),
        "edges": [{"source": s, "target": t, "type": k} for (s, t), k in sorted(edges.items())],
    }
    OUTPUT.write_text(json.dumps(data, ensure_ascii=False, indent=1), encoding="utf-8")

    counts = defaultdict(int)
    for e in data["edges"]:
        counts[e["type"]] += 1
    modules = defaultdict(int)
    for n in data["nodes"]:
        modules[n["module"]] += 1
    print(f"nodes: {len(data['nodes'])}  (classes {len(classes)}, namespaces {len(data['nodes']) - len(classes)})")
    print("edges:", dict(counts))
    print("modules:", dict(sorted(modules.items())))


if __name__ == "__main__":
    main()
