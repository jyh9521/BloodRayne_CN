# BloodRayne: Terminal Cut 汉化工作笔记

每次改动都追加到这里。倒序排列，最新的在最上面。

---

## 引擎事实速查

这些都是从文件和反汇编里实测出来的，可以当结论用。

### POD3 封包

```
0x000  "POD3"
0x004  u32   文件头 CRC = crc32_mpeg2(file[0x008:0x120])
0x008  char[80]  注释
0x058  u32   文件数
0x05C  u32   审计记录数
0x060  u32   revision
0x064  u32   priority
0x108  u32   索引偏移
0x10C  u32   用途不明，原样保留
0x110  u32   名字表字节数   ★ 增删文件必须重算
0x120  ...   文件数据（按索引顺序连续排列）
<idx>  索引项 × N，每项 20 字节：名字偏移 / 大小 / 数据偏移 / 时间戳 / crc32_mpeg2(数据)
<名字表>  NUL 分隔
<审计区>  每条 312 字节，纯开发期记录，引擎不读
```

校验：**CRC-32/MPEG-2**（poly `0x04C11DB7`，init `0xFFFFFFFF`，不反射，末尾不异或）。

`podtool.py` 对六个语言包解包→重打包，输出与原文件**逐字节一致**。

### 文本

- 各语言独立 POD，`ENGLISH.POD` 仅 405 KB
- 43 个 `.txt`，格式 `键, 说话人, 正文`
- `MSGLIST.TXT` 是 UI 串，`"英文原文", "译文"` 成对，前 6 行是元数据
- 导出后共 **2243 条 / 约 15000 词**：voice 1342、hud 405、ui 496

### 双字节

- `MSGLIST.TXT` 第 4 行 = 双字节开关，第 6 行 = 字体名
- `CreateFontA` 调用点 `0x578C14`，`fdwCharSet = 1` = DEFAULT_CHARSET，**没有硬编码 Shift-JIS**，不需要改 exe
- 实测在中文系统上直接可用，**不需要 Locale Emulator**

### 字体

- `0x446457` 按 id 分发：`6` → `dbcsfont.fnt`（双字节走这条）
- `.TEX` = 792 字节头 + 亮度平面 + alpha 平面，两个 8-bit 平面
  头部 u32：`[2, 2, width, height, 0, 0, 0xFF000000, 0xFFFFFFFF]`
- `.FNT` v1001 纯文本：`码位,x,y,宽,高,顶部间距,u0,v0,u1,v1`，码位是多字节编码的大端整数
- 原版日文图集只装了 1221 字，即译文实际用字，不是完整字库

### 语言 id

跳转表 `0x4E79B0`：**1=EN 2=FR 3=IT 4=ES 5=DE 6=JP 7=RU**

我们占用 **JP 槽位**（id 6），因为它是引擎已经跑通双字节代码路径的那个。
`rayne1.ini` 里对应 `language=JP`。

### 过场动画

- 位置：游戏目录 `video\`，全部 `.bik`（Bink1，`BIKi`，1280×960，29.97fps）
- 六个语言版本的**视频流逐帧哈希完全一致**，只有音轨不同——官方做的是配音不是字幕，全片无内嵌字幕
- 有对白的只有 `INTRO`（2:51）和 `OUTRO5`（0:28），其余无语音
- 硬编码基名在 `0x4CE4E9`：`video\ziggurat`、`video\logos`、`video\intro`、`video\demo`
- 脚本触发的：`queueVideo(trans)` 在阿根廷篇结尾，`queueVideo(Outro1~5)` 在最终 Boss 后
- **ffmpeg 只能解码 Bink，不能编码**。硬封装字幕必须用 RAD Video Tools 重压

### SRT 字幕系统（Terminal Cut 新增）

- 文件名：`<视频基名>_<语言后缀>.srt`，如 `video\intro_JP.srt`
- 构造点 `0x57D2C4`，在 bik 打开函数内部 `0x57D6F7` **无条件调用**，不受 `subtitleMode` 门控
- 走引擎虚拟文件系统（`..\ENGINE\DOSIO.C`），**不是** `fopen`
- 解析器期望的格式：
  1. 序号行（`%d\n`，返回值必须为 1）
  2. 时间码行 `%d:%d:%d,%d --> %d:%d:%d,%d`，**必须正好 8 个字段**
  3. **只能一行文本**
  4. 跳过一行（空行）
- **单个视频最多 200 条**（`cmp eax, 0xc8`）
- 条目结构 0x408 字节：起始时间 float(+0)、结束时间 float(+4)、文本(+8, 上限 0x3FF)
- 渲染循环 `0x57D989`，倒序遍历按时间戳匹配

---

## 发包清单

- **`winmm.dll` = BloodRayne tweaks 1.7** —— 发布汉化包时必须一并封装。
- `JAPANESE.POD`（中文文本 + 中文字库）
- `WORLD.POD`（已修 `DE_C1CASTLE` 机甲关卡 `dbConversation` 缺字幕）
- 硬字幕版 `.bik`（至少 `INTRO` / `OUTRO5`，按最终语音版本分别处理）
- `dist_tool\BloodRayneCNTool.exe`（文本/语音/视频字幕组合切换 + 高 DPI 鼠标修复，单文件 GUI）
- 说明文件（语言要选 Japanese）

---

## 双版本架构（英文语音 / 日文语音）

### 硬约束：中文文本只能跑在 JP 槽位

`0x4463ED` 处 `call 0x57ADC0` 取当前**语言 id** 存入 esi，随后：

```
cmp esi, 7  → fnte_pfd      (RU)
cmp esi, 6  → dbcsfont.fnt  (JP) ★ 双字节字库只认 id 6
else        → brfont.fnt
```

**双字节字库被硬编码绑死在语言 id 6 上。** 所以不能靠"改跑 EN 槽位"来做英文语音版
——那样会 fallback 到单字节的 `brfont.fnt`，中文一个字都出不来。两个版本都必须
`language=JP`。

### 语音是按语言 id 分的

- `PCVOX.POD` 内含 `SOUND\EN`(3325) `SOUND\JP`(3313) `SOUND\ES` `SOUND\FR` `SOUND\IT` `SOUND\RU`
- 路径由三处调用点拼装，格式串 `'%s\%s'`：`0x550FB7`、`0x50F028`、`0x513705`
  每处都是 `call 0x57ADC0`(取语言id) → `push eax` → `call 0x4E79B0`(id转后缀)
- 过场动画音轨同理，由 `0x57D68E` 决定用 `INTRO_JP.bik` 还是 `INTRO_EN.bik`

### 两版差异点

| | 文本 | 字库 | 语言id | 游戏内语音 | 过场音轨 |
|---|---|---|---|---|---|
| 日文语音版 | 中文 | dbcsfont | JP | `SOUND\JP` | `INTRO_JP.bik` |
| 英文语音版 | 中文 | dbcsfont | JP | 需重定向到 `SOUND\EN` | 需重定向到 `INTRO_EN.bik` |

英文语音版的重定向有两条路：

**A. exe 补丁（推荐）**：把语音相关调用点的 `call 0x57ADC0`（5 字节）替换成
`mov eax, 1`（`B8 01 00 00 00`，同样 5 字节）。原地等长覆盖，不用重定位，可逆。
代价：动了 exe，Steam 校验完整性会报差异。

**B. 本地文件复制**：过场动画可以直接把 `INTRO_EN.bik` 复制成 `INTRO_JP.bik`
（用户本地已有文件，不增加下载量）。但游戏内语音在 `PCVOX.POD`(1.8GB) 里面，
只能重打包，代价太大。

---

## 双版本出包流程

早期方案是两个版本共用同一个 `JAPANESE.POD`，只按语音改 exe 的 15 个字节。
后续确认日文版游戏内文本与英文版并不完全一致，所以最终发布会改成三维组合：
文本来源、语音、视频字幕来源都允许用户独立选择。

```
# 1. 出包（两版共用）
python zh_cn_tools\build_release.py . zh_cn_tools\翻译表.tsv --install

