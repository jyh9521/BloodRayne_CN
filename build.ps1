param(
    [string]$GameDir = ""
)

$ErrorActionPreference = "Stop"
$Repo = Split-Path -Parent $MyInvocation.MyCommand.Path
$Tools = Join-Path $Repo "tools"
$SrcTool = Join-Path $Repo "src\tool"
$Trans = Join-Path $Repo "translations"
$Dist = Join-Path $Repo "dist"
$Work = Join-Path $Dist "_work"
$Out = Join-Path $Dist "BloodRayne_CN_Build"
$PrebuiltTool = Join-Path $Repo "prebuilt\BloodRayneCNTool.exe"

function Find-GameDir {
    $candidates = @(
        $GameDir,
        "C:\Program Files (x86)\Steam\steamapps\common\BloodRayne Terminal Cut",
        "C:\Program Files\Steam\steamapps\common\BloodRayne Terminal Cut"
    ) | Where-Object { $_ -and $_.Trim() }
    foreach ($c in $candidates) {
        if ((Test-Path -LiteralPath (Join-Path $c "rayne1.exe")) -and
            (Test-Path -LiteralPath (Join-Path $c "ENGLISH.POD")) -and
            (Test-Path -LiteralPath (Join-Path $c "JAPANESE.POD")) -and
            (Test-Path -LiteralPath (Join-Path $c "WORLD.POD")) -and
            (Test-Path -LiteralPath (Join-Path $c "PCART.POD"))) {
            return (Resolve-Path -LiteralPath $c).Path
        }
    }
    throw "未找到游戏目录。请运行：powershell -ExecutionPolicy Bypass -File .\build.ps1 -GameDir `"X:\...\BloodRayne Terminal Cut`""
}

function Require-Command($Name, $Hint) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "缺少命令：$Name。$Hint"
    }
}

function Invoke-VcBuild {
    $vcvars = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    if (-not (Test-Path -LiteralPath $vcvars)) {
        throw "未找到 Visual Studio 2022 Build Tools：$vcvars"
    }
    $cmd = @"
call "$vcvars" >nul
cd /d "$Repo"
if not exist "$Work\tool" mkdir "$Work\tool"
rc /nologo /fo "$Work\tool\release_tool.res" "$SrcTool\release_tool.rc"
cl /nologo /O2 /MT /utf-8 /DUNICODE /D_UNICODE "$SrcTool\release_tool.c" "$SrcTool\font_patch.c" "$Work\tool\release_tool.res" /Fe:"$Work\tool\BloodRayneCNTool.exe" /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comdlg32.lib comctl32.lib advapi32.lib shell32.lib ole32.lib
"@
    & cmd.exe /c $cmd
    $tool = Join-Path $Work "tool\BloodRayneCNTool.exe"
    if (($LASTEXITCODE -ne 0) -or (-not (Test-Path -LiteralPath $tool))) {
        throw "工具编译失败或没有生成 exe。"
    }
    return $tool
}

function Get-ToolExe {
    try {
        return Invoke-VcBuild
    } catch {
        if (Test-Path -LiteralPath $PrebuiltTool) {
            Write-Warning "无法在本机编译工具，改用 prebuilt\BloodRayneCNTool.exe。原因：$($_.Exception.Message)"
            return $PrebuiltTool
        }
        throw "无法编译工具，且缺少预编译文件：$PrebuiltTool"
    }
}

function Invoke-Python([string[]]$Arguments) {
    & python @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Python 构建步骤失败：python $($Arguments -join ' ')" }
}

function Remove-BuildDir($Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return }
    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $distRoot = (Resolve-Path -LiteralPath $Dist).Path
    if (-not $resolved.StartsWith($distRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "拒绝清理非 dist 目录：$resolved"
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}

$Game = Find-GameDir
$JapanesePod = Join-Path $Game "JAPANESE.POD"
if ((Get-Item -LiteralPath $JapanesePod).Length -gt 1000000) {
    throw "当前 JAPANESE.POD 看起来不是原版日文包。请先通过 Steam 验证游戏文件完整性，还原干净游戏目录后再构建。"
}
Require-Command python "请安装 Python 3，并确保 python 在 PATH 中。"
& python -c "import PIL" 2>$null
if ($LASTEXITCODE -ne 0) {
    throw "缺少 Pillow。请运行：python -m pip install pillow"
}

if (-not (Test-Path -LiteralPath $Dist)) { New-Item -ItemType Directory -Path $Dist | Out-Null }
Remove-BuildDir $Work
Remove-BuildDir $Out
New-Item -ItemType Directory -Path $Work, $Out | Out-Null

Write-Host "[1/6] 编译汉化工具"
$ToolExe = Get-ToolExe

Write-Host "[2/6] 构建英文基准文本 POD"
$EnOut = Join-Path $Work "text_en"
Invoke-Python @((Join-Path $Tools "build_release.py"), $Game, (Join-Path $Trans "text_en.tsv"), "--variation", "Medium", "--size", "19", "--out", $EnOut)

Write-Host "[3/6] 构建日文基准文本 POD"
$JpOut = Join-Path $Work "text_jp"
Invoke-Python @((Join-Path $Tools "build_release_from_japanese.py"), $Game, (Join-Path $Trans "text_jp.tsv"), "--variation", "Medium", "--size", "19", "--out", $JpOut)

Write-Host "[4/6] 修补 WORLD.POD 字幕触发"
Copy-Item -LiteralPath (Join-Path $Game "WORLD.POD") -Destination (Join-Path $Out "WORLD.POD") -Force
Invoke-Python @((Join-Path $Tools "patch_castle_subtitles.py"), $Out, "--apply")

Write-Host "[5/6] 整理发布目录"
New-Item -ItemType Directory -Path `
    (Join-Path $Out "zh_cn_tools\texts"), `
    (Join-Path $Out "zh_cn_tools\defaults\text_en"), `
    (Join-Path $Out "zh_cn_tools\defaults\text_jp"), `
    (Join-Path $Out "zh_cn_tools\variants\text_en"), `
    (Join-Path $Out "zh_cn_tools\variants\text_jp") | Out-Null
Copy-Item -LiteralPath (Join-Path $EnOut "JAPANESE.POD") -Destination (Join-Path $Out "JAPANESE.POD") -Force
Copy-Item -LiteralPath $ToolExe -Destination (Join-Path $Out "zh_cn_tools\BloodRayneCNTool.exe") -Force
Copy-Item -LiteralPath (Join-Path $Trans "text_en.tsv") -Destination (Join-Path $Out "zh_cn_tools\texts\text_en.tsv") -Force
Copy-Item -LiteralPath (Join-Path $Trans "text_jp.tsv") -Destination (Join-Path $Out "zh_cn_tools\texts\text_jp.tsv") -Force
foreach ($kind in @("defaults", "variants")) {
    Copy-Item -LiteralPath (Join-Path $EnOut "JAPANESE.POD") -Destination (Join-Path $Out "zh_cn_tools\$kind\text_en\JAPANESE.POD") -Force
    Copy-Item -LiteralPath (Join-Path $JpOut "JAPANESE.POD") -Destination (Join-Path $Out "zh_cn_tools\$kind\text_jp\JAPANESE.POD") -Force
}
Copy-Item -LiteralPath (Join-Path $EnOut "atlas.png") -Destination (Join-Path $Out "zh_cn_tools\variants\text_en\atlas.png") -Force
Copy-Item -LiteralPath (Join-Path $JpOut "atlas.png") -Destination (Join-Path $Out "zh_cn_tools\variants\text_jp\atlas.png") -Force

$Winmm = Join-Path $Repo "third_party\winmm.dll"
if (Test-Path -LiteralPath $Winmm) {
    Copy-Item -LiteralPath $Winmm -Destination (Join-Path $Out "winmm.dll") -Force
}

@"
BloodRayne Terminal Cut 简体中文汉化

安装：
1. 将本目录内容复制到游戏根目录覆盖。
2. 运行 zh_cn_tools\BloodRayneCNTool.exe，可切换文本/语音/视频字幕偏好，并启用 DPI/LAA 修复。

根目录 JAPANESE.POD 默认为“基于英文”文本。
硬字幕 BIK 视频不在源码仓库中生成到本目录；如需分发，请单独构建并作为可选包发布。
"@ | Set-Content -LiteralPath (Join-Path $Out "README_CN.txt") -Encoding UTF8

Write-Host "[6/6] 生成 SHA256SUMS.txt"
Get-ChildItem -LiteralPath $Out -Recurse -Force -File |
    Where-Object { $_.Name -ne "SHA256SUMS.txt" } |
    Sort-Object FullName |
    ForEach-Object {
        $rel = $_.FullName.Substring($Out.Length + 1)
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash
        "$hash  $rel"
    } | Set-Content -LiteralPath (Join-Path $Out "SHA256SUMS.txt") -Encoding ASCII

Write-Host ""
Write-Host "完成：$Out"
