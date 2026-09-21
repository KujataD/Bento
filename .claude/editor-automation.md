# エディタ操作の CUI 化 — 設計

AI(Claude Code 等)と人間の両方が、同じコマンドでエディタを操作・確認できるようにするための設計文書。

- 対象: KujataEngine のエディタ(`KujataEngine/Editor/`)と、起動・ログまわり。
- 状態: **Step 1(CUI の土台)を実装済み。** Step 2 以降は未着手(§6)。
- 関連: [CLAUDE.md](CLAUDE.md) の「現在の方針」②(決定論+リプレイ)、[determinism.md](determinism.md) の Step 4(入力の記録・再生)

---

## 1. なぜ必要か

今のエディタは「人間が画面を見てマウスで操作する」ことしか想定していない。
AI に作業させると、次のところで詰まる(2026-09 の作業で実際に起きたこと)。

| 困りごと | 今の回避策 | 問題 |
|---|---|---|
| オブジェクトを選べない | スクリーンショットから座標を割り出してクリック | レイアウトが変わると同じ座標では押せない |
| 結果を確かめられない | ウィンドウを撮影して画像を目で見る | 値の確認ができない。見落としやすい |
| キーが押せない | OS へのキー注入 | Release では届かない |
| ログが読めない | ファイルの `logs/` だけ | エディタの Console の中身は外から読めない |
| 起動確認が手作業 | 起動 → 何秒か待つ → 強制終了 を毎回組み立てる | 待ち時間の勘に頼る。失敗を終了コードで判定できない |

人間にとっても似た不便がある: Undo の履歴に何をしたかが出ない、Console を絞り込めない、
シーン JSON の git 差分が ID の揺れで読みにくい、不具合を他人の PC で再現できない。

## 2. なぜバッチではなく CUI か

最初は「コマンドライン引数で起動して、処理を流して終了する」バッチ実行を中心に考えたが、やめた。
バッチでは**エディタ側の問題を見落とす**。

- **毎回まっさらな状態から始まる。** 選択の食い違い、Undo の積み方、ホットリロード後に残る古い状態、
  Play/Stop の繰り返しによる状態の持ち越しなど、**使い続けて初めて出る不具合**が再現しない。
- **エディタの処理を通らない近道ができやすい。** バッチ専用の経路を作ると、人間が通る処理と別物になり、
  「バッチでは通るのにエディタでは壊れる」が起きる。
- **様子を見て次の手を決められない。** AI は「状態を見る → 操作する → 結果を見る」を繰り返して作業する。一方的に流すだけでは合わない。

そこで、**起動中のエディタに 1 つずつコマンドを送り、結果を受け取る CUI** を中心にする。
バッチは「起動したエディタに、同じコマンドを順に流すだけ」のものとして残す(まっさらな状態からの再現確認に使う)。

## 3. 構成

```
 起動中のエディタ(UI も普段どおり動いている)
   └ コマンド層 EditorCommandRegistry
        └ 実行はメインスレッドのフレームの頭(EditorApplication::Update の先頭)。
          人間の UI 操作と同じ処理(EditorApplication / EditorUndoManager / SceneJsonImporter)を呼ぶ
        ↑ EditorCommandServer(受付の窓口。1 件ずつ順に実行する)
 ├ Console の入力欄 : エディタの Console ウィンドウで直接打つ(人間)
 ├ kujata CLI      : ターミナルから Tools/kujata.cmd <コマンド>。引数なしで対話モード(人間・AI)
 │                   標準入力をつなぐと 1 行 1 コマンドで順に実行(引用符が崩れないので AI はこちら)
 │                   名前付きパイプ \\.\pipe\KujataEditor 経由
 ├ MCP サーバー    : (未実装)Claude Code から同じコマンドをツールとして呼ぶ
 └ スクリプト実行  : --run <ファイル> [--exit] = 起動したエディタに同じコマンドを 1 行ずつ流す
```

**人間も AI も同じ CUI を使う。** 人間は Console の入力欄か `kujata` の対話モード、AI は `kujata <コマンド>` を 1 回ずつ呼ぶ。
どちらも同じコマンド層を通るので、名前・結果・エラー文が揃う。

### 守る決まり

1. **名前は 1 つに揃える。** Inspector の表示名・JSON のキー・コマンドで使う名前を同じにする。
   人間が「MonsterBall の Rotator の speed を上げて」と言えば、そのまま AI のコマンドになる。
2. **オブジェクトはパスでも指定できる。** instanceId(`go_...`)に加えて `Stage/Enemies/Guardian` のような階層パスを受け付ける。
   同名が複数あるときは**曖昧だとエラーを返す**(黙って先頭を選ばない)。
3. **失敗の理由は文章で返す。** 人間が読んで直せる文にする。
4. **変更は必ず Undo できる。** 変更系のコマンドは実行前に Undo のスナップショットを取り、ラベルに `[CUI]` と中身を入れる。
5. **エディタの様子を毎回返す。** 返り値に「そのコマンドの実行中に出たログ」と「実行後のエディタの状態」を必ず含める(§4)。
   これで AI も人間も、1 手ごとにエディタ側で何か起きていないかを確かめられる。
6. **エディタの機能もコマンドとして公開する。** UI の部品には描画と入力の受け取りだけを残し、中の処理はコマンドに寄せる
   (例: ギズモはドラッグ中の見た目だけを持ち、手を離した時点で `field.set` 相当の処理を 1 回呼ぶ)。今あるものは少しずつ移す。

## 4. コマンドの形と返り値

### 入力

1 行のテキスト。人間が打つ形と AI が送る形は同じ。

```
field.set MonsterBall RotatorComponent speed 0.05
object.get "Stage/Enemies/Guardian"
wait 60
```