# 2. 选语音
python zh_cn_tools\patch_exe.py . --voice en    # 英语语音
python zh_cn_tools\patch_exe.py . --voice jp    # 日语语音
python zh_cn_tools\patch_exe.py . --status      # 查当前状态
```

### 补丁位点（文件偏移，已与原始 exe 校验一致）

| 文件偏移 | VA | 用途 | 原始字节 | 补丁后 |
|---|---|---|---|---|
| `0x1503B1` | `0x550FB1` | 游戏内语音 | `E8 0A 9E 02 00` | `B8 01 00 00 00` |
| `0x10E422` | `0x50F022` | 口型同步 `.LIP` | `E8 99 BD 06 00` | `B8 01 00 00 00` |
| `0x17CA88` | `0x57D688` | 过场 `.bik` 音轨 | `E8 33 D7 FF FF` | `B8 01 00 00 00` |
| `0x112AFF` | `0x5136FF` | **中文文本加载路径** | `E8 BC 76 06 00` | **守卫，绝不改** |

`call <取语言id>`（5 字节）→ `mov eax, 1`（5 字节，1=EN）。等长原地覆盖，可逆。

最后一行是这套补丁最危险的地方：它和前三处指令完全相同，改了中文就全没了。
所以工具把它当守卫，**写盘前后各校验一次**，不符就中止且不落盘。

口型 `.LIP` 必须跟语音一起切，否则英语语音配日语口型会明显对不上。

### 给玩家的一键工具

`zh_cn_tools\dist_tool\BloodRayneCNTool.exe` 随汉化包一起发。单文件原生 Win32 GUI，
不依赖 Python / PowerShell / .NET。放在游戏根目录或其子目录可自动识别；如果没识别到，
会让玩家选择游戏目录。

两个标签页：

- `切换`：三个下拉框 + `应用切换` 按钮。
  - `文本`：`基于英文` / `基于日文`，覆盖游戏根目录 `JAPANESE.POD`。
  - `语音`：`英文语音` / `日文语音`，改 `rayne1.exe` 的游戏内语音和口型同步两个位点。
  - `视频字幕`：`基于英文` / `基于日文`，只改 `rayne1.exe` 的过场视频选择位点，让游戏播放 `_EN.bik` 或 `_JP.bik`。
- `高 DPI 鼠标修复`：启用 / 恢复默认两个按钮，写入或移除当前用户
  `AppCompatFlags\Layers` 里的 `HIGHDPIAWARE`。

工具不写备份、不写临时文件。语音还原靠精确字节表，状态不认识就拒绝操作。
为保证任何语言的 Windows 上都显示简体中文，exe 内嵌 `Noto Sans SC` 子集字体
（只含本工具实际用字），启动时通过 `AddFontMemResourceEx` 私有加载，退出时释放；
不会安装字体或留下字体残留。

文本切换资源按以下目录随汉化包放在游戏目录内；工具只负责复制/覆盖，不内嵌大文件：

```
zh_cn_tools\variants\text_en\JAPANESE.POD
zh_cn_tools\variants\text_jp\JAPANESE.POD
```

硬字幕视频体积大，单独发布。想安装硬字幕视频的用户自行覆盖 `video\` 下对应 BIK；
不安装也可以使用工具的 `视频字幕` 选项，但它只会改变游戏选择 `_EN.bik` 还是
`_JP.bik`，不会凭空产生字幕。

**必须写进玩家说明**：Steam「验证游戏文件完整性」会把 exe 还原成日语语音，
验证过之后重新双击一次即可。

---

## 变更记录

### 2026-07-28 · 日文基准文本成品 + 四视频最终硬字幕

2026-07-28 深夜字体修正：`NotoSansSC-VF.ttf` 是可变字体，Pillow 默认加载时
实际落到 Weight 100（Thin），在游戏菜单的暗色半透明 UI 上笔画极细、可读性很差。
`mkdbcsfont.py` 已新增 `--variation`，`build_release.py` /
`build_release_from_japanese.py` 默认传 `Bold`。用户测试后认为 Bold 略粗，当前测试包改为
`Noto Sans SC Medium`、字号 19 生成 DBCS 位图字库；仍然是开源可再分发字体路线，
不使用微软雅黑等系统商业字体。

同日追加：玩家工具新增 `游戏字体` 标签页。工具可枚举系统已安装 TrueType 字体，
也可临时加载用户选择的 `.ttf/.otf/.ttc` 字体文件。选择字体后会立即用
`《吸血莱恩》简体中文汉化包` 做预览，并自动比对
当前 `JAPANESE.POD`、`zh_cn_tools\variants\text_en\JAPANESE.POD`、
`zh_cn_tools\variants\text_jp\JAPANESE.POD` 内所有 GBK 双字节用字；缺字则显示缺字数
和示例，拒绝应用，且不再弹窗。不缺字时点击 `应用字体`，工具会用 Win32/GDI 原生重建
`ART\DBCSFONT.TEX` 和 `DATA\DBCSFONT.FNT`，并直接重打包上述三个 POD。
该功能不依赖 Python，不安装字体；外部字体只用 `AddFontResourceEx(..., FR_PRIVATE)`
临时私有加载，退出时释放。工具窗口已加高，字体页说明移动到标签页内部，避免越界。

同日追加修复：英文基准构建路径曾把 `WORLD\JP\MSGLIST.TXT` 第 6 行双字节字体名
写成 `"黑体"`；日文基准保留原版 `"ＭＳ Ｐゴシック"`。实测英文基准启动时报
`Can't load texture dbcsfont.tif`，说明该字段不是纯展示名，可能参与引擎字体资源
分发。`textio.py` 已改为保留原版 `"ＭＳ Ｐゴシック"` 标记，英文/日文基准一致。

