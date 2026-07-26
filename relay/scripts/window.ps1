# 声で PC を操作する（#219）のウィンドウ操作。relay の executor.ts から
# `powershell.exe -NoProfile -NonInteractive -File window.ps1 -ProcessName x -Action minimize`
# の形で呼ばれる。**呼び出し側は文字列を連結しない**（-File + パラメータ束縛なので、
# 渡された値はコードとして解釈されず、常に文字列の値として入る）。
#
# 終了コード: 0=成功 / 3=対象の窓が無い（＝失敗ではなく状態） / それ以外=失敗
#
# ⚠ このファイルは **UTF-8 BOM 付き**で保存すること。
#   Windows PowerShell 5.1 は BOM の無いファイルを ANSI(cp932) として読むため、
#   日本語コメントが文字化けし、その場で構文エラーになる（実測で確認済み）。
[CmdletBinding()]
param(
    # 拡張子なしのプロセス名。executor 側で検証済みだが、ここでも同じ形を要求する
    # （このスクリプトを手で叩いた時にも同じ防壁が効くように）。
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9 ._-]{0,63}$')]
    [string]$ProcessName,

    # apps.json に登録された実行パス。Get-Process -Name は**ベース名一致**なので、
    # これで「登録した実行ファイルそのもの」まで絞り込む
    # （同名の別プロセスの窓を巻き添えで動かさないため）。
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$ExePath,

    # ValidateSet に無い値は PowerShell が束縛の時点で弾く（実行前に落ちる）。
    [Parameter(Mandatory = $true)]
    [ValidateSet('minimize', 'maximize', 'restore', 'focus')]
    [string]$Action
)

$ErrorActionPreference = 'Stop'

# ウィンドウの表示状態は .NET から直接触れないので user32.dll を P/Invoke で借りる。
# ShowWindow の nCmdShow: 3=最大化 / 6=最小化 / 9=元に戻す（最小化からの復帰を含む）。
$signature = @'
[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
'@

try {
    Add-Type -MemberDefinition $signature -Name 'Native' -Namespace 'RelayWin32' | Out-Null
}
catch {
    # 既に読み込み済みなら型の再定義で失敗する。その場合は続行してよい。
    if (-not ('RelayWin32.Native' -as [type])) {
        Write-Error "user32 の読み込みに失敗: $($_.Exception.Message)"
        exit 4
    }
}

$SHOW_WINDOW = @{
    minimize = 6
    maximize = 3
    restore  = 9
    focus    = 9  # 最小化されていても前に出せるよう、まず復帰させてから前面化する
}

# MainWindowHandle が 0 のプロセス（バックグラウンド・別セッション）は対象外。
$candidates = @(
    Get-Process -Name $ProcessName -ErrorAction SilentlyContinue |
    Where-Object { $_.MainWindowHandle -ne 0 }
)

if ($candidates.Count -eq 0) {
    exit 3
}

# 実行パスが一致するものがあれば、そこまで絞る（同名の別プロセスを巻き添えにしない）。
# $_.Path は保護されたプロセスで例外になり得るので握る。
$exact = @(
    $candidates | Where-Object {
        $path = $null
        try { $path = $_.Path } catch { $path = $null }
        $path -ieq $ExePath
    }
)

# ⚠ 一致が 0 でも、候補が**ちょうど 1 つ**なら名前一致で操作する。
# Windows 11 の notepad.exe のように、**起動したパスと実プロセスのパスが違う**アプリがある
# （Store 版へリダイレクトされ、実体は WindowsApps\...\Notepad.exe になる。実測で確認済み）。
# 厳密一致だけにすると、そういうアプリのウィンドウ操作が永久に効かなくなる。
#
# 候補が複数ある時にフォールバックしないのは、「登録した実行パスと一致するものが 1 つも無い」
# ＝同一性が最も怪しい状況で、最も広く手を出すことになるため。
$fallback = $false
if ($exact.Count -gt 0) {
    $targets = $exact
}
elseif ($candidates.Count -eq 1) {
    $targets = $candidates
    $fallback = $true
}
else {
    exit 3
}

foreach ($p in $targets) {
    [RelayWin32.Native]::ShowWindow($p.MainWindowHandle, $SHOW_WINDOW[$Action]) | Out-Null
    if ($Action -eq 'focus') {
        # 前面化は OS 側の前面ロックで拒否されることがある（他アプリ操作中など）。
        # 最小化からの復帰までは効いているので、ここは best effort として成功扱いにする。
        [RelayWin32.Native]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
    }
}

# 名前一致で操作した場合は 5 を返す。呼び出し側は成功として扱うが、
# 「登録した実行ファイルそのものだと確認できていない」ことを監査ログに残す。
if ($fallback) {
    exit 5
}

exit 0
