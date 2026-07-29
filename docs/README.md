# BloodRayne: Terminal Cut 简体中文化工作区

本目录是汉化项目的工具与验证包。**不会自动修改游戏文件**，安装是手动一步。

---

## 一、已确认的引擎事实

这些都是从游戏文件里实测出来的，不是推测。

### POD3 封包格式（已完全解开）

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
0x110  u32   名字表字节数  ★ 打包时必须重算
0x120  ...   文件数据（按索引顺序连续排列）
<idx>  索引项 × N，每项 20 字节：
         u32 名字偏移（相对名字表起始）
         u32 大小
         u32 数据偏移
         u32 unix 时间戳
         u32 crc32_mpeg2(数据)
<名字表>  以 NUL 分隔
<审计区>  每条 312 字节，纯开发期记录，引擎不读
```

**校验算法：CRC-32/MPEG-2**（poly `0x04C11DB7`，init `0xFFFFFFFF`，不反射，末尾不异或）。

### ⚠ 踩过的坑：`0x110` 名字表长度

`0x110` 曾被误判为 build id。它其实是**名字表的字节数**，六个语言包加
`PCART` / `STARTUP` / `WORLD` 全部吻合。

只要增删文件或改文件名，这个字段就必须重算。写错的后果很隐蔽：
**所有 CRC 照样验证通过**，但引擎按错误长度读名字表，直接拒绝挂载，弹
`Cannot mount ...` / `PODMAIN.CPP`。

`podtool.py verify` 现在会显式检查这一项，不要只看 CRC。

### ⚠ 引擎自带的报错 bug

挂载失败时弹的文件名**是错的**。`0x4F4E48` 处压入 `JAPANESE.POD` 调用挂载，
失败分支在 `0x4F4E55` 压的却是 `"Cannot mount RUSSIAN.POD"` 字面量——原作者
从俄语那段复制粘贴忘了改。

**认行号，不要认文件名**：

| Line | 实际失败的包 |
|---|---|
| 145 (0x91) | RUSSIAN.POD |
| **148 (0x94)** | **JAPANESE.POD** |

`podtool.py` 对 ENGLISH / JAPANESE / RUSSIAN / FRENCH / ITALIAN / SPANISH
六个包做过解包→重打包，输出与原文件**逐字节一致**。

### 文本

- 各语言一个独立 POD，`ENGLISH.POD` 仅 405 KB
- 43 个 `.txt`，格式 `键, 说话人, 正文`，正文即待译内容
- `MSGLIST.TXT` 是 UI 串，格式 `"英文原文", "译文"`
- 总量约 **2 万词 / 152 KB**

### 双字节支持（关键）

`MSGLIST.TXT` 前 6 行是元数据，日语版内容为：

```
Version
0
Double byte support
1                      ← 双字节开关
Double byte font
"ＭＳ Ｐゴシック"        ← 字体名
```

`rayne1.exe` 中 `CreateFontA` 调用点在 `0x578C14`，逆序压栈可读出
`fdwCharSet = 1` = **DEFAULT_CHARSET**——**没有硬编码 Shift-JIS**。
全文 `.text` 段也搜不到 `0x81/0x9F/0xE0` 一类的 SJIS 前导字节范围比较。
结论：字符集跟随系统 ANSI 代码页，**不需要改 exe**。

### 字体

`0x446457` 处按字体 id 分发：`7` → fnte_pfd，`6` → **dbcsfont.fnt**，其余 → brfont.fnt。
所以双字节文本走的是预生成图集，不是运行时 GDI 渲染——**中文必须自己造图集**。

**`.TEX` 格式**：792 字节头 + 两个 8-bit 平面

```
头 u32: [2, 2, width, height, 0, 0, 0xFF000000, 0xFFFFFFFF]
平面 0: 亮度（原版恒为 0x01）
平面 1: alpha，即字形覆盖
```

**`.FNT` 格式**（version 1001，纯文本）

```
// .FNT version
1001
// charSpacing, lineHeight, lineSpacing, shadowXOffset, shadowYOffset
2,79,2,0,0
// firstChar, charCount
33,1253
<码位>,<x>,<y>,<宽>,<高>,<顶部间距>,<u0>,<v0>,<u1>,<v1>
```

#### ⚠ 第二个数字不是「字形数量」

文件里的注释写的是 `// firstChar, charCount`，**这个注释是错的**。
引擎实际按 `second - first + 1` 分配槽位，并只读这么多条。所以它是一个
「闭区间上界」，正确写法是 `first + 条目数 - 1`。