同日追加排查：一次实机卡住后闪退的 WER 记录为 `rayne1.exe` /
异常码 `0xc0000417` / 偏移 `0x0018acd0`，崩溃报告确认进程加载了根目录本地
`WINMM.dll`。该 DLL 是第三方 `BloodRayne_tweaks 1.7.0.0`，不是当前工具的
高 DPI 注册表修复；当前工具只写
`HKCU\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers`
里的 `HIGHDPIAWARE`。因此后续核心测试包移除 `winmm.dll`，当前安装现场已将它改名为
`winmm.dll.disabled_by_codex_20260728_2300` 以便 A/B 验证。

同日追加确认：用户实测上述闪退与 `winmm.dll` 无关，触发点是
`LA_GRAVEYARD.TXT` 第 58/59 号语音附近：`dbSay(a1s4_Rayne12)` 播完后进入
`dbSay(a1s4_mynce18a)` 时卡住；跳过该段文本则可继续。原版日文
`a1s4_mynce18a` 文本中间有显式 `\n`，中文曾合成单行。已将译文改为：
`明斯：也许吧，也可能是这一次的情况更糟。\n这些生物很像是昆虫，似乎每隔三十年就会成群出现。`
并重建当前 `JAPANESE.POD`、`variants\text_en`、`variants\text_jp`。

基于用户完成的 `zh_cn_tools\japanese_text_export.tsv`，新增日文源文本构建脚本：

```
zh_cn_tools\build_release_from_japanese.py
```

它从 `zh_cn_tools\backup\JAPANESE.POD.bak` 解包，按日文源表的 `file/line`
回填 `chinese` 列，生成中文 `JAPANESE.POD`。空译文行共 110 条，检查后确认
这些行的 `japanese` 原文也为空，属于原日文包空白/拟声占位，不是漏翻。

当前两个文本变体均已生成到玩家工具约定目录：

| 变体 | 文件 | 大小 | SHA256 |
|---|---|---:|---|
| 基于英文 | `zh_cn_tools\variants\text_en\JAPANESE.POD` | 2,336,530 | `E1446775B7E50320B836BE93057F576CC9159F676E125CE1937102355D11124E` |
| 基于日文 | `zh_cn_tools\variants\text_jp\JAPANESE.POD` | 2,332,805 | `BA42131EAFCF15A1E47D259A25CF672B1A2D0CBC323E6B3CDDB93D63DAF1C014` |

两者 `podtool.py verify` 均 PASS，字体闸门均通过。正式发布字体使用
`NotoSansSC-VF.ttf` 生成游戏内 DBCS 位图字库；Noto Sans SC 为开源字体，
不是微软雅黑/黑体/宋体这类不可再分发的系统字体。`mkdbcsfont.py` 的自动探测
顺序也已改成优先选择 `zh_cn_tools\fonts\NotoSansSC-VF.ttf` 或
`C:\Windows\Fonts\NotoSansSC-VF.ttf`。

日文基准表已追加 34 条 `line=+` / `kind=ui-exe` 的 exe 查表补项；当前根目录
`JAPANESE.POD` 与 `text_jp` 变体一致，`Off (Faster)`、`On (Slower)`、
`Normal (Faster)`、`High (Slower)` 均已在 `WORLD\JP\MSGLIST.TXT` 命中中文译文。

基于用户完成的 `zh_cn_tools\video_subtitle_drafts\video_subtitles_to_translate.tsv`，
新增 ASS 生成脚本：

```
zh_cn_tools\generate_hardsub_ass_from_tsv.py
```

生成四份 per-video ASS：

```
zh_cn_tools\hardsub_work\ass\INTRO_EN.ass
zh_cn_tools\hardsub_work\ass\INTRO_JP.ass
zh_cn_tools\hardsub_work\ass\OUTRO5_EN.ass
zh_cn_tools\hardsub_work\ass\OUTRO5_JP.ass
```

`build_hardsub_bik.py` 新增 `--ass-dir` 参数，压制时从 `<target>.ass` 读取字幕。
本次从 `zh_cn_tools\backup\video_original` 的原版 BIK 重新压制，输出：

| 文件 | 大小 | SHA256 |
|---|---:|---|
| `zh_cn_tools\hardsub_dist\INTRO_EN.bik` | 309,248,992 | `20B064F90EC2BFD0B4A64CDE90F22822D9F21707C408E21314002FDF35C11E62` |
| `zh_cn_tools\hardsub_dist\INTRO_JP.bik` | 309,262,444 | `251A4962C952209C231B43ACA76F89F14A6F2432496A3E1A20B42A0D8B70CE0C` |
| `zh_cn_tools\hardsub_dist\OUTRO5_EN.bik` | 47,994,700 | `F993D44D45E46C8612FBD53BBC5B2E29318FB026E89D7007C9C54D9994DCE8FD` |
| `zh_cn_tools\hardsub_dist\OUTRO5_JP.bik` | 47,922,088 | `0ECA2B9555620DB7E83956BD6AC228D2F79F386809C0881D70D871AA540C81BE` |