- 空白で区切る。空白を含む名前は `"..."` で囲む。
- `field.set` の値は残りの部分を JSON として読む(`0.05` / `true` / `"文字列"` / `[1, 2, 3]`)。JSON として読めなければ文字列として扱う。
- コンポーネントは型名で指定する。同じ型が複数あるときは `ColliderComponent#1` のように番号を付ける(0 始まり)。

### 返り値(JSON 1 行)

```json
{
  "ok": true,
  "command": "field.set MonsterBall RotatorComponent speed 0.05",
  "result": { "object": "MonsterBall", "component": "RotatorComponent", "key": "speed", "value": 0.05 },
  "error": "",
  "logs": ["...このコマンドの実行中に Console に出たログ..."],
  "state": { "mode": "Edit", "scene": "SampleScene", "selection": "MonsterBall", "undoTop": "[CUI] field.set MonsterBall/RotatorComponent.speed", "frame": 1234 }
}
```

返事の `text` は人間向けに整形した結果(Console と `kujata` はこれを表示する)。
`kujata -Json` は返事の JSON をそのまま出す(AI・スクリプト向け)。失敗すると終了コード 1。

## 5. コマンド一覧(Step 1 で実装したもの)

| 分類 | コマンド | 内容 |
|---|---|---|
| 案内 | `help [コマンド]` | コマンド一覧・使い方 |
| 参照 | `state` | エディタの状態(モード・シーン・選択・Undo の先頭・フレーム番号) |
| | `scene.list` | 全オブジェクトのパス・instanceId・有効/無効・コンポーネント |
| | `object.get <オブジェクト>` | コンポーネントごとの全フィールド値 |
| | `component.types` | 追加できるコンポーネントの型名 |
| | `log.tail [件数]` | Console の最近のログ |
| 選択 | `select <オブジェクト>` / `select none` | Hierarchy の選択を変える |
| 編集 | `object.create <名前> [親]` | 空のオブジェクトを作る |
| | `object.delete <オブジェクト>` | 子ごと削除する |
| | `object.rename <オブジェクト> <新しい名前>` | 名前を変える |
| | `object.active <オブジェクト> <true/false>` | 有効/無効を切り替える |
| | `component.add <オブジェクト> <型名>` | コンポーネントを足す |
| | `component.remove <オブジェクト> <型名[#番号]>` | コンポーネントを外す |
| | `field.set <オブジェクト> <型名[#番号]> <キー> <値>` | フィールドを書き換える(キー `enabled` でコンポーネントの有効/無効) |
| | `undo` / `redo` | Undo / Redo |
| 実行 | `play.start` / `play.stop` | Play / Stop |
| | `wait <フレーム数>` | 指定フレーム進むのを待ってから返す(Play 中の変化を見るため。待っている間のログも返す) |
| | `scene.save` | シーンを保存する(Ctrl+S と同じ) |
| | `module.reload` | GameModule をビルドし直して差し替える(Reload DLL と同じ) |
| | `quit` | エディタを終了する |

- 編集系のコマンドは Play 中も使える(Inspector と同じ。Stop すると Play 前の状態に戻る)。
- `field.set` は、シーンの JSON の該当フィールドだけを書き換えて Undo と同じ経路(`SceneJsonImporter::ApplySceneJsonString`)で適用する。
  参照フィールド(ObjectRef)も JSON 上の instanceId として書き換えられる。

## 6. これからの Step

| Step | 内容 |
|---|---|
| 1(済) | コマンド層、名前付きパイプ、Console の入力欄、`kujata` CLI、`--run` / `--exit` |
|  | 実装中に CUI で見つかって直した既存の不具合: 折りたたまれた親の子を選ぶと Hierarchy が選択を外す / Windows のメッセージを 1 フレーム 1 件しか処理せず入力が遅れる / 最初の編集の Undo ラベルが "Initial" のまま |
| 2 | `view.screenshot`(Scene/Game ビューの描画結果を PNG に書き出す)、`state.dump`(シーン状態の書き出し)、ログの JSON Lines 出力と Console の絞り込み |
| 3 | エディタ独自の機能のコマンド化: `prefab.open` / `prefab.apply` / `prefab.revert`、`animation.addKey` など。UI 側の処理をコマンドへ寄せる |
| 4 | 型情報: `SerializedFieldRegistry` に `Mode::DescribeSchema` を足し、`schema.get` でフィールドの型・範囲・説明を返す。Inspector のツールチップ・範囲チェックも同じ情報から出す |
| 5 | MCP サーバー(`kujata` と同じパイプを使う)、コマンドパレット(Ctrl+P)、Undo の履歴ウィンドウ、CUI で変えたオブジェクトの強調表示 |
| 6 | 固定 ID(今は `GenerateInstanceId()` が時刻と乱数で作るので差分がぶれる)と入力の記録・再生。決定論(determinism.md)と一緒に進める |

### CUI でも確かめられないもの

- **UI の部品そのものの不具合**(ボタンの当たり判定、ドラッグ、ドッキング)はコマンドを通らないので見つからない。
  Dear ImGui Test Engine(ウィジェットのラベルで UI を操作する)を検討する。**ライセンスが ImGui 本体と別**なので事前に確認が要る。
- **使い心地・見た目**は人間が判断する。CUI で「壊れていない」ことを確かめてから人間に渡す。

## 7. 決めていないこと

- MCP サーバーを何で書くか(PowerShell / Python / C++)。
- 複数のエディタを同時に起動したときのパイプ名(今は固定の 1 本なので、2 つ目のエディタは CUI を受け付けない)。
- 配布ビルド(エディタ UI なし)で CUI をどこまで使えるようにするか(今はエディタのビルドだけ)。