五个随游戏发布的字体全部吻合，无一例外：

| 字体 | first | second | 实际条目 | second-first+1 |
|---|---|---|---|---|
| DBCSFONT | 33 | 1253 | 1221 | 1221 ✔ |
| BRFONT | 33 | 255 | 223 | 223 ✔ |
| BRBIGFONT | 33 | 255 | 223 | 223 ✔ |
| FONTCOUR | 33 | 255 | 223 | 223 ✔ |
| BRFONT_RU | 33 | 255 | 223 | 223 ✔ |

写成原始数量的后果同样很隐蔽：**不报错、不崩溃**，超出
`(数量 - first)` 的字形被静默丢弃，游戏里表现为**部分汉字显示为空白**，
而且因为条目按码位排序，空白的总是码位靠后的那批。

`mkdbcsfont.py` 现在会写正确的上界，并在生成后自检每个声明的字形在图集里
确实有像素。

码位是多字节编码的大端整数值（原版是 Shift-JIS，中文用 GBK）。
原版 `DBCSFONT.TEX` 是 1024×1024，只装了 **1221 个字形**——即日文译文实际用到的字，
不是完整字库。中文同理，按译文用字生成即可。

---

## 二、工具

| 文件 | 用途 |
|---|---|
| `podtool.py` | POD3 解包 / 打包 / 校验，`list`·`unpack`·`pack`·`verify` |
| `extract_font.py` | 从 `PCART.POD` 抽出全部字体资源 |
| `mkdbcsfont.py` | 按译文用字生成中文 `DBCSFONT.TEX` / `.FNT`，容量不够自动扩图集 |
| `textio.py` | 文本导出成 CSV 对照表 / 从 CSV 回填 |
| `build_release_from_japanese.py` | 从日文源文本表生成“基于日文”的 `JAPANESE.POD` |
| `generate_hardsub_ass_from_tsv.py` | 从视频字幕翻译 TSV 生成四个 per-video ASS |
| `build_test_pack.py` | 一键产出验证包 |
| `stress_test.py` | 生成大字库压力测试包 |
| `build_hardsub_bik.py` | 给 `INTRO` / `OUTRO5` 烧简中硬字幕并用 RAD Video Tools 重压 BIK |

### 翻译流程

```bash
# 1. 导出对照表（UTF-8 BOM，Excel 直接打开）
python textio.py export "游戏根目录" 翻译表.csv

# 2. 填 chinese 列（其余列不要动，file/line 是回填定位用的）

# 3. 随时看进度
python textio.py stats 翻译表.csv

# 4. 回填 + 打包
python textio.py import "游戏根目录" 翻译表.csv work/
```

CSV 列：`file, line, kind, key, speaker, english, chinese, note`

`kind` 三种：`voice` 有配音的台词（1342 条）、`hud` 任务目标等界面文字
（405 条）、`ui` 设置与系统串（496 条）。共 **2243 条 / 约 15000 词**。

未填的行会自动回退英文，所以可以分批翻译、随时出包测试。

常用命令：

```bash
python podtool.py list   ENGLISH.POD
python podtool.py unpack ENGLISH.POD work/
python podtool.py pack   work/ out.POD --template ENGLISH.POD
python podtool.py verify out.POD
```

硬字幕构建：