已抽帧检查：

```
zh_cn_tools\hardsub_work\verify_final\INTRO_EN_12s.png
zh_cn_tools\hardsub_work\verify_final\INTRO_JP_12s.png
zh_cn_tools\hardsub_work\verify_final\OUTRO5_EN_20s.png
zh_cn_tools\hardsub_work\verify_final\OUTRO5_JP_20s.png
```

四张图均确认字幕压在有效视频画面内，没有落入底部黑边；英文基准和日文基准
字幕内容不同，符合翻译表。

2026-07-28 晚追加：本机测试目录的 `video\INTRO_EN.bik`、`video\INTRO_JP.bik`、
`video\OUTRO5_EN.bik`、`video\OUTRO5_JP.bik` 已覆盖为
`zh_cn_tools\hardsub_dist\` 中的硬字幕版，四个文件 SHA256 与上表一致。
此前游戏里看不到字幕，是因为只生成了 `hardsub_dist`，尚未覆盖 `video\`。

### 2026-07-28 · TRANS 不做硬字幕发布

`TRANS_EN.bik` / `TRANS_JP.bik` 也有少量语音，已复制原版到：

```
zh_cn_tools\backup\video_original\TRANS_EN.bik
zh_cn_tools\backup\video_original\TRANS_JP.bik
```

曾临时追加到字幕翻译主表并切出听辨素材：

```
zh_cn_tools\video_subtitle_drafts\TRANS_EN.source.srt
zh_cn_tools\video_subtitle_drafts\TRANS_JP.source.srt
zh_cn_tools\video_subtitle_drafts\trans_clips\TRANS_EN_*.wav
zh_cn_tools\video_subtitle_drafts\trans_clips\TRANS_JP_*.wav
```

用户确认 TRANS 没什么实质内容，正式版不翻译、不压硬字幕。当前
`video_subtitles_to_translate.tsv` 已恢复为 30 行，只保留四个正式视频：
INTRO_EN 11、INTRO_JP 13、OUTRO5_EN 3、OUTRO5_JP 3。

`extract_bik_subtitle_drafts.py` 和 `build_hardsub_bik.py` 的默认目标已恢复为
INTRO_EN / INTRO_JP / OUTRO5_EN / OUTRO5_JP。`generate_hardsub_ass_from_tsv.py`
当前生成四份 ASS。

### 2026-07-28 · 玩家工具改为三维组合切换

`zh_cn_tools\release_tool.c` 已把第一个标签页从 `语音切换` 改成 `切换`。
界面现在提供三个下拉框：

```
文本：基于英文 / 基于日文
语音：英文语音 / 日文语音
视频字幕：基于英文 / 基于日文
```

点击 `应用切换` 后，工具会先检查文本 POD 变体是否存在，再覆盖 `JAPANESE.POD`、
修改 `rayne1.exe` 的语音两个位点和过场视频一个位点。视频字幕选项不检查也不复制
BIK 文件；硬字幕视频由用户按需单独覆盖。语音/视频补丁仍保留中文文本站点守卫，
状态异常就拒绝写入。

发包资源目录约定见上方“给玩家的一键工具”。当前只完成工具能力和目录约定；
`text_jp` 成品要等日文源文本翻译完成后生成，硬字幕视频单独出包。

已更新内嵌 UI 字体子集：从 `release_tool.c` 的宽字符串重新抽取 213 个字符，
用 `C:\Windows\Fonts\NotoSansSC-VF.ttf` 生成
`zh_cn_tools\tool_ui_noto_sc_subset.ttf`。最终
`zh_cn_tools\dist_tool\BloodRayneCNTool.exe` 重新编译完成，启动/退出烟测通过。

### 2026-07-28 · 日英视频字幕源稿提取，等待翻译

用户确认英文版和日文版视频台词不同，不能共用同一份中文字幕。当前阶段只提取
源语种字幕稿，**暂不重压 BIK**；等 `zh_text` 翻译完成后再压制硬字幕。

新增提取脚本和翻译稿目录：

```
zh_cn_tools\extract_bik_subtitle_drafts.py
zh_cn_tools\video_subtitle_drafts\video_subtitles_to_translate.tsv
zh_cn_tools\video_subtitle_drafts\INTRO_EN.source.srt
zh_cn_tools\video_subtitle_drafts\INTRO_JP.source.srt
zh_cn_tools\video_subtitle_drafts\OUTRO5_EN.source.srt
zh_cn_tools\video_subtitle_drafts\OUTRO5_JP.source.srt
```

`video_subtitles_to_translate.tsv` 是主翻译文件，共 30 条：

| 视频 | 源语言 | 条数 |
|---|---|---:|
| `INTRO_EN` | 英语 | 11 |
| `INTRO_JP` | 日语 | 13 |
| `OUTRO5_EN` | 英语 | 3 |
| `OUTRO5_JP` | 日语 | 3 |

翻译时填 `zh_text` 列即可；`video` / `index` / `start` / `end` /
`start_srt` / `end_srt` 这些定位列不要改。`source_text` 是 Whisper ASR 初稿，
已人工整理专名和明显错字，但仍可能有误，翻译时可以顺手校正源文并在 `note` 标注。

当前 `video\INTRO_EN.bik`、`video\INTRO_JP.bik`、`video\OUTRO5_EN.bik`、
`video\OUTRO5_JP.bik` 已恢复为 `zh_cn_tools\backup\video_original\` 里的原版文件，
等待最终译文后重新硬字幕压制。上一轮 `zh_cn_tools\hardsub_dist\` 中的硬字幕 BIK
是废弃试压版，不要发布。

同日追加：游戏内文本也需要以日文原版为源文重看，不能只依赖英文版文本。
新增导出脚本和日文文本表：

```
zh_cn_tools\extract_japanese_text.py
zh_cn_tools\japanese_text_export.tsv
```

该表从 `zh_cn_tools\backup\JAPANESE.POD.bak` 读取原版日语，按 CP932 解码，
输出 UTF-8-BOM TSV。列结构：

```
file / line / kind / key / speaker / english / japanese / chinese / note
```

`japanese` 是日语源文，`english` 只作为 key 对齐后的辅助参考，`chinese` 留空等待翻译。
导出结果共 2479 条：hud 399、voice 1348、ui 732。日语包与英文包文件集合一致；
有 7 条日语语音 key 在英文包里没有同名 key，已在 `note=missing matching English key`
标注，翻译时以 `japanese` 为准。

### 2026-07-28 · INTRO / OUTRO5 硬字幕 BIK 首版

已下载并解包官方 RAD Video Tools。官网包为加密 7z，密码 `RAD`；
安装器要求管理员权限，但本地直接用 7z 解出 `radvideo64.exe` 即可，不需要安装。

新增硬字幕素材和脚本：

```
zh_cn_tools\hardsub_intro.ass
zh_cn_tools\hardsub_outro5.ass
zh_cn_tools\build_hardsub_bik.py
```

2026-07-28 追加修正：首版字幕放在 1280×960 容器的下黑边里，游戏/播放器缩放时
会裁切。已把两个 ASS 的 `MarginV` 从 `86` 改为 `220`，让字幕落进实际宽银幕画面
内容区内。原片有效画面实测约为 y=189..770，底黑边约 190px。

流程：`ffmpeg` 解码 Bink 并烧 ASS 字幕到 MP4 中间片，再用
`radvideo64.exe binkc <中间片> <输出.bik> /#` 重压成 Bink1。
`ffmpeg` 来源当前用 Python 包 `imageio-ffmpeg`；RAD 工具在：

