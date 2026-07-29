# BloodRayne Terminal Cut 简体中文汉化

这是《BloodRayne: Terminal Cut》简体中文汉化的源码包。目标是：本地删除项目工作文件后，只要有一份干净的游戏安装目录，就能从 GitHub 克隆本仓库并重新构建可发布的汉化目录。

## 构建要求

- Windows 10/11
- Steam 版 `BloodRayne Terminal Cut`
- Python 3，且 `python` 在 `PATH` 中
- Python 包：`Pillow`
- 可选：Visual Studio 2022 Build Tools，安装 C++ 桌面构建工具。没有 C++ 环境时，脚本会使用 `prebuilt\BloodRayneCNTool.exe`。

安装 Python 依赖：

```powershell
python -m pip install pillow
```

## 一键构建

请先确保游戏目录是干净 Steam 原版，尤其是根目录 `JAPANESE.POD` 没有被旧汉化覆盖。最稳妥的方式是先在 Steam 里“验证游戏文件完整性”。

在仓库根目录运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

如果脚本没有自动找到游戏目录，显式指定：

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 -GameDir "C:\Program Files (x86)\Steam\steamapps\common\BloodRayne Terminal Cut"
```

输出目录：

```text
dist\BloodRayne_CN_Build
```

把该目录里的内容复制到游戏根目录覆盖即可测试。根目录默认放的是“基于英文”的中文文本；玩家可用 `zh_cn_tools\BloodRayneCNTool.exe` 在英文基准/日文基准、英文语音/日文语音、英文视频字幕/日文视频字幕之间组合切换。

## 仓库内容

- `translations\text_en.tsv`：基于英文文本的中文译文
- `translations\text_jp.tsv`：基于日文文本的中文译文
- `translations\video_subtitles.tsv`：四个有字幕版本的视频字幕译文
- `tools\`：POD 解包/重打包、文本导入、字库生成、字幕生成等构建脚本
- `src\tool\`：玩家用 Win32 单文件工具源码
- `prebuilt\BloodRayneCNTool.exe`：无 C++ 构建环境时使用的预编译玩家工具
- `subtitles\video\`：视频字幕源稿和整理稿
- `docs\chinese_note.md`：项目事实、逆向结论、打包流程和历史记录
- `third_party\`：可选第三方文件说明

## 构建产物

`build.ps1` 会生成：

- `JAPANESE.POD`：中文文本和中文游戏字库，根目录默认为英文基准
- `WORLD.POD`：修复 `DE_C1CASTLE` 原游戏缺字幕问题
- `zh_cn_tools\BloodRayneCNTool.exe`：无 Python / .NET 依赖的玩家工具
- `zh_cn_tools\texts\*.tsv`：玩家工具文本编辑页使用的译文表
- `zh_cn_tools\defaults\...` / `zh_cn_tools\variants\...`：文本切换和字体切换用 POD
- `SHA256SUMS.txt`：构建产物校验

如果 `third_party\winmm.dll` 存在，构建脚本会把它复制到输出根目录；否则不会包含该可选兼容 DLL。

## 硬字幕视频

仓库不提交游戏原始 BIK，也不默认提交重压后的 BIK 成品。原因是体积大，而且 BIK 成品由商业游戏视频派生，适合放在 GitHub Release 的可选附件，而不是源码仓库。

字幕源在 `translations\video_subtitles.tsv`。如需重新生成 ASS：

```powershell
python .\tools\generate_hardsub_ass_from_tsv.py .\translations\video_subtitles.tsv --out-dir .\dist\_work\ass
```

如需重压 BIK，需要自行安装 RAD Video Tools，并确保 `radvideo64.exe` 在 `PATH` 中或设置环境变量 `RADVIDEO`。视频重压脚本在：

```text
tools\build_hardsub_bik.py
```

## 发布前事项

- 给本项目选择并提交明确的开源许可证，例如 `LICENSE`。
- 如果发布可选 `winmm.dll`，确认其来源和授权允许再分发。
- 如果发布硬字幕 BIK，建议作为 GitHub Release 的单独可选包发布。