```bash
python zh_cn_tools\build_hardsub_bik.py . --source-dir zh_cn_tools\backup\video_original
```

输出写到 `zh_cn_tools\hardsub_dist\`。确认无误后可加 `--install` 覆盖安装到
`video\`；安装时会先备份原版 BIK 到 `zh_cn_tools\backup\video_original\`。

`unpack` 会额外写出 `_pod_manifest.txt`（原始文件名大小写与时间戳）和
`_pod_audit.bin`（审计区原样保留），`pack` 靠它们复原字节级一致的输出。

---

## 三、验证包怎么用

`dist/JAPANESE.POD` 是最小验证包，内容：

- 12 条通用 UI 串译为中文，GBK 编码
- 双字节开关置 1，字体名改为 `黑体`
- 其余未翻译内容回退为**英文**（不是日文），所以不会出现乱码干扰判断
- 内含按用字生成的中文 `ART\DBCSFONT.TEX` + `DATA\DBCSFONT.FNT`
  （53 个汉字 + 原版 94 个 ASCII 字形逐像素保留）

### 安装

1. 备份原文件（`backup/JAPANESE.POD.bak` 已存一份）
2. 把 `dist/JAPANESE.POD` 复制到游戏根目录，覆盖同名文件
3. 启动游戏，在语言里选 **Japanese**

### 看什么

按重要性排序，逐条确认：

1. **中文是否显示** —— 设置界面应出现「分辨率」「色深」「渲染器」「开始游戏」
   「恢复默认设置」等字样
2. **是否需要区域设置** —— 若显示为乱码，用 Locale Emulator 以简体中文(936)
   启动 `rayne1.exe` 再试。这一条决定最终要不要给玩家附带 LE 或改 exe
3. **字体覆盖是否生效** —— 若中文位置是空白或错字，说明语言 POD 覆盖不了
   `PCART.POD` 里的字体。所有 POD 的 priority 都是 1000，优先级靠挂载顺序，
   需要改走直接重打 `PCART.POD` 的路线（477 MB，慢但可行）
4. **字形位置/基线** —— 中文若偏上偏下，调 `mkdbcsfont.py` 的 `--size`
   和字形裁剪的 `bearing`
5. **断行** —— 找一句长中文看是否按字断行

### 重新生成

```bash
python build_test_pack.py "游戏根目录"
```

要换字体（例如换成系统里的思源黑体）：

```bash
python build_test_pack.py "游戏根目录" --font C:\Windows\Fonts\msyh.ttc --index 0
```

---

## 四、后续路线

**已解决**：封包格式、校验算法、文本格式、字体格式、字符集不需要改 exe。

**待验证**（就是上面那 5 条，跑一次验证包即可全部确认）。

**剩余工作量**：

| 项 | 量 | 说明 |
|---|---|---|
| 正文翻译 | ~2 万词 | 唯一的大头 |
| UI 串 | ~250 条 | `MSGLIST.TXT` |
| 字体图集 | 自动 | 已验证：字号 17 + 2048×2048 可装 GB2312 一级全量 3755 字，仅用到 646px，余量充足 |
| 语言菜单加「中文」 | 可选 | 改 exe 里的 `Language\tJapanese` 字符串即可，纯观感 |
| 过场视频硬字幕 | 采用 | 游戏 SRT 残留能解析但不渲染；最终发布改用硬字幕 `.bik` |
| DE_C1CASTLE 缺字幕 | 已修 | `patch_castle_subtitles.py` 将 51 处 `dbConversation` 原地改成 `dbStartSay    ` |
| 玩家工具 | 已合并 | `dist_tool\BloodRayneCNTool.exe`：文本/语音/视频字幕组合切换 + 高 DPI 鼠标修复，单文件 GUI；内嵌 Noto Sans SC 子集，跨系统显示简中 |

字体建议用**思源黑体 / Noto Sans CJK SC**（OFL 许可，可随汉化包分发），
不要直接打包 Windows 自带的黑体或微软雅黑。