```
zh_cn_tools\radtools\portable\radvideo64.exe
```

当前已生成四个双语音版本的硬字幕成品：

```
zh_cn_tools\hardsub_dist\INTRO_EN.bik
zh_cn_tools\hardsub_dist\INTRO_JP.bik
zh_cn_tools\hardsub_dist\OUTRO5_EN.bik
zh_cn_tools\hardsub_dist\OUTRO5_JP.bik
```

尺寸：

| 文件 | 大小 |
|---|---:|
| `INTRO_EN.bik` | 309,249,408 |
| `INTRO_JP.bik` | 309,262,308 |
| `OUTRO5_EN.bik` | 47,943,384 |
| `OUTRO5_JP.bik` | 47,958,788 |

解码检查通过：四个输出仍是 `BIKi`，1280×960，29.97fps；英语版保留
48000Hz mono 音轨，日语版保留 44100Hz stereo 音轨。已抽帧检查：

```
zh_cn_tools\hardsub_work\verify\INTRO_EN_12s.jpg
zh_cn_tools\hardsub_work\verify\INTRO_JP_12s.jpg
zh_cn_tools\hardsub_work\verify\OUTRO5_EN_20s.jpg
zh_cn_tools\hardsub_work\verify\OUTRO5_JP_20s.jpg
```

这些帧均确认简体中文字幕已烧入像素，字体和描边正常。

注意：开场电影对白不在游戏文本表里。当前字幕稿是基于本地英文音轨用
`faster-whisper` 转写后人工修正专名（Brimstone / Beliar / Rayne 等）的首版；
如果后续要精修台词和时间轴，直接改两个 `.ass` 后重跑：

```
python zh_cn_tools\build_hardsub_bik.py . --source-dir zh_cn_tools\backup\video_original
```

