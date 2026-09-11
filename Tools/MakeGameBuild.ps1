# 遊んでもらうための配布フォルダを作る。
#
# 課題提出用の 1_ExeFile(エディタ操作前提・Debug)とは別物で、こちらは
# 「VSの入っていないPCへzipで渡して、そのまま遊べる」形にする。
#
# 要点:
#   - Release構成を配る。DebugのCRT(MSVCP140D.dll等)は**再頒布不可**で、
#     VSの入っていないPCでは起動しない。
#   - Data(DirectXGame/Data)と EngineData(KujataEngine/EngineData)は **exeの隣**へ置く。
#     配布先には KujataEngine.sln も DirectXGame/ も無いので、エンジンとプロジェクトの起点は
#     どちらもカレント(exeの隣)になる。
#   - StartupScene は TitleScene にする(配布物はタイトルから始まる)。
#
# 使い方:  powershell -ExecutionPolicy Bypass -File Tools\MakeGameBuild.ps1
#
# **このファイルは UTF-8 BOM付きで保存すること。**
# Windows PowerShell 5.1 は BOM 無しを CP932 として読み、日本語コメントでパースエラーになる。

param(
    [string]$Destination = "C:\Users\muto\source\repos\KujataD\GameBuild",
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent $PSScriptRoot
$binDir = Join-Path $repo "build\bin\$Configuration"
$moduleDll = Join-Path $repo "DirectXGame\GameModule\bin\$Configuration\GameModule.dll"
$dataSrc = Join-Path $repo "DirectXGame\Data"
$engineDataSrc = Join-Path $repo "KujataEngine\EngineData"

# exe名は DirectXGame/Game.props の KujataExeName で決まるので、決め打ちせず bin から探す。
$exe = Get-ChildItem $binDir -Filter *.exe -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $exe) {
    throw "exe が無い: $binDir  先に $Configuration をビルドすること"
}
if (-not (Test-Path $moduleDll)) {
    throw "GameModule.dll が無い: $moduleDll  先に $Configuration をビルドすること"
}

# --- 出力先を作り直す(古い Data が混ざると原因不明の不具合になる) ---
if (Test-Path $Destination) {
    Remove-Item $Destination -Recurse -Force
}
New-Item -ItemType Directory -Path $Destination | Out-Null

# --- 実行ファイル一式 ---
Copy-Item $exe.FullName $Destination
Copy-Item $moduleDll $Destination
foreach ($dll in @("dxcompiler.dll", "dxil.dll")) {
    $src = Join-Path $binDir $dll
    if (Test-Path $src) { Copy-Item $src $Destination }
}

# --- 再頒布可能なCRT(アプリローカル配置。インストール不要にするため) ---
$redistRoot = Join-Path ${env:ProgramFiles} "Microsoft Visual Studio\18\Community\VC\Redist\MSVC"
if (-not (Test-Path $redistRoot)) {
    $redistRoot = Join-Path ${env:ProgramFiles} "Microsoft Visual Studio\2022\Community\VC\Redist\MSVC"
}
if (Test-Path $redistRoot) {
    $version = Get-ChildItem $redistRoot -Directory |
        Where-Object { $_.Name -match '^\d' } |
        Sort-Object Name -Descending |
        Select-Object -First 1
    # onecore と debug_nonredist は配ってはいけない。
    $crtDir = Get-ChildItem (Join-Path $version.FullName "x64") -Directory |
        Where-Object { $_.Name -like "Microsoft.VC*.CRT" } |
        Select-Object -First 1
    if ($crtDir) {
        Get-ChildItem $crtDir.FullName -Filter *.dll | ForEach-Object { Copy-Item $_.FullName $Destination }
        Write-Host "CRT: $($crtDir.FullName)"
    }
} else {
    Write-Warning "再頒布可能なCRTが見つからない。配布先にVCランタイムが必要になる"
}

# --- EngineData(シェーダーと既定テクスチャ) ---
robocopy $engineDataSrc (Join-Path $Destination "EngineData") /E /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "EngineData のコピーに失敗した (robocopy=$LASTEXITCODE)" }

# --- Data(作業用の産物は持って行かない) ---
$dataDst = Join-Path $Destination "Data"
$exclude = @("logs", "Temp")
robocopy $dataSrc $dataDst /E /NFL /NDL /NJH /NJS /NP /XD $exclude /XF "*.log" | Out-Null
if ($LASTEXITCODE -ge 8) { throw "Data のコピーに失敗した (robocopy=$LASTEXITCODE)" }

# --- 配布物はタイトルから始める ---
$startup = Join-Path $dataDst "ProjectSettings\StartupScene.txt"
[System.IO.File]::WriteAllText($startup, "TitleScene", (New-Object System.Text.UTF8Encoding($false)))

$size = (Get-ChildItem $Destination -Recurse -File | Measure-Object -Property Length -Sum).Sum
Write-Host ""
Write-Host "出力: $Destination"
Write-Host ("ファイル数: {0}  合計: {1:N1} MB" -f (Get-ChildItem $Destination -Recurse -File).Count, ($size / 1MB))
Write-Host "起動確認: $(Join-Path $Destination $exe.Name)"
