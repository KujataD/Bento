@echo off
rem 起動中の KujataEngine エディタへ CUI コマンドを送る。使い方は kujata help(引数なしで対話モード)。
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0kujata.ps1" %*
