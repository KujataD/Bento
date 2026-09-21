<#
.SYNOPSIS
    起動中の KujataEngine エディタへ CUI コマンドを送る(人間・AI 共通)。

.DESCRIPTION
    エディタの Console の入力欄と同じコマンドを、ターミナルから実行する。
    コマンドの一覧は `kujata help`。設計は .claude/editor-automation.md。

    引数なしで起動すると対話モードになる(exit で抜ける)。
    標準入力をつなぐと、1 行 1 コマンドとして順に実行する(空白や引用符を含む引数はこちらが確実)。
    エディタとは名前付きパイプ \\.\pipe\KujataEditor でつながる。

.PARAMETER Json
    返事の JSON をそのまま 1 行で出す(AI・スクリプト向け)。

.PARAMETER TimeoutMs
    エディタへの接続を待つ時間(ミリ秒)。wait コマンドの待ち時間とは関係ない。

.EXAMPLE
    kujata scene.list
    kujata field.set MonsterBall RotatorComponent speed 0.05
    kujata 'object.rename MonsterBall "Monster Ball"'
    kujata -Json state
    echo 'object.create "Test Object" Stage' | kujata -Json
    kujata
#>
# -Json / -TimeoutMs は名前付きでだけ受け取る(コマンドの 1 語目を TimeoutMs と取り違えないように)。
[CmdletBinding(PositionalBinding = $false)]
param(
    [switch]$Json,
    [int]$TimeoutMs = 3000,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Command
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding $false

# 引数を 1 行のコマンドに戻す。空白を含む引数は "..." で囲み直す。
# 1 つの文字列で渡された場合(例: kujata 'field.set A B name "x"')はそのまま使う。
function Join-CommandLine([string[]]$Parts) {
    if ($Parts.Count -eq 1) {
        return $Parts[0]
    }
    $quoted = foreach ($part in $Parts) {
        if ($part -match '\s') { '"' + $part + '"' } else { $part }
    }
    return ($quoted -join ' ')
}

# コマンドを 1 行送り、返事(JSON 1 行)を受け取る。1 コマンドごとに接続し直す
# (対話モードで接続を握り続けると、他のクライアント(AI など)がつなげなくなるため)。
function Invoke-EditorCommand([string]$Line) {
    $pipe = New-Object System.IO.Pipes.NamedPipeClientStream('.', 'KujataEditor', [System.IO.Pipes.PipeDirection]::InOut)
    try {
        try {
            $pipe.Connect($TimeoutMs)
        } catch [System.TimeoutException] {
            throw "エディタにつながりません。KujataEngine のエディタが起動しているか確認してください(\\.\pipe\KujataEditor)。"
        }
        $utf8 = New-Object System.Text.UTF8Encoding $false
        $writer = New-Object System.IO.StreamWriter($pipe, $utf8)
        $writer.AutoFlush = $true
        $reader = New-Object System.IO.StreamReader($pipe, $utf8)
        $writer.WriteLine($Line)
        return $reader.ReadLine()
    } finally {
        $pipe.Dispose()
    }
}

# 人間向けの表示。結果の本文は エディタが作った text をそのまま出す(日本語が崩れないように)。
function Show-Response([string]$Raw) {
    $response = $Raw | ConvertFrom-Json
    foreach ($log in $response.logs) {
        Write-Host "  | $log" -ForegroundColor DarkGray
    }
    if ($response.ok) {
        Write-Host $response.text
    } else {
        Write-Host $response.text -ForegroundColor Red
    }
    $state = $response.state
    $selection = if ($state.selection) { $state.selection } else { '(なし)' }
    $undo = if ($state.undoTop) { $state.undoTop } else { '(なし)' }
    Write-Host ("[{0}] scene={1}  選択={2}  Undo={3}" -f $state.mode, $state.scene, $selection, $undo) -ForegroundColor DarkCyan
    return [bool]$response.ok
}

function Invoke-AndShow([string]$Line) {
    $raw = Invoke-EditorCommand $Line
    if ($null -eq $raw) {
        throw "エディタから返事がありませんでした(エディタが終了した可能性があります)。"
    }
    if ($Json) {
        # 戻り値(成否)と混ざらないよう、JSON は標準出力へ直接書く。
        [Console]::Out.WriteLine($raw)
        return [bool](($raw | ConvertFrom-Json).ok)
    }
    return (Show-Response $raw)
}

if ((-not $Command -or $Command.Count -eq 0) -and [Console]::IsInputRedirected) {
    # 標準入力からコマンドを 1 行ずつ読んで実行する。引用符をシェルに崩されないので、AI・スクリプトからはこれが確実。
    #   例: echo 'object.create "Test Object" Stage' | kujata -Json
    $failed = $false
    foreach ($line in [Console]::In.ReadToEnd() -split "`r?`n") {
        $line = $line.Trim()
        if ($line -eq '' -or $line.StartsWith('#')) { continue }
        try {
            if (-not (Invoke-AndShow $line)) { $failed = $true }
        } catch {
            Write-Host $_.Exception.Message -ForegroundColor Red
            exit 2
        }
    }
    if ($failed) { exit 1 } else { exit 0 }
}

if ($Command -and $Command.Count -gt 0) {
    # 1 回だけ実行する。失敗したら終了コード 1。
    try {
        $ok = Invoke-AndShow (Join-CommandLine $Command)
    } catch {
        Write-Host $_.Exception.Message -ForegroundColor Red
        exit 2
    }
    if ($ok) { exit 0 } else { exit 1 }
}

# 対話モード
Write-Host 'KujataEngine エディタの CUI です。help でコマンド一覧、exit で終了します。' -ForegroundColor DarkCyan
while ($true) {
    $line = Read-Host 'kujata'
    if ($null -eq $line) { break }
    $line = $line.Trim()
    if ($line -eq '') { continue }
    if ($line -eq 'exit') { break }
    try {
        Invoke-AndShow $line | Out-Null
    } catch {
        Write-Host $_.Exception.Message -ForegroundColor Red
    }
}