构建默认只写 `hardsub_dist`，不会覆盖 `video\`。确认后可用：

```
python zh_cn_tools\build_hardsub_bik.py . --source-dir zh_cn_tools\backup\video_original --install
```

该命令会先把原版 BIK 备份到 `zh_cn_tools\backup\video_original\`，再覆盖安装。

注意：本节记录的是上一轮共用中文草稿的试压流程。由于后续确认英文版和日文版台词
不同，这批输出已废弃；当前游戏目录四个 BIK 已恢复为
`zh_cn_tools\backup\video_original\` 中的原版文件。

`build_hardsub_bik.py` 会在 RAD 压制前删除已有输出文件，避免 `radvideo64.exe`
因覆盖确认弹窗卡住。当前安装版已按 `MarginV=220` 重新生成并核对：

| 文件 | SHA256 |
|---|---|
| `INTRO_EN.bik` | `6967FA95AAAE3730C59D62B418ABC038065436C4CB7F6DD86181F2ADA22CD5CB` |
| `INTRO_JP.bik` | `5801462E7D694F81709D13BCB74CE13F5EDFF58488EEADCE6676D5C49B5EDE1E` |
| `OUTRO5_EN.bik` | `2273B4BC35B7FACD016DDA17B73CC9724060C495DD4B4B3A13DE8F4BC691CEF2` |
| `OUTRO5_JP.bik` | `5666233ED3F51AAB878CDA53B25CFADBF19D22222EB6B1A1ADE1EE7A73CAB447` |

### 2026-07-28 · DE_C1CASTLE 机甲关卡字幕修复 + 单文件玩家工具

`DE_C1CASTLE` 缺字幕不是翻译漏包，而是脚本里 51 处机甲对话用了
`dbConversation(...)`。该函数在 Terminal Cut 里播放语音但不显示字幕。

修复方式：在 `WORLD\DE_C1CASTLE.SCB` 里原地替换：

```
dbConversation(
dbStartSay    (
```

`dbStartSay` 后补 4 个空格，保持每条脚本文本长度不变，所以不用重排 SCB 内部表，
也不用重排 `WORLD.POD`；只重算该 POD 条目的 CRC。当前 `WORLD.POD` 已应用，
`podtool.py verify WORLD.POD` 通过。

新增工具：

```
zh_cn_tools\patch_castle_subtitles.py . --apply
zh_cn_tools\patch_castle_subtitles.py . --restore
zh_cn_tools\patch_castle_subtitles.py . --status
```

`dist_switch` 和 `dist_dpi` 已合并为单文件 GUI：

```
zh_cn_tools\dist_tool\BloodRayneCNTool.exe
```

2026-07-28 追加修正：旧版 GUI 用 `DEFAULT_GUI_FONT`，在日文/非中文系统上会落到
缺简中字形的字体，控件正文出现方块。现已改为内嵌 `Noto Sans SC` 子集字体并给
所有子控件显式设置字体；`PrintWindow` 截图确认正文显示正常。完整字体 17.8MB，
子集字体约 182KB，最终 exe 约 347KB。

编译脚本：

```
zh_cn_tools\build_release_tool.bat
```

### 2026-07-28 · Steam 两个已知 bug 排查 + DPI 鼠标修复

**Steam 字幕 bug**：`https://steamcommunity.com/app/1373510/discussions/0/4904953131461150114/`
对应的问题是 `DE_C1CASTLE` 机甲关卡缺字幕。搜索索引暴露的回复提到：
直到 Gosler 才有字幕，脚本用的是 `dbConversation()`。

本地排查结论：汉化文本**没有漏包**。当前 `JAPANESE.POD` 内
`WORLD\JP\DE_C1CASTLE.TXT` 存在，机甲关卡 5-63 行对白已经是中文；
`WORLD.POD` 里的 `WORLD\DE_C1CASTLE.SCB` 确实大量调用 `dbConversation(...)`
（51 处）。所以这个 bug 是原游戏/脚本显示路径继承问题，不是汉化表遗漏。
已在后续记录里通过改 `WORLD\DE_C1CASTLE.SCB` 修复。

**高 DPI 鼠标 bug**：Steam 玩家描述的“鼠标左键/右键游戏中无效，键盘攻击键正常，
Windows 显示缩放改 100% 后恢复”符合微软 KB 2907016 描述的高 DPI 游戏输入缩放错位。
本地 `rayne1.exe` manifest 没有声明 DPI aware，确实会走 Windows DPI 虚拟化路径。

旧版玩家工具曾用 bat + PowerShell，现已废弃并合并到
`zh_cn_tools\dist_tool\BloodRayneCNTool.exe`。

它写入当前用户注册表：

```
HKCU\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers
<rayne1.exe 绝对路径> = ~ HIGHDPIAWARE
```

等价于 `rayne1.exe` 属性 → 兼容性 → 更改高 DPI 设置 →
“高 DPI 缩放替代：应用程序”。不改 exe，不影响 Steam 校验；脚本也提供撤销。

### 2026-07-28 · 过场字幕路线改为硬字幕

最终结论：游戏没有能实际渲染 `.bik` 字幕的完整系统。虽然 exe 里有 SRT 加载/解析
残留，且文件能被 VFS 找到、读出、解析出时间轴，但播放阶段始终没有文本绘制。
发布路线改为**硬字幕**。

`build_release.py` 已停止把 `zh_cn_tools\subtitles\*.srt` 计入字形集或写入
`JAPANESE.POD`。这些 SRT 只保留为打轴/烧录素材，不再属于游戏内发布链路。

### 2026-07-28 · exe 界面串补齐 + 查表机制

**发现**：MSGLIST 是个**以英文原文为键的查表**。exe 把硬编码的界面串推给
`0x4E7840` 查表，命中就换译文，没命中就原样显示英文。

所以界面上残留的英文**不一定是写死的**，多半只是 MSGLIST 里缺条目，补上即可，
不需要改 exe。

**系统性排查**：枚举所有 `call 0x4E7840` 的调用点并回溯其字符串参数——
**共 244 条 exe 串经过查表，其中 27 条 MSGLIST 里没有**。这是完整缺口，
已全部补齐（`Walk` `Run` `MouseB1~5` `MWheel fwd/back` `Monitor\t%d`
`Antialiasing\t%s` `Texture detail\t%s` `Water reflections\t*` 等）。

这些行在翻译表里标 `kind=ui-exe`、`line=+`，`textio` 会追加成新的 MSGLIST 条目
而不是替换现有行。

**翻不了的例外**：语言选项的值（`English` `Japanese` `Italiano` `Español`
`Russian`）在 `0x4C969C` 那段是用 `sprintf` **直接拼进显示串的，不经过查表**，
MSGLIST 加条目无效。只能打二进制补丁。GBK 中文比英文短（`英语`4字节 <
`English`7字节），可原地覆盖。但考虑到只影响设置里一行的值，暂不做。

### 2026-07-28 · 过场字幕：加载已通，卡在渲染（未解决，待继续）

**当前状态：字幕文件能被正确找到、读取、解析，时间轴也建好了，就是画不出来。**

#### 已证实работает的部分（不用再查）

用 DebugView 抓 `OutputDebugStringA`（**不要只看 `console.txt`，它记不全**，
我因此误判过一次），三个测试字幕全部解析成功，时间码与源文件完全一致：

```
Line : 1   Start 0.500000   End 3.000000     ← ziggurat
Line : 1   Start 1.000000   End 6.000000     ← logos
Line : 2   Start 6.500000   End 12.000000
Line : 1   Start 2.000000   End 7.000000     ← intro
Line : 2   Start 8.000000   End 13.000000
```

说明这些都是对的：POD 内路径命名、文件被 VFS 找到、GBK 编码、CRLF 行尾、
解析器格式匹配、时间码换算。

#### 已排除的可能（都有实证，别重走）

| 假设 | 证据 |
|---|---|
| `subtitleMode` 没开 | ini 里 `subtitleMode=1`；试过改 2 也无效 |
| 语言槽不对 | `language=JP`，且解析器确实去读了 `_JP.srt` |
| 行尾要 CRLF | 读行函数 `0x55CA20` 对 `\r` 跳过、`\n` 断行，两种都吃 |
| POD 查找区分大小写 | 比较函数 `0x554690` 第三参数为 0 = 大小写不敏感 |
| 文件没进 POD / 路径不对 | 已证伪——解析器成功读出了内容 |
| 中文字库加载太晚 | `dbcsfont.tif` 在 t=36.23 载入，视频在 38.4/41.4/44.4，**早两秒多** |
| 中文字形缺失 | 换成纯 ASCII 测试同样不显示 |

#### 关键地址

| 地址 | 作用 |
|---|---|
| `0x57D280` | SRT 加载/解析函数入口 |
| `0x57D2C4` | 拼 `%s_%s.srt` 文件名 |
| `0x57D2F5` | 经引擎 VFS 打开（`0x55CD80`，**不是** fopen） |
| `0x57D315` | 读序号行，`%d\n`，返回值必须 == 1 |
| `0x57D32D` | 调试打印 `Line : %d` ← 日志里能看到，证明走到了这里 |
| `0x57D395` | 时间码 sscanf，`%d:%d:%d,%d --> %d:%d:%d,%d`，必须正好 8 个字段 |
| `0x57D3C4` | 条目写入数组 `0x1FBD1F0`，步长 `0x408` |
| `0x1FEF830` | 字幕条数（解析后应 > 0） |
| `0x57D989` | 播放循环里按时间戳倒序匹配当前字幕的循环 |
| `0x57D6F7` | 在 bik 打开函数内**无条件**调用 SRT 加载 |

条目结构（0x408 字节）：起始时间 float(+0)、结束时间 float(+4)、文本(+8, 上限 0x3FF)

#### 还没查清的地方（下一步从这里接）

1. **找不到真正的文本绘制调用。** `0x57D989` 那个循环只是按时间选中条目，
   算出 `edx` 指向条目后就走到 `0x57D9C7`，没看到把结果存到哪，也没看到
   取 `+8` 处文本去画的地方。绘制点应该在播放循环 `0x57D750`~`0x57D960`
   之间，但那段全是浮点数在铺视频四边形的顶点，我没找出字幕那一支。

2. **线性反汇编在这一带会失步**，用步长 `0x408` 反查引用点搜不到结果
   （明明 `0x57D3BE` 和 `0x57D999` 都有 `imul ..., 0x408`）。建议用 IDA/Ghidra
   这类能正确划分函数边界的工具重看，比脚本线性扫可靠。

3. **`subtitleMode` 的真实语义没确认。** 它是 exe 里唯一与字幕相关的字符串，
   存在设置对象 `[0x62D6D0]+0xC`，写入点 `0x495019`。没有界面开关。
   有效取值范围、以及绘制处到底判不判它，都还没验证。

#### 当前遗留物

`subtitles\` 下是 ASCII / 中文交替的测试字幕（`logos.srt` `intro.srt`
`ziggurat.srt` `demo.srt`），POD 里对应 `VIDEO\*_JP.SRT` 等三种路径拼写各一份。
不需要的话删掉 `subtitles\` 里的文件重新出包即可，不影响正文汉化。

#### 备选方案

硬烧字幕进视频。注意 **ffmpeg 能解码 Bink 但不能编码**，必须用 RAD Video Tools。
且英语/日语两版播的是不同 `.bik`，要各烧一份，原片合计约 700MB，
发行成本很高。只有 INTRO(2:51) 和 OUTRO5(0:28) 有对白。

**问题**：SRT 字幕始终不显示，logo / 片头都试过。

**已排除的原因**（都验证过，不是这些）：

| 假设 | 结论 |
|---|---|
| `subtitleMode` 关着 | ✘ 用户 ini 里 `subtitleMode=1`，是开的 |
| 语言槽不对 | ✘ ini 里 `language=JP`，对的 |
| 行尾符要 CRLF | ✘ 读行函数 `0x55CA20` 对 `\r` 跳过、对 `\n` 断行，LF/CRLF 通吃 |
| POD 查找区分大小写 | ✘ 比较函数 `0x554690` 第三参数为 0 = 大小写不敏感 |
| 字幕文件没进 POD | ✘ 已确认在包内，CRC 通过 |

**本次改动**：`build_release.py` 现在为每个字幕同时生成三种候选路径打进 POD：

```
VIDEO\INTRO_JP.SRT
INTRO_JP.SRT
video\intro_JP.srt
```

穷举比继续猜便宜。待验证。

**下一步若仍失败**：`0x449470`（路径规范化）和 `0x55CD80` → 虚表 +0x2C 的
CDataFile 打开路径还没读完，可能与 getFileInfo 钩子走的不是同一条路。

### 2026-07-28 · 字幕纳入造字流程

字幕文本在 POD 之外但共用同一张 DBCS 图集，不算进去会整片空白。
`build_release.py` 现在扫 `subtitles/` 并计入字形闸门。

字幕文件命名规则：放 `subtitles\`，**用视频基名，不带语言后缀**
（`intro.srt`、`outro5.srt`），工具自动补后缀和路径。

### 2026-07-28 · 视频代理片

`zh_cn_tools\proxy\` 下生成 640×480 代理片，共 12MB（原片 2.4GB），
用于在 Subtitle Edit / Aegisub 里拉时间轴。帧率与原片一致（29.97），
**量出的时间码可直接写进 SRT，不用换算**。

### 2026-07-28 · TSV 支持

`textio.py` 按扩展名自动识别分隔符，`.tsv` / `.csv` 通吃，新增 `convert` 命令。
转换时若任何单元格含真实制表符或换行会直接报错拒绝写，防止撕列。

实测全表无真实制表符——文本里的 `\t` 是字面两字符转义，不是 tab 字节。

### 2026-07-28 · 字形集判定标准统一（修 bug）

**症状**：破折号 `—`、中文引号 `“ ”`、省略号 `…` 不显示。

**原因**：`mkdbcsfont.py` 用 Unicode 区段白名单决定造哪些字，只放行汉字、
CJK 标点、全角符号，漏掉了 General Punctuation 区段。而 `build_release.py`
的闸门用的是"GBK 能否编码"。**两套标准不一致就是丢字的来源。**

**修正**：统一成一套——闸门要什么，造字就造什么。

同时 `™` 无 GBK 码位，按英文原文写法降级成 `(TM)` 而非静默替换成 `?`，
`©` `®` `€` 同样处理，并明确报告替换了什么。

### 2026-07-28 · .FNT 槽位数写法（修 bug）

**症状**：部分汉字显示为空白，且空白的总是码位靠后的那批。

**原因**：`.FNT` 头部第二个数字，文件自带注释写的是 `charCount`，**注释是错的**。
引擎实际按 `second - first + 1` 分配槽位并只读这么多条。写成原始数量会导致
超出部分被静默丢弃。

五个随游戏发布的字体全部吻合，无一例外：

| 字体 | first | second | 实际条目 | second-first+1 |
|---|---|---|---|---|
| DBCSFONT | 33 | 1253 | 1221 | 1221 ✔ |
| BRFONT | 33 | 255 | 223 | 223 ✔ |
| BRBIGFONT | 33 | 255 | 223 | 223 ✔ |
| FONTCOUR | 33 | 255 | 223 | 223 ✔ |
| BRFONT_RU | 33 | 255 | 223 | 223 ✔ |

**修正**：正确写法是 `first + 条目数 - 1`。生成后自检每个声明的字形在图集里
确实有像素。

### 2026-07-28 · 名字表长度字段（修 bug）

**症状**：游戏启动弹 `Cannot mount RUSSIAN.POD` / `PODMAIN.CPP` / `Line: 148`。

**原因**：头部 `0x110` 被误判为 build id，实为**名字表字节数**。增删文件后
名字表长度变了，字段没重算，引擎按错误长度读，拒绝挂载。

**这个坑很隐蔽：所有 CRC 照样验证通过。** 校验覆盖数据和头部自身，
覆盖不到头部记的这本账。

**修正**：打包时重算 `0x110`；`podtool.py verify` 增加结构性检查
（名字表长度、文件数、索引偏移），最后给 PASS/FAIL。

**附带发现——引擎自身的报错 bug**：挂载失败时弹的文件名是错的。
`0x4F4E48` 压入 `JAPANESE.POD` 调挂载，失败分支 `0x4F4E55` 压的却是
`"Cannot mount RUSSIAN.POD"` 字面量，原作者从俄语那段复制粘贴忘了改。

**认行号不认文件名**：145 = RUSSIAN.POD，**148 = JAPANESE.POD**。

### 2026-07-28 · 英文原文的 CP1252 字节

英文脚本不是纯 ASCII，混着 `ß`（德语脏话）、`é`（匈牙利人名）、`î`（法语台词）
和弯引号。双字节模式下这些高位字节会被当成前导字节吃掉后半行。

**处理**：音译降级（`ß`→`ss`、`é`→`e`）而非替换成 `?`，保证未翻译的英文
回退行仍然可读。

### 2026-07-28 · 大字库容量验证（通过）

字号 17 + 2048×2048 装下 GB2312 一级全量 3755 字，仅用到 646px，余量充足。

真机验证：4 行探针分别取自图集顶部 / 中部 / 下部 / 最底部，每行 6 字全部正常。
证明引擎接受 2048×2048 图集，且从 y=20 到 y=628 整张图的 V 坐标都算得对。

### 2026-07-28 · 最小验证包（通过）

12 条 UI 串译成中文，双字节开关置 1，中文图集内置，未翻译部分回退英文。
真机确认「分辨率」「显卡」「恢复默认设置」「开始游戏」全部正常显示。

---

## 工具

| 文件 | 用途 |
|---|---|
| `podtool.py` | POD3 解包 / 打包 / 校验 |
| `extract_font.py` | 从 `PCART.POD` 抽字体资源 |
| `mkdbcsfont.py` | 按用字生成中文 `DBCSFONT.TEX` / `.FNT`，容量不够自动扩图集 |
| `textio.py` | 文本导出 CSV/TSV / 回填 / 统计 / 格式互转 |
| `build_release.py` | 一键出包（含字体、字形闸门；不再打包 SRT） |
| `stress_test.py` | 大字库压力测试包 |

### 日常循环

```
python zh_cn_tools\build_release.py . zh_cn_tools\翻译表.tsv --install
python zh_cn_tools\textio.py stats zh_cn_tools\翻译表.tsv
```

**路径写 `.`**（在游戏目录里跑），不要写占位符文字。

原版 `JAPANESE.POD` 备份在 `zh_cn_tools\backup\`。

### 硬性约束

- 游戏内 SRT 路线已废弃；字幕发布走硬字幕 `.bik`
- GBK 装不下生僻字和 CJK 扩展 B 区，回填时会警告并列出
- CSV/TSV 的 `file` 和 `line` 两列不要动，回填靠它们定位

### 2026-07-28 · 文本安全范围按像素宽检测

字幕 / 对话风险不是单纯汉字数量，而是当前 `DATA\DBCSFONT.FNT` 下每个
字面 `\n` 分段的实际像素宽度。英文可按空格换行，中文长段没有空格时更容易
触发引擎问题。

已知样本：

- 崩溃段合并后：`830px`（`LA_GRAVEYARD.TXT` / `a1s4_mynce18a`）
- 修复后两段：`367px` / `461px`
- 用户参考长句：`681px`

当前先按风险线处理：

- `>= 800px`：高危，优先人工插入 `\n`
- `700-799px`：可疑，建议检查显示效果
- `< 700px`：暂按低风险处理，但还不是引擎硬上限证明

扫描工具：

```
python zh_cn_tools\scan_text_widths.py --pod JAPANESE.POD --sheet zh_cn_tools\翻译表.tsv --limit-px 700 --out zh_cn_tools\long_chinese_segments_over_700px.tsv
```

同日进一步收窄：这次卡死点更像是**语音字幕的连续无断点 token**问题，
不是整句总长。原版日文语音字幕里有更长整句，但会用半角空格切成多个 token。

实机边界测试使用开头第一句 `LA_CHURCH.TXT` / `a1s1_Rayne1.wav`：

- `75` GBK 字节：显示
- `76` GBK 字节：显示
- `78` GBK 字节：闪退
- `80` / `83` / `84` / `85` / `86` / `87` GBK 字节：闪退
- `76` GBK 字节内容中插入 4 个字面 `\n`：显示
- `7` 行、每行 `76` GBK 字节、行间/末尾共 `7` 个字面 `\n`：显示

发布规则：`kind=voice` 的文本按字面 `\n` 分成显示行后，**每个显示行不得超过
76 GBK 字节**。字面 `\n` 本身不计入显示行长度；原始字段总长度也不是这次触发
闪退的限制。超出则人工插入字面 `\n`。已生成实机测试包：

```
zh_cn_tools\engine_limit_tests\voice_token\
```

其中 `start_line\T075` / `T076` / `T078` / `T080` 等目录只替换开头第一句，
便于快速复测边界。

临时筛选表：

```
zh_cn_tools\voice_tokens_over_76_en_base.tsv
zh_cn_tools\voice_tokens_over_76_jp_base.tsv
```

这两份表只列 `kind=voice` 且连续 token `> 76` GBK 字节的项目；当前英文基准
50 条，日文基准 1 条。扫描脚本：

```
python zh_cn_tools\scan_voice_token_lengths.py --sheet zh_cn_tools\翻译表.tsv --limit 76 --out zh_cn_tools\voice_tokens_over_76_en_base.tsv
python zh_cn_tools\scan_voice_token_lengths.py --sheet zh_cn_tools\japanese_text_export.tsv --limit 76 --out zh_cn_tools\voice_tokens_over_76_jp_base.tsv
```
