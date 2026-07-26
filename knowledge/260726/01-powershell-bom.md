# .ps1 が「文字化けして構文エラー」になる（Windows PowerShell 5.1 と BOM）

## 何が起きたか

日本語コメント入りの `relay/scripts/window.ps1` を `powershell.exe -File` で実行したら、
コメントとは関係ない行でパースエラーが出た。

```
At window.ps1:18 char:1
+ [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr ...
+ ~~~~~~~~~~~~~~~~~~~~~~~~~
Unexpected attribute 'DllImport'.
...
The string is missing the terminator: '.
```

エラーメッセージの中の日本語コメントが `譌｢縺ｫ隱ｭ縺ｿ霎ｼ縺ｿ貂医∩` のように化けていた。

## 原因

**Windows PowerShell 5.1 は、BOM の無いファイルを ANSI（日本語環境では cp932）として読む。**

UTF-8 で書いた日本語コメントは cp932 として解釈されると別のバイト列に化ける。
その化けた結果にクォート記号相当のバイトが混ざると、**後続のコードのクォートの対応が崩れ**、
コメントとは無関係な行でパースエラーになる。今回は here-string の開始（`@'`）が
認識されず、中身の C# コードが PowerShell のコードとして解釈されていた。

つまり「日本語コメントを書いただけ」で構文が壊れる。エラーは化けた場所ではなく
**その先**に出るので、原因に見当がつきにくい。

## 対処

`.ps1` は **UTF-8 BOM 付き**で保存する。

```powershell
$p = "relay\scripts\window.ps1"
$c = Get-Content -Raw -Encoding UTF8 $p
Set-Content -Path $p -Value $c -Encoding UTF8BOM -NoNewline
```

確認（先頭 3 バイトが `EF BB BF`）:

```powershell
Format-Hex -Path relay\scripts\window.ps1 -Count 4
```

## 補足

- **PowerShell 7（`pwsh`）は既定が UTF-8** なので BOM 無しでも通る。
  この違いのせいで「手元の pwsh では動くのに relay から呼ぶと落ちる」が起きる。
  relay が `powershell.exe`（5.1）を既定にしているのは、Windows に必ず入っているため。
- Claude Code の `Write` ツールは BOM を付けずに書くので、`.ps1` を新規作成した後は
  毎回この変換が要る。ファイル冒頭にもその旨をコメントで残してある。
- 同じ理屈で、cp932 に無い文字（絵文字・`—` など）を含む BOM 無し `.ps1` も壊れ得る。

## 関連

- #219（声で PC を操作する実行アダプタ）
