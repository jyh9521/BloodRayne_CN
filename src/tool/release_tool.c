/*
 * BloodRayne: Terminal Cut 简体中文版 - 玩家工具
 *
 * 单文件 Win32 GUI：
 *   - 切换：组合切换文本来源、语音、视频字幕来源。
 *   - 游戏修复：写入/移除 HKCU AppCompatFlags 的 HIGHDPIAWARE，并可给 rayne1.exe 开启 LAA。
 *
 * 编译示例：
 *   cl /nologo /O2 /MT /DUNICODE /D_UNICODE release_tool.c /Fe:切换与修复工具.exe ^
 *      /link /SUBSYSTEM:WINDOWS comctl32.lib advapi32.lib shell32.lib
 */

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>

#include "font_patch.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")

#define APP_TITLE L"BloodRayne 简体中文版工具"
#define NSITES 3
#define PATCH_LEN 5
#define MAX_TOKENS 32

typedef struct {
    DWORD off;
    BYTE orig[PATCH_LEN];
    const wchar_t *name;
} PatchSite;

static const PatchSite kVoiceSites[NSITES] = {
    { 0x1503B1, { 0xE8, 0x0A, 0x9E, 0x02, 0x00 }, L"游戏内语音" },
    { 0x10E422, { 0xE8, 0x99, 0xBD, 0x06, 0x00 }, L"口型同步" },
    { 0x17CA88, { 0xE8, 0x33, 0xD7, 0xFF, 0xFF }, L"过场动画音轨" },
};

static const DWORD kGuardOff = 0x112AFF;
static const BYTE kGuardOrig[PATCH_LEN] = { 0xE8, 0xBC, 0x76, 0x06, 0x00 };
static const BYTE kVoicePatched[PATCH_LEN] = { 0xB8, 0x01, 0x00, 0x00, 0x00 };

static const wchar_t *kDpiFlags[] = {
    L"HIGHDPIAWARE",
    L"DPIUNAWARE",
    L"GDIDPISCALING",
    L"PERPROCESSSYSTEMDPIFORCEON",
    L"PERPROCESSSYSTEMDPIFORCEOFF",
};

enum {
    IDC_GAME_PATH = 101,
    IDC_BROWSE = 102,
    IDC_TAB = 103,
    IDC_STATUS = 104,
    IDC_TEXT_COMBO = 201,
    IDC_VOICE_COMBO = 202,
    IDC_VIDEO_COMBO = 203,
    IDC_APPLY_SWITCH = 204,
    IDC_SWITCH_INFO = 205,
    IDC_DPI_ENABLE = 301,
    IDC_DPI_RESTORE = 302,
    IDC_DPI_INFO = 303,
    IDC_LAA_ENABLE = 304,
    IDC_LAA_RESTORE = 305,
    IDC_LAA_INFO = 306,
    IDC_FONT_COMBO = 401,
    IDC_FONT_REFRESH = 402,
    IDC_FONT_FILE = 403,
    IDC_FONT_CHECK = 404,
    IDC_FONT_APPLY = 405,
    IDC_FONT_INFO = 406,
    IDC_FONT_PREVIEW = 407,
    IDC_FONT_MISSING = 408,
    IDC_FONT_EXAMPLE = 409,
    IDC_FONT_RESET = 410,
    IDC_EDIT_BASIS = 501,
    IDC_EDIT_SEARCH = 502,
    IDC_EDIT_DIRTY_ONLY = 503,
    IDC_EDIT_LIST = 504,
    IDC_EDIT_ORIGINAL = 505,
    IDC_EDIT_CHINESE = 506,
    IDC_EDIT_SAVE = 507,
    IDC_EDIT_IMPORT = 508,
    IDC_EDIT_COUNT = 509,
    IDC_CELL_EDIT = 510,
    IDC_EDIT_WARN = 511,
};

#define MAX_TEXT_ROWS 9000
#define MAX_TEXT_COLS 16

typedef struct {
    wchar_t *cells[MAX_TEXT_COLS];
    int ncols;
    int id;
    int dirty;
} TextRow;

typedef struct {
    wchar_t *headers[MAX_TEXT_COLS];
    TextRow rows[MAX_TEXT_ROWS];
    int ncols;
    int nrows;
    int loaded;
    int basis_jp;
    int col_file, col_line, col_key, col_speaker, col_english, col_japanese, col_chinese, col_note;
    wchar_t path[MAX_PATH];
} TextSheet;

static HINSTANCE gInst;
static HWND gMain, gPathCtl, gTab, gStatus;
static HWND gTextCombo, gVoiceCombo, gVideoCombo, gApplySwitch, gSwitchInfo;
static HWND gTextLabel, gVoiceLabel, gVideoLabel;
static HWND gEditBasis, gEditSearch, gEditDirtyOnly, gEditList, gEditOriginal, gEditChinese;
static HWND gEditSave, gEditImport, gEditCount, gEditWarn, gEditOriginalLabel, gEditChineseLabel, gCellEdit;
static HWND gDpiEnable, gDpiRestore, gDpiInfo, gDpiNote;
static HWND gLaaEnable, gLaaRestore, gLaaInfo, gLaaNote;
static HWND gFontCombo, gFontRefresh, gFontFile, gFontApply, gFontInfo, gFontNote, gFontPreview;
static HWND gFontMissing, gFontExample, gFontReset;
static wchar_t gGameDir[MAX_PATH];
static wchar_t gExternalFontPath[MAX_PATH];
static int gFontCanApply;
static int gFontMissingBad;
static int gEditorBasis;
static int gEditorCurrentRow = -1;
static int gEditorProgrammatic;
static int gCellEditRow = -1;
static int gFilteredRows[MAX_TEXT_ROWS];
static int gFilteredCount;
static int gEditHasUnsafeVoiceLine;
static HFONT gUiFont;
static HFONT gPreviewFont;
static HANDLE gFontMem;
static TextSheet gSheet;

#define IDR_NOTO_SC 501

static void update_status(void);

static void msg(HWND hwnd, UINT icon, const wchar_t *text)
{
    MessageBoxW(hwnd, text, APP_TITLE, MB_OK | icon);
}

static int file_exists(const wchar_t *path)
{
    DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static void join_path(wchar_t *out, size_t cap, const wchar_t *dir, const wchar_t *leaf)
{
    _snwprintf(out, cap, L"%s\\%s", dir, leaf);
    out[cap - 1] = 0;
}

static void game_path(wchar_t *out, size_t cap, const wchar_t *rel)
{
    join_path(out, cap, gGameDir, rel);
}

static int file_size64(const wchar_t *path, unsigned long long *size)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &data)) return 0;
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return 0;
    *size = ((unsigned long long)data.nFileSizeHigh << 32) | data.nFileSizeLow;
    return 1;
}

static int read_chunk(const wchar_t *path, unsigned long long off, BYTE *buf, DWORD len)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    LARGE_INTEGER li;
    DWORD got = 0;
    int ok = 0;
    if (h == INVALID_HANDLE_VALUE) return 0;
    li.QuadPart = (LONGLONG)off;
    if (SetFilePointerEx(h, li, NULL, FILE_BEGIN) &&
        ReadFile(h, buf, len, &got, NULL) && got == len)
        ok = 1;
    CloseHandle(h);
    return ok;
}

static int files_same_quick(const wchar_t *a, const wchar_t *b)
{
    unsigned long long asz, bsz;
    BYTE abuf[65536], bbuf[65536];
    unsigned long long checks[3];
    int nchecks = 1;
    DWORD len;

    if (!file_size64(a, &asz) || !file_size64(b, &bsz) || asz != bsz) return 0;
    if (asz == 0) return 1;
    checks[0] = 0;
    if (asz > sizeof(abuf) * 2) {
        checks[nchecks++] = asz / 2;
        checks[nchecks++] = asz - sizeof(abuf);
    }
    for (int i = 0; i < nchecks; i++) {
        len = (DWORD)((asz - checks[i]) < sizeof(abuf) ? (asz - checks[i]) : sizeof(abuf));
        if (!read_chunk(a, checks[i], abuf, len) || !read_chunk(b, checks[i], bbuf, len))
            return 0;
        if (memcmp(abuf, bbuf, len) != 0) return 0;
    }
    return 1;
}

static int text_variant_path(int based_jp, wchar_t *out, size_t cap)
{
    const wchar_t *rel = based_jp ?
        L"zh_cn_tools\\variants\\text_jp\\JAPANESE.POD" :
        L"zh_cn_tools\\variants\\text_en\\JAPANESE.POD";
    game_path(out, cap, rel);
    return file_exists(out);
}

static int default_text_variant_path(int based_jp, wchar_t *out, size_t cap)
{
    const wchar_t *rel = based_jp ?
        L"zh_cn_tools\\defaults\\text_jp\\JAPANESE.POD" :
        L"zh_cn_tools\\defaults\\text_en\\JAPANESE.POD";
    game_path(out, cap, rel);
    return file_exists(out);
}

static int is_game_dir(const wchar_t *dir)
{
    wchar_t exe[MAX_PATH], vox[MAX_PATH], world[MAX_PATH];
    join_path(exe, MAX_PATH, dir, L"rayne1.exe");
    join_path(vox, MAX_PATH, dir, L"PCVOX.POD");
    join_path(world, MAX_PATH, dir, L"WORLD.POD");
    return file_exists(exe) && file_exists(vox) && file_exists(world);
}

static int parent_dir(wchar_t *path)
{
    wchar_t *p = wcsrchr(path, L'\\');
    if (!p) return 0;
    *p = 0;
    return 1;
}

static int try_set_game_dir(const wchar_t *dir)
{
    if (!dir || !dir[0] || !is_game_dir(dir)) return 0;
    wcsncpy(gGameDir, dir, MAX_PATH);
    gGameDir[MAX_PATH - 1] = 0;
    return 1;
}

static int detect_from_self(void)
{
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(NULL, path, MAX_PATH)) return 0;
    parent_dir(path);
    for (int i = 0; i < 4; i++) {
        if (try_set_game_dir(path)) return 1;
        if (!parent_dir(path)) break;
    }
    return 0;
}

static int detect_from_steam(void)
{
    const wchar_t *subkeys[] = {
        L"Software\\Valve\\Steam",
        L"Software\\Wow6432Node\\Valve\\Steam",
    };
    HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    wchar_t steam[MAX_PATH], dir[MAX_PATH];
    DWORD type, cb;

    for (int r = 0; r < 2; r++) {
        for (int s = 0; s < 2; s++) {
            HKEY h;
            if (RegOpenKeyExW(roots[r], subkeys[s], 0, KEY_READ, &h) != ERROR_SUCCESS)
                continue;
            cb = sizeof(steam);
            if ((RegQueryValueExW(h, L"SteamPath", NULL, &type, (BYTE *)steam, &cb) == ERROR_SUCCESS ||
                 RegQueryValueExW(h, L"InstallPath", NULL, &type, (BYTE *)steam, &cb) == ERROR_SUCCESS) &&
                type == REG_SZ) {
                _snwprintf(dir, MAX_PATH, L"%s\\steamapps\\common\\BloodRayne Terminal Cut", steam);
                dir[MAX_PATH - 1] = 0;
                if (try_set_game_dir(dir)) { RegCloseKey(h); return 1; }
            }
            RegCloseKey(h);
        }
    }

    if (try_set_game_dir(L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\BloodRayne Terminal Cut"))
        return 1;
    if (try_set_game_dir(L"C:\\Program Files\\Steam\\steamapps\\common\\BloodRayne Terminal Cut"))
        return 1;
    return 0;
}

static int choose_game_dir(HWND hwnd)
{
    BROWSEINFOW bi;
    LPITEMIDLIST pidl;
    wchar_t path[MAX_PATH];
    ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = hwnd;
    bi.lpszTitle = L"请选择 BloodRayne Terminal Cut 游戏目录";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return 0;
    path[0] = 0;
    SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    if (!try_set_game_dir(path)) {
        msg(hwnd, MB_ICONERROR, L"选择的目录不是 BloodRayne Terminal Cut 游戏目录。");
        return 0;
    }
    return 1;
}

static void game_exe_path(wchar_t *out)
{
    join_path(out, MAX_PATH, gGameDir, L"rayne1.exe");
}

static int read_bytes_at(FILE *f, DWORD off, BYTE *buf, size_t len)
{
    return fseek(f, (long)off, SEEK_SET) == 0 && fread(buf, 1, len, f) == len;
}

static int patch_group_state(int first, int count, const wchar_t *what, wchar_t *detail, size_t cap)
{
    wchar_t exe[MAX_PATH];
    FILE *f;
    BYTE cur[PATCH_LEN];
    int jp = 0, en = 0;

    if (!is_game_dir(gGameDir)) {
        _snwprintf(detail, cap, L"未识别到游戏目录");
        return -2;
    }
    game_exe_path(exe);
    if (_wfopen_s(&f, exe, L"rb") != 0 || !f) {
        _snwprintf(detail, cap, L"无法读取 rayne1.exe");
        return -2;
    }
    if (!read_bytes_at(f, kGuardOff, cur, PATCH_LEN) || memcmp(cur, kGuardOrig, PATCH_LEN) != 0) {
        fclose(f);
        _snwprintf(detail, cap, L"中文文本加载守卫异常，拒绝操作");
        return -1;
    }
    for (int i = first; i < first + count; i++) {
        if (!read_bytes_at(f, kVoiceSites[i].off, cur, PATCH_LEN)) {
            fclose(f);
            _snwprintf(detail, cap, L"读取补丁位点失败");
            return -2;
        }
        if (memcmp(cur, kVoiceSites[i].orig, PATCH_LEN) == 0) jp++;
        else if (memcmp(cur, kVoicePatched, PATCH_LEN) == 0) en++;
    }
    fclose(f);
    if (jp == count) {
        _snwprintf(detail, cap, L"当前%s：日文", what);
        return 0;
    }
    if (en == count) {
        _snwprintf(detail, cap, L"当前%s：英文", what);
        return 1;
    }
    _snwprintf(detail, cap, L"%s补丁状态不一致，可能被其他补丁改过", what);
    return -1;
}

static int voice_state(wchar_t *detail, size_t cap)
{
    int state = patch_group_state(0, 2, L"语音", detail, cap);
    if (state == 0) _snwprintf(detail, cap, L"当前语音：日语语音");
    if (state == 1) _snwprintf(detail, cap, L"当前语音：英语语音");
    return state;
}

static int video_patch_state(wchar_t *detail, size_t cap)
{
    int state = patch_group_state(2, 1, L"视频字幕", detail, cap);
    if (state == 0) {
        _snwprintf(detail, cap, L"当前视频字幕：基于日文");
        return 1;
    }
    if (state == 1) {
        _snwprintf(detail, cap, L"当前视频字幕：基于英文");
        return 0;
    }
    return state;
}

static int write_patch_group(HWND hwnd, int first, int count, int want_en, const wchar_t *what, int notify)
{
    wchar_t exe[MAX_PATH], detail[256];
    FILE *f;
    BYTE cur[PATCH_LEN];
    int state = patch_group_state(first, count, what, detail, 256);
    if (state < 0) {
        msg(hwnd, MB_ICONERROR, detail);
        return 0;
    }
    if (state == want_en) {
        if (notify)
            msg(hwnd, MB_ICONINFORMATION, L"当前已经是所选状态。");
        return 1;
    }
    game_exe_path(exe);
    if (_wfopen_s(&f, exe, L"r+b") != 0 || !f) {
        msg(hwnd, MB_ICONERROR, L"无法写入 rayne1.exe。请先关闭游戏；如果仍失败，请以管理员身份运行本工具。");
        return 0;
    }
    if (!read_bytes_at(f, kGuardOff, cur, PATCH_LEN) || memcmp(cur, kGuardOrig, PATCH_LEN) != 0) {
        fclose(f);
        msg(hwnd, MB_ICONERROR, L"写入前守卫失败，已中止。");
        return 0;
    }
    for (int i = first; i < first + count; i++) {
        const BYTE *src = want_en ? kVoicePatched : kVoiceSites[i].orig;
        if (fseek(f, (long)kVoiceSites[i].off, SEEK_SET) != 0 ||
            fwrite(src, 1, PATCH_LEN, f) != PATCH_LEN) {
            fclose(f);
            msg(hwnd, MB_ICONERROR, L"写入中断。请用 Steam 验证游戏文件后重试。");
            return 0;
        }
    }
    fclose(f);
    if (notify)
        msg(hwnd, MB_ICONINFORMATION, L"切换完成。请完全退出游戏后重新启动。");
    return 1;
}

static int set_voice(HWND hwnd, int want_en)
{
    return write_patch_group(hwnd, 0, 2, want_en, L"语音", 1);
}

static int write_voice_state(HWND hwnd, int want_en, int notify)
{
    return write_patch_group(hwnd, 0, 2, want_en, L"语音", notify);
}

static int write_video_subtitle_state(HWND hwnd, int based_jp, int notify)
{
    return write_patch_group(hwnd, 2, 1, based_jp ? 0 : 1, L"视频字幕", notify);
}

static int text_state(void)
{
    wchar_t cur[MAX_PATH], variant[MAX_PATH];
    game_path(cur, MAX_PATH, L"JAPANESE.POD");
    if (text_variant_path(0, variant, MAX_PATH) && files_same_quick(cur, variant)) return 0;
    if (text_variant_path(1, variant, MAX_PATH) && files_same_quick(cur, variant)) return 1;
    return -1;
}

static int video_subtitle_state(void)
{
    wchar_t detail[256];
    return video_patch_state(detail, 256);
}

static int copy_text_variant(HWND hwnd, int based_jp)
{
    wchar_t src[MAX_PATH], dst[MAX_PATH], err[1024];
    if (!text_variant_path(based_jp, src, MAX_PATH)) {
        _snwprintf(err, 1024, L"缺少文本变体文件：\r\n%s", src);
        msg(hwnd, MB_ICONERROR, err);
        return 0;
    }
    game_path(dst, MAX_PATH, L"JAPANESE.POD");
    if (!CopyFileW(src, dst, FALSE)) {
        _snwprintf(err, 1024, L"无法覆盖 JAPANESE.POD。\r\n请先关闭游戏；如果仍失败，请以管理员身份运行本工具。\r\n\r\n来源：%s", src);
        msg(hwnd, MB_ICONERROR, err);
        return 0;
    }
    return 1;
}

static const wchar_t *basis_label(int state)
{
    if (state == 0) return L"基于英文";
    if (state == 1) return L"基于日文";
    return L"未识别";
}

static const wchar_t *voice_label_from_state(int state)
{
    if (state == 1) return L"英语语音";
    if (state == 0) return L"日语语音";
    return L"未识别";
}

static int apply_switch(HWND hwnd)
{
    int text_choice = (int)SendMessageW(gTextCombo, CB_GETCURSEL, 0, 0);
    int voice_choice = (int)SendMessageW(gVoiceCombo, CB_GETCURSEL, 0, 0);
    int video_choice = (int)SendMessageW(gVideoCombo, CB_GETCURSEL, 0, 0);
    wchar_t text_src[MAX_PATH], missing[1024];

    if (!is_game_dir(gGameDir)) {
        msg(hwnd, MB_ICONERROR, L"未识别到游戏目录。");
        return 0;
    }
    if (text_choice < 0) text_choice = 0;
    if (voice_choice < 0) voice_choice = 0;
    if (video_choice < 0) video_choice = 0;

    if (!text_variant_path(text_choice == 1, text_src, MAX_PATH)) {
        _snwprintf(missing, 1024, L"缺少文本变体文件：\r\n%s", text_src);
        msg(hwnd, MB_ICONERROR, missing);
        return 0;
    }
    if (voice_state(missing, 1024) < 0) {
        msg(hwnd, MB_ICONERROR, missing);
        return 0;
    }
    if (video_patch_state(missing, 1024) < 0) {
        msg(hwnd, MB_ICONERROR, missing);
        return 0;
    }

    if (!copy_text_variant(hwnd, text_choice == 1)) return 0;
    if (!write_voice_state(hwnd, voice_choice == 0, 0)) return 0;
    if (!write_video_subtitle_state(hwnd, video_choice == 1, 0)) return 0;

    msg(hwnd, MB_ICONINFORMATION, L"切换完成。请完全退出游戏后重新启动。");
    return 1;
}

static int token_is_dpi(const wchar_t *tok)
{
    for (int i = 0; i < (int)(sizeof(kDpiFlags) / sizeof(kDpiFlags[0])); i++)
        if (_wcsicmp(tok, kDpiFlags[i]) == 0) return 1;
    return 0;
}

static void append_token(wchar_t tokens[MAX_TOKENS][64], int *count, const wchar_t *tok)
{
    if (!tok || !tok[0] || wcscmp(tok, L"~") == 0 || *count >= MAX_TOKENS) return;
    for (int i = 0; i < *count; i++)
        if (_wcsicmp(tokens[i], tok) == 0) return;
    wcsncpy(tokens[*count], tok, 63);
    tokens[*count][63] = 0;
    (*count)++;
}

static int read_dpi_value(wchar_t *value, DWORD cch)
{
    wchar_t exe[MAX_PATH];
    HKEY h;
    DWORD type = 0, cb = cch * sizeof(wchar_t);
    game_exe_path(exe);
    value[0] = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers",
        0, KEY_READ, &h) != ERROR_SUCCESS) return 0;
    if (RegQueryValueExW(h, exe, NULL, &type, (BYTE *)value, &cb) != ERROR_SUCCESS || type != REG_SZ) {
        RegCloseKey(h);
        value[0] = 0;
        return 0;
    }
    value[cch - 1] = 0;
    RegCloseKey(h);
    return wcsstr(value, L"HIGHDPIAWARE") != NULL;
}

static int dpi_enabled(void)
{
    wchar_t value[512];
    return read_dpi_value(value, 512);
}

static int write_dpi_tokens(HWND hwnd, int enable)
{
    wchar_t exe[MAX_PATH], value[512], copy[512];
    wchar_t tokens[MAX_TOKENS][64];
    int count = 0;
    wchar_t *ctx = NULL, *tok;
    HKEY h;
    LONG rc;

    if (!is_game_dir(gGameDir)) {
        msg(hwnd, MB_ICONERROR, L"未识别到游戏目录。");
        return 0;
    }
    game_exe_path(exe);
    read_dpi_value(value, 512);
    wcsncpy(copy, value, 511);
    copy[511] = 0;

    tok = wcstok_s(copy, L" \t\r\n", &ctx);
    while (tok) {
        if (!token_is_dpi(tok)) append_token(tokens, &count, tok);
        tok = wcstok_s(NULL, L" \t\r\n", &ctx);
    }
    if (enable) append_token(tokens, &count, L"HIGHDPIAWARE");

    rc = RegCreateKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers",
        0, NULL, 0, KEY_SET_VALUE, NULL, &h, NULL);
    if (rc != ERROR_SUCCESS) {
        msg(hwnd, MB_ICONERROR, L"无法写入当前用户兼容性设置。");
        return 0;
    }
    if (count == 0) {
        RegDeleteValueW(h, exe);
    } else {
        wchar_t out[512] = L"~";
        for (int i = 0; i < count; i++) {
            wcscat_s(out, 512, L" ");
            wcscat_s(out, 512, tokens[i]);
        }
        rc = RegSetValueExW(h, exe, 0, REG_SZ, (const BYTE *)out, (DWORD)((wcslen(out) + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(h);
    if (rc != ERROR_SUCCESS) {
        msg(hwnd, MB_ICONERROR, L"DPI 设置写入失败。");
        return 0;
    }
    msg(hwnd, MB_ICONINFORMATION, enable ?
        L"高 DPI 鼠标修复已启用。请完全退出游戏后重新启动。" :
        L"高 DPI 鼠标修复已恢复默认。");
    return 1;
}

static int pe_characteristics_offset(const wchar_t *path, DWORD *out)
{
    HANDLE h;
    LARGE_INTEGER li;
    DWORD got = 0, peoff = 0, sig = 0;
    unsigned long long size = 0;

    if (!file_size64(path, &size)) return 0;
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;

    li.QuadPart = 0x3C;
    if (!SetFilePointerEx(h, li, NULL, FILE_BEGIN) ||
        !ReadFile(h, &peoff, sizeof(peoff), &got, NULL) || got != sizeof(peoff)) {
        CloseHandle(h);
        return 0;
    }
    if ((unsigned long long)peoff + 24 > size) {
        CloseHandle(h);
        return 0;
    }
    li.QuadPart = peoff;
    if (!SetFilePointerEx(h, li, NULL, FILE_BEGIN) ||
        !ReadFile(h, &sig, sizeof(sig), &got, NULL) || got != sizeof(sig) ||
        sig != 0x00004550) {
        CloseHandle(h);
        return 0;
    }
    CloseHandle(h);
    *out = peoff + 4 + 18;
    return 1;
}

static int read_laa_state(void)
{
    wchar_t exe[MAX_PATH];
    DWORD off, got = 0;
    WORD chars = 0;
    LARGE_INTEGER li;
    HANDLE h;

    if (!is_game_dir(gGameDir)) return -1;
    game_exe_path(exe);
    if (!pe_characteristics_offset(exe, &off)) return -1;
    h = CreateFileW(exe, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    li.QuadPart = off;
    if (!SetFilePointerEx(h, li, NULL, FILE_BEGIN) ||
        !ReadFile(h, &chars, sizeof(chars), &got, NULL) || got != sizeof(chars)) {
        CloseHandle(h);
        return -1;
    }
    CloseHandle(h);
    return (chars & 0x20) ? 1 : 0;
}

static int laa_backup_path(wchar_t *out, size_t cap, int create_dirs)
{
    wchar_t dir[MAX_PATH];
    if (!is_game_dir(gGameDir)) return 0;
    if (create_dirs) {
        game_path(dir, MAX_PATH, L"zh_cn_tools");
        CreateDirectoryW(dir, NULL);
        game_path(dir, MAX_PATH, L"zh_cn_tools\\backups");
        CreateDirectoryW(dir, NULL);
    }
    game_path(out, cap, L"zh_cn_tools\\backups\\rayne1.exe.original");
    return 1;
}

static int patch_laa(HWND hwnd)
{
    wchar_t exe[MAX_PATH], bak[MAX_PATH], err[1024];
    DWORD off, got = 0, written = 0;
    WORD chars = 0;
    LARGE_INTEGER li;
    HANDLE h;
    int state;

    if (!is_game_dir(gGameDir)) {
        msg(hwnd, MB_ICONERROR, L"未识别到游戏目录。");
        return 0;
    }
    state = read_laa_state();
    game_exe_path(exe);
    if (state == 1) {
        msg(hwnd, MB_ICONINFORMATION, L"4GB/LAA 补丁已经启用。");
        return 1;
    }
    if (state < 0 || !pe_characteristics_offset(exe, &off)) {
        msg(hwnd, MB_ICONERROR, L"无法识别 rayne1.exe 的 PE 头。");
        return 0;
    }
    laa_backup_path(bak, MAX_PATH, 1);
    if (!file_exists(bak) && !CopyFileW(exe, bak, TRUE)) {
        _snwprintf(err, 1024, L"无法备份 rayne1.exe。\r\n请先关闭游戏；如果仍失败，请以管理员身份运行本工具。\r\n\r\n备份位置：%s", bak);
        msg(hwnd, MB_ICONERROR, err);
        return 0;
    }

    h = CreateFileW(exe, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        msg(hwnd, MB_ICONERROR, L"无法写入 rayne1.exe。请先关闭游戏；如果仍失败，请以管理员身份运行本工具。");
        return 0;
    }
    li.QuadPart = off;
    if (!SetFilePointerEx(h, li, NULL, FILE_BEGIN) ||
        !ReadFile(h, &chars, sizeof(chars), &got, NULL) || got != sizeof(chars)) {
        CloseHandle(h);
        msg(hwnd, MB_ICONERROR, L"读取 rayne1.exe 失败。");
        return 0;
    }
    chars |= 0x20;
    li.QuadPart = off;
    if (!SetFilePointerEx(h, li, NULL, FILE_BEGIN) ||
        !WriteFile(h, &chars, sizeof(chars), &written, NULL) || written != sizeof(chars)) {
        CloseHandle(h);
        msg(hwnd, MB_ICONERROR, L"写入 4GB/LAA 补丁失败。");
        return 0;
    }
    CloseHandle(h);
    msg(hwnd, MB_ICONINFORMATION, L"4GB/LAA 补丁已启用。原版 exe 已备份，可在本页一键恢复。");
    return 1;
}

static int restore_laa(HWND hwnd)
{
    wchar_t exe[MAX_PATH], bak[MAX_PATH], err[1024];
    if (!is_game_dir(gGameDir)) {
        msg(hwnd, MB_ICONERROR, L"未识别到游戏目录。");
        return 0;
    }
    laa_backup_path(bak, MAX_PATH, 0);
    if (!file_exists(bak)) {
        msg(hwnd, MB_ICONERROR, L"没有找到原版 exe 备份，无法恢复。");
        return 0;
    }
    game_exe_path(exe);
    if (!CopyFileW(bak, exe, FALSE)) {
        _snwprintf(err, 1024, L"无法恢复 rayne1.exe。\r\n请先关闭游戏；如果仍失败，请以管理员身份运行本工具。\r\n\r\n备份位置：%s", bak);
        msg(hwnd, MB_ICONERROR, err);
        return 0;
    }
    msg(hwnd, MB_ICONINFORMATION, L"rayne1.exe 已恢复为备份的原版。");
    return 1;
}

static void update_status(void)
{
    wchar_t detail[256], dpi[128], laa[128], repair[256], status[1024], sw[512];
    int v = voice_state(detail, 256);
    int t = text_state();
    int s = video_subtitle_state();
    int l = read_laa_state();
    (void)v;
    _snwprintf(sw, 512, L"当前文本：%s\r\n当前语音：%s\r\n当前视频字幕：%s",
        basis_label(t), voice_label_from_state(v), basis_label(s));
    _snwprintf(dpi, 128, L"高 DPI 鼠标修复：%s", dpi_enabled() ? L"已启用" : L"未启用");
    _snwprintf(laa, 128, L"4GB/LAA 补丁：%s", l == 1 ? L"已启用" : (l == 0 ? L"未启用" : L"无法识别"));
    _snwprintf(repair, 256, L"%s\r\n%s", dpi, laa);
    _snwprintf(status, 1024, L"%s\r\n%s", sw, repair);
    SetWindowTextW(gPathCtl, gGameDir[0] ? gGameDir : L"未识别");
    SetWindowTextW(gSwitchInfo, sw);
    SetWindowTextW(gDpiInfo, dpi);
    SetWindowTextW(gLaaInfo, laa);
    SetWindowTextW(gStatus, status);

    SendMessageW(gTextCombo, CB_SETCURSEL, t == 1 ? 1 : 0, 0);
    SendMessageW(gVoiceCombo, CB_SETCURSEL, v == 0 ? 1 : 0, 0);
    SendMessageW(gVideoCombo, CB_SETCURSEL, s == 1 ? 1 : 0, 0);
}

static HWND mk(HWND parent, const wchar_t *cls, const wchar_t *text, DWORD style,
    int x, int y, int w, int h, int id)
{
    return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, gInst, NULL);
}

static BOOL CALLBACK set_child_font(HWND child, LPARAM lp)
{
    SendMessageW(child, WM_SETFONT, (WPARAM)lp, TRUE);
    return TRUE;
}

static wchar_t *xwcsdup(const wchar_t *s)
{
    size_t n = s ? wcslen(s) : 0;
    wchar_t *p = (wchar_t *)calloc(n + 1, sizeof(wchar_t));
    if (p && s) wcscpy_s(p, n + 1, s);
    return p;
}

static void free_sheet(TextSheet *sh)
{
    for (int c = 0; c < sh->ncols; c++) {
        free(sh->headers[c]);
        sh->headers[c] = NULL;
    }
    for (int r = 0; r < sh->nrows; r++) {
        for (int c = 0; c < sh->rows[r].ncols; c++) {
            free(sh->rows[r].cells[c]);
            sh->rows[r].cells[c] = NULL;
        }
    }
    ZeroMemory(sh, sizeof(*sh));
    sh->col_file = sh->col_line = sh->col_key = sh->col_speaker = -1;
    sh->col_english = sh->col_japanese = sh->col_chinese = sh->col_note = -1;
}

static int get_tool_dir(wchar_t *out, size_t cap)
{
    if (!GetModuleFileNameW(NULL, out, (DWORD)cap)) return 0;
    return parent_dir(out);
}

static void text_sheet_path(int basis_jp, wchar_t *out, size_t cap)
{
    wchar_t dir[MAX_PATH];
    get_tool_dir(dir, MAX_PATH);
    _snwprintf(out, cap, L"%s\\texts\\%s", dir, basis_jp ? L"text_jp.tsv" : L"text_en.tsv");
    out[cap - 1] = 0;
}

static wchar_t *read_utf8_file(const wchar_t *path)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    LARGE_INTEGER sz;
    DWORD got = 0;
    char *buf;
    wchar_t *out;
    int wlen;
    if (h == INVALID_HANDLE_VALUE) return NULL;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 0 || sz.QuadPart > 64 * 1024 * 1024) {
        CloseHandle(h);
        return NULL;
    }
    buf = (char *)malloc((size_t)sz.QuadPart + 1);
    if (!buf) {
        CloseHandle(h);
        return NULL;
    }
    if (!ReadFile(h, buf, (DWORD)sz.QuadPart, &got, NULL) || got != (DWORD)sz.QuadPart) {
        free(buf);
        CloseHandle(h);
        return NULL;
    }
    CloseHandle(h);
    buf[sz.QuadPart] = 0;
    char *start = buf;
    int bytes = (int)sz.QuadPart;
    if (bytes >= 3 && (BYTE)buf[0] == 0xEF && (BYTE)buf[1] == 0xBB && (BYTE)buf[2] == 0xBF) {
        start += 3;
        bytes -= 3;
    }
    wlen = MultiByteToWideChar(CP_UTF8, 0, start, bytes, NULL, 0);
    if (wlen <= 0) {
        free(buf);
        return NULL;
    }
    out = (wchar_t *)calloc((size_t)wlen + 1, sizeof(wchar_t));
    if (out)
        MultiByteToWideChar(CP_UTF8, 0, start, bytes, out, wlen);
    free(buf);
    return out;
}

static int write_utf8_file(const wchar_t *path, const wchar_t *text)
{
    HANDLE h;
    DWORD written = 0;
    int bytes;
    char bom[3] = { (char)0xEF, (char)0xBB, (char)0xBF };
    char *buf;
    bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    if (bytes <= 1) return 0;
    buf = (char *)malloc(bytes);
    if (!buf) return 0;
    WideCharToMultiByte(CP_UTF8, 0, text, -1, buf, bytes, NULL, NULL);
    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        free(buf);
        return 0;
    }
    if (!WriteFile(h, bom, 3, &written, NULL) || written != 3 ||
        !WriteFile(h, buf, bytes - 1, &written, NULL) || written != (DWORD)(bytes - 1)) {
        CloseHandle(h);
        free(buf);
        return 0;
    }
    CloseHandle(h);
    free(buf);
    return 1;
}

static int split_tsv_line(wchar_t *line, wchar_t **cols, int maxcols)
{
    int n = 0;
    wchar_t *p = line;
    while (n < maxcols) {
        cols[n++] = p;
        wchar_t *tab = wcschr(p, L'\t');
        if (!tab) break;
        *tab = 0;
        p = tab + 1;
    }
    return n;
}

static int header_index(TextSheet *sh, const wchar_t *name)
{
    for (int i = 0; i < sh->ncols; i++) {
        if (sh->headers[i] && _wcsicmp(sh->headers[i], name) == 0) return i;
    }
    return -1;
}

static int load_text_sheet(int basis_jp)
{
    wchar_t path[MAX_PATH], *text, *ctx = NULL, *line;
    wchar_t *cols[MAX_TEXT_COLS];
    int first = 1;
    free_sheet(&gSheet);
    text_sheet_path(basis_jp, path, MAX_PATH);
    text = read_utf8_file(path);
    if (!text) return 0;
    line = wcstok_s(text, L"\n", &ctx);
    while (line) {
        size_t len = wcslen(line);
        while (len && (line[len - 1] == L'\r' || line[len - 1] == L'\n')) line[--len] = 0;
        if (first) {
            gSheet.ncols = split_tsv_line(line, cols, MAX_TEXT_COLS);
            for (int c = 0; c < gSheet.ncols; c++) gSheet.headers[c] = xwcsdup(cols[c]);
            first = 0;
        } else if (gSheet.nrows < MAX_TEXT_ROWS) {
            TextRow *r = &gSheet.rows[gSheet.nrows];
            int n = split_tsv_line(line, cols, MAX_TEXT_COLS);
            r->ncols = gSheet.ncols;
            r->id = gSheet.nrows + 1;
            for (int c = 0; c < gSheet.ncols; c++) r->cells[c] = xwcsdup(c < n ? cols[c] : L"");
            gSheet.nrows++;
        }
        line = wcstok_s(NULL, L"\n", &ctx);
    }
    free(text);
    gSheet.basis_jp = basis_jp;
    gSheet.loaded = 1;
    wcsncpy(gSheet.path, path, MAX_PATH - 1);
    gSheet.col_file = header_index(&gSheet, L"file");
    gSheet.col_line = header_index(&gSheet, L"line");
    gSheet.col_key = header_index(&gSheet, L"key");
    gSheet.col_speaker = header_index(&gSheet, L"speaker");
    gSheet.col_english = header_index(&gSheet, L"english");
    gSheet.col_japanese = header_index(&gSheet, L"japanese");
    gSheet.col_chinese = header_index(&gSheet, L"chinese");
    gSheet.col_note = header_index(&gSheet, L"note");
    return gSheet.col_chinese >= 0;
}

static wchar_t *escaped_to_editor(const wchar_t *s)
{
    size_t n = wcslen(s);
    wchar_t *out = (wchar_t *)calloc(n * 2 + 2, sizeof(wchar_t));
    size_t j = 0;
    if (!out) return NULL;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == L'\\' && s[i + 1]) {
            if (s[i + 1] == L'n') {
                out[j++] = L'\r';
                out[j++] = L'\n';
                i++;
            } else if (s[i + 1] == L't') {
                out[j++] = L'\t';
                i++;
            } else if (s[i + 1] == L'\\') {
                out[j++] = L'\\';
                i++;
            } else {
                out[j++] = s[i];
            }
        } else {
            out[j++] = s[i];
        }
    }
    out[j] = 0;
    return out;
}

static wchar_t *editor_to_escaped(const wchar_t *s)
{
    size_t n = wcslen(s);
    wchar_t *out = (wchar_t *)calloc(n * 2 + 2, sizeof(wchar_t));
    size_t j = 0;
    if (!out) return NULL;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == L'\r') {
            if (s[i + 1] == L'\n') i++;
            out[j++] = L'\\';
            out[j++] = L'n';
        } else if (s[i] == L'\n') {
            out[j++] = L'\\';
            out[j++] = L'n';
        } else if (s[i] == L'\t') {
            out[j++] = L'\\';
            out[j++] = L't';
        } else if (s[i] == L'\\') {
            out[j++] = L'\\';
            out[j++] = L'\\';
        } else {
            out[j++] = s[i];
        }
    }
    out[j] = 0;
    return out;
}

static int ci_contains(const wchar_t *hay, const wchar_t *needle)
{
    size_t hn, nn;
    if (!needle || !needle[0]) return 1;
    if (!hay) return 0;
    hn = wcslen(hay);
    nn = wcslen(needle);
    if (nn > hn) return 0;
    for (size_t i = 0; i + nn <= hn; i++) {
        size_t j = 0;
        for (; j < nn; j++) {
            if (towlower(hay[i + j]) != towlower(needle[j])) break;
        }
        if (j == nn) return 1;
    }
    return 0;
}

static int row_matches_filter(TextRow *r, const wchar_t *q, int dirty_only)
{
    wchar_t idbuf[32];
    if (dirty_only && !r->dirty) return 0;
    if (!q || !q[0]) return 1;
    _snwprintf(idbuf, 32, L"%d", r->id);
    if (ci_contains(idbuf, q)) return 1;
    if (gSheet.col_english >= 0 && ci_contains(r->cells[gSheet.col_english], q)) return 1;
    if (gSheet.col_chinese >= 0 && ci_contains(r->cells[gSheet.col_chinese], q)) return 1;
    return 0;
}

static void extract_placeholders(const wchar_t *s, wchar_t *out, size_t cap)
{
    size_t j = 0;
    out[0] = 0;
    for (size_t i = 0; s && s[i]; i++) {
        size_t start, p, k = 0;
        wchar_t buf[64];
        if (s[i] != L'%') continue;
        if (s[i + 1] == L'%') {
            i++;
            continue;
        }
        start = i;
        p = i + 1;

        /*
         * The game text uses Windows FormatMessage-style placeholders such as
         * %1!s! and %2!d!. A literal percentage like "100%" must not be treated
         * as a placeholder.
         */
        if (!iswdigit(s[p])) continue;
        while (iswdigit(s[p])) p++;
        if (s[p] != L'!') continue;
        p++;
        if (!s[p] || s[p] == L'!') continue;
        while (s[p] && s[p] != L'!' && k < 60) p++;
        if (s[p] != L'!') continue;
        p++;

        k = p - start;
        if (k >= 64) continue;
        wcsncpy_s(buf, 64, s + start, k);
        buf[k] = 0;
        if (j + k + 2 < cap) {
            if (j) out[j++] = L' ';
            wcscpy_s(out + j, cap - j, buf);
            j += k;
        }
        i = p - 1;
    }
}

static int is_voice_row(TextRow *r)
{
    int col_kind = header_index(&gSheet, L"kind");
    return col_kind >= 0 && r->cells[col_kind] && _wcsicmp(r->cells[col_kind], L"voice") == 0;
}

static int gbk_bytes_line(const wchar_t *s, int chars)
{
    wchar_t *buf;
    int out = 0;
    int i = 0;
    int bytes;
    if (chars <= 0) return 0;
    buf = (wchar_t *)calloc((size_t)chars + 1, sizeof(wchar_t));
    if (!buf) return WideCharToMultiByte(936, 0, s, chars, NULL, 0, NULL, NULL);
    while (i < chars) {
        if (i + 1 < chars && s[i] == L'@' && s[i + 1] == L'@') {
            int j = i + 2;
            int closed = 0;
            while (j + 1 < chars) {
                if (s[j] == L'@' && s[j + 1] == L'@') {
                    j += 2;
                    closed = 1;
                    break;
                }
                j++;
            }
            if (closed) {
                i = j;
                continue;
            }
        }
        buf[out++] = s[i++];
    }
    bytes = WideCharToMultiByte(936, 0, buf, out, NULL, 0, NULL, NULL);
    free(buf);
    return bytes;
}

static int max_voice_line_gbk_bytes(const wchar_t *escaped)
{
    int maxb = 0;
    const wchar_t *start = escaped;
    const wchar_t *p = escaped;
    while (p && *p) {
        if (*p == L'\\' && p[1] == L'n') {
            int b = gbk_bytes_line(start, (int)(p - start));
            if (b > maxb) maxb = b;
            p += 2;
            start = p;
            continue;
        }
        if (*p == L'\r' || *p == L'\n') {
            int b = gbk_bytes_line(start, (int)(p - start));
            if (b > maxb) maxb = b;
            if (*p == L'\r' && p[1] == L'\n') p++;
            p++;
            start = p;
            continue;
        }
        p++;
    }
    if (start) {
        int b = gbk_bytes_line(start, (int)(p - start));
        if (b > maxb) maxb = b;
    }
    return maxb;
}

static int sheet_has_unsafe_line(int *row_out, int *bytes_out)
{
    for (int i = 0; i < gSheet.nrows; i++) {
        TextRow *r = &gSheet.rows[i];
        int b;
        b = max_voice_line_gbk_bytes(r->cells[gSheet.col_chinese]);
        if (b > 76) {
            if (row_out) *row_out = i;
            if (bytes_out) *bytes_out = b;
            return 1;
        }
    }
    return 0;
}

static void update_edit_safety(void)
{
    int row = -1, bytes = 0;
    wchar_t warn[256];
    gEditHasUnsafeVoiceLine = sheet_has_unsafe_line(&row, &bytes);
    if (gEditHasUnsafeVoiceLine) {
        _snwprintf(warn, 256, L"文本单行超过 76 GBK 字节：ID %d，当前 %d。请换行后再保存。", gSheet.rows[row].id, bytes);
        SetWindowTextW(gEditWarn, warn);
    } else {
        SetWindowTextW(gEditWarn, L"文本单行限制：每行不超过 76 GBK 字节。");
    }
    EnableWindow(gEditSave, !gEditHasUnsafeVoiceLine);
    EnableWindow(gEditImport, !gEditHasUnsafeVoiceLine);
}

static const wchar_t *cell_or_empty(TextRow *r, int col)
{
    if (!r || col < 0 || col >= r->ncols || !r->cells[col]) return L"";
    return r->cells[col];
}

static void row_required_placeholders(TextRow *r, wchar_t *out, size_t cap)
{
    const wchar_t *src = L"";
    out[0] = 0;
    if (gSheet.basis_jp && gSheet.col_japanese >= 0) src = r->cells[gSheet.col_japanese];
    else if (gSheet.col_english >= 0) src = r->cells[gSheet.col_english];
    extract_placeholders(src, out, cap);
    if (!out[0]) extract_placeholders(r->cells[gSheet.col_chinese], out, cap);
}

static int placeholders_ok(TextRow *r, const wchar_t *new_chinese)
{
    wchar_t required[512], got[512];
    row_required_placeholders(r, required, 512);
    extract_placeholders(new_chinese, got, 512);
    return wcscmp(required, got) == 0;
}

static int set_row_chinese(int row_index, const wchar_t *escaped, int warn)
{
    TextRow *r;
    if (row_index < 0 || row_index >= gSheet.nrows || gSheet.col_chinese < 0) return 0;
    r = &gSheet.rows[row_index];
    if (!placeholders_ok(r, escaped)) {
        if (warn) {
            wchar_t required[512], got[512], detail[2048];
            row_required_placeholders(r, required, 512);
            extract_placeholders(escaped, got, 512);
            _snwprintf(detail, 2048,
                L"占位符被修改或丢失，已拒绝修改。\r\n"
                L"位置：ID %d，%s:%s，key=%s\r\n"
                L"应保留：%s\r\n"
                L"当前包含：%s\r\n"
                L"请保留 %1!s!、%2!d! 这类占位符。",
                r->id,
                cell_or_empty(r, gSheet.col_file),
                cell_or_empty(r, gSheet.col_line),
                cell_or_empty(r, gSheet.col_key),
                required[0] ? required : L"无",
                got[0] ? got : L"无");
            msg(gMain, MB_ICONERROR, detail);
        }
        return 0;
    }
    if (wcscmp(r->cells[gSheet.col_chinese], escaped) == 0) {
        update_edit_safety();
        return 1;
    }
    free(r->cells[gSheet.col_chinese]);
    r->cells[gSheet.col_chinese] = xwcsdup(escaped);
    r->dirty = 1;
    update_edit_safety();
    return 1;
}

static void refresh_text_list(void)
{
    wchar_t q[256] = L"", count[128];
    int dirty_only = 0, shown = 0, dirty = 0;
    if (!gEditList || !gSheet.loaded) return;
    GetWindowTextW(gEditSearch, q, 256);
    dirty_only = SendMessageW(gEditDirtyOnly, BM_GETCHECK, 0, 0) == BST_CHECKED;
    SendMessageW(gEditList, WM_SETREDRAW, FALSE, 0);
    for (int i = 0; i < gSheet.nrows; i++) {
        TextRow *r = &gSheet.rows[i];
        if (r->dirty) dirty++;
        if (!row_matches_filter(r, q, dirty_only)) continue;
        gFilteredRows[shown] = i;
        shown++;
    }
    gFilteredCount = shown;
    ListView_SetItemCount(gEditList, shown);
    SendMessageW(gEditList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(gEditList, NULL, TRUE);
    _snwprintf(count, 128, L"显示 %d / 共 %d，已改 %d", shown, gSheet.nrows, dirty);
    SetWindowTextW(gEditCount, count);
    update_edit_safety();
}

static int list_selected_row(void)
{
    int item = ListView_GetNextItem(gEditList, -1, LVNI_SELECTED);
    if (item < 0) return -1;
    if (item >= gFilteredCount) return -1;
    return gFilteredRows[item];
}

static void load_editor_row(int row)
{
    TextRow *r;
    wchar_t *text;
    if (row < 0 || row >= gSheet.nrows) return;
    r = &gSheet.rows[row];
    gEditorCurrentRow = row;
    gEditorProgrammatic = 1;
    SetWindowTextW(gEditOriginal,
        gSheet.basis_jp && gSheet.col_japanese >= 0 ? r->cells[gSheet.col_japanese] :
        (gSheet.col_english >= 0 ? r->cells[gSheet.col_english] : L""));
    text = escaped_to_editor(r->cells[gSheet.col_chinese]);
    SetWindowTextW(gEditChinese, text ? text : L"");
    free(text);
    gEditorProgrammatic = 0;
}

static void commit_big_editor(void)
{
    int len;
    wchar_t *buf, *esc;
    if (gEditorProgrammatic || gEditorCurrentRow < 0) return;
    len = GetWindowTextLengthW(gEditChinese);
    buf = (wchar_t *)calloc((size_t)len + 1, sizeof(wchar_t));
    if (!buf) return;
    GetWindowTextW(gEditChinese, buf, len + 1);
    esc = editor_to_escaped(buf);
    if (esc && set_row_chinese(gEditorCurrentRow, esc, 1)) refresh_text_list();
    free(esc);
    free(buf);
}

static void commit_big_editor_light(void)
{
    int len;
    wchar_t *buf, *esc;
    TextRow *r;
    if (gEditorProgrammatic || gEditorCurrentRow < 0) return;
    len = GetWindowTextLengthW(gEditChinese);
    buf = (wchar_t *)calloc((size_t)len + 1, sizeof(wchar_t));
    if (!buf) return;
    GetWindowTextW(gEditChinese, buf, len + 1);
    esc = editor_to_escaped(buf);
    if (esc && gEditorCurrentRow >= 0 && gEditorCurrentRow < gSheet.nrows) {
        r = &gSheet.rows[gEditorCurrentRow];
        if (wcscmp(r->cells[gSheet.col_chinese], esc) != 0) {
            free(r->cells[gSheet.col_chinese]);
            r->cells[gSheet.col_chinese] = xwcsdup(esc);
            r->dirty = 1;
        }
        update_edit_safety();
    }
    free(esc);
    free(buf);
    InvalidateRect(gEditList, NULL, TRUE);
}

static void commit_cell_edit(void)
{
    int len;
    wchar_t *buf;
    if (!gCellEdit || gCellEditRow < 0) return;
    len = GetWindowTextLengthW(gCellEdit);
    buf = (wchar_t *)calloc((size_t)len + 1, sizeof(wchar_t));
    if (buf) {
        GetWindowTextW(gCellEdit, buf, len + 1);
        set_row_chinese(gCellEditRow, buf, 1);
        free(buf);
    }
    DestroyWindow(gCellEdit);
    gCellEdit = NULL;
    gCellEditRow = -1;
    InvalidateRect(gEditList, NULL, TRUE);
    if (gEditorCurrentRow >= 0) load_editor_row(gEditorCurrentRow);
}

static void begin_cell_edit(HWND hwnd, int item, int subitem)
{
    RECT rc;
    int row;
    if (subitem != 2 || item < 0) return;
    commit_cell_edit();
    if (item >= gFilteredCount) return;
    row = gFilteredRows[item];
    rc.top = subitem;
    rc.left = LVIR_BOUNDS;
    if (!SendMessageW(gEditList, LVM_GETSUBITEMRECT, (WPARAM)item, (LPARAM)&rc)) return;
    MapWindowPoints(gEditList, hwnd, (POINT *)&rc, 2);
    gCellEdit = CreateWindowExW(0, L"EDIT", gSheet.rows[row].cells[gSheet.col_chinese],
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
        hwnd, (HMENU)(INT_PTR)IDC_CELL_EDIT, gInst, NULL);
    SendMessageW(gCellEdit, WM_SETFONT, (WPARAM)gUiFont, TRUE);
    gCellEditRow = row;
    SetFocus(gCellEdit);
    SendMessageW(gCellEdit, EM_SETSEL, 0, -1);
}

static int save_text_sheet(HWND hwnd)
{
    size_t cap = 1024, len = 0;
    wchar_t *out = (wchar_t *)calloc(cap, sizeof(wchar_t));
    if (!gSheet.loaded || !out) return 0;
    commit_big_editor();
    commit_cell_edit();
    update_edit_safety();
    if (gEditHasUnsafeVoiceLine) {
        free(out);
        msg(hwnd, MB_ICONERROR, L"存在超过 76 GBK 字节的文本行，不能保存。");
        return 0;
    }
    for (int c = 0; c < gSheet.ncols; c++) {
        const wchar_t *cell = gSheet.headers[c] ? gSheet.headers[c] : L"";
        size_t need = wcslen(cell) + 3;
        if (len + need >= cap) { cap = (cap + need) * 2; out = (wchar_t *)realloc(out, cap * sizeof(wchar_t)); }
        if (c) out[len++] = L'\t';
        wcscpy_s(out + len, cap - len, cell); len += wcslen(cell);
    }
    out[len++] = L'\n'; out[len] = 0;
    for (int r = 0; r < gSheet.nrows; r++) {
        for (int c = 0; c < gSheet.ncols; c++) {
            const wchar_t *cell = gSheet.rows[r].cells[c] ? gSheet.rows[r].cells[c] : L"";
            size_t need = wcslen(cell) + 3;
            if (len + need >= cap) { cap = (cap + need) * 2; out = (wchar_t *)realloc(out, cap * sizeof(wchar_t)); }
            if (c) out[len++] = L'\t';
            wcscpy_s(out + len, cap - len, cell); len += wcslen(cell);
        }
        out[len++] = L'\n'; out[len] = 0;
    }
    if (!write_utf8_file(gSheet.path, out)) {
        free(out);
        msg(hwnd, MB_ICONERROR, L"保存 TSV 失败。");
        return 0;
    }
    free(out);
    for (int r = 0; r < gSheet.nrows; r++) gSheet.rows[r].dirty = 0;
    refresh_text_list();
    msg(hwnd, MB_ICONINFORMATION, L"译文已保存到 TSV。");
    return 1;
}

static void quote_arg(wchar_t *out, size_t cap, const wchar_t *arg)
{
    wcscat_s(out, cap, L"\"");
    wcscat_s(out, cap, arg);
    wcscat_s(out, cap, L"\"");
}

static int run_wait(HWND hwnd, const wchar_t *cmd, const wchar_t *fail_msg)
{
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD code = 1;
    wchar_t *mutable_cmd = xwcsdup(cmd);
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (!CreateProcessW(NULL, mutable_cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        free(mutable_cmd);
        msg(hwnd, MB_ICONERROR, fail_msg);
        return 0;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    free(mutable_cmd);
    if (code != 0) {
        msg(hwnd, MB_ICONERROR, fail_msg);
        return 0;
    }
    return 1;
}

static void timestamp_suffix(wchar_t *out, size_t cap)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    _snwprintf(out, cap, L"%04d%02d%02d_%02d%02d%02d",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    out[cap - 1] = 0;
}

static int import_text_to_game(HWND hwnd)
{
    wchar_t tool_dir[MAX_PATH], work_root[MAX_PATH], outdir[MAX_PATH], outpod[MAX_PATH], err[1024];
    wchar_t curpod[MAX_PATH], bakdir[MAX_PATH], bakpod[MAX_PATH], stamp[32], variant[MAX_PATH];
    BrTextEdit *edits = NULL;
    int basis_jp;
    int edit_count = 0;

    if (!gSheet.loaded) {
        msg(hwnd, MB_ICONERROR, L"请先载入文本表。");
        return 0;
    }
    commit_big_editor();
    commit_cell_edit();
    update_edit_safety();
    if (gEditHasUnsafeVoiceLine) {
        msg(hwnd, MB_ICONERROR, L"存在超过 76 GBK 字节的文本行，不能导入游戏。");
        return 0;
    }
    if (!is_game_dir(gGameDir)) {
        msg(hwnd, MB_ICONERROR, L"未识别到游戏目录。");
        return 0;
    }
    if (!save_text_sheet(hwnd)) return 0;

    basis_jp = gSheet.basis_jp;
    get_tool_dir(tool_dir, MAX_PATH);

    timestamp_suffix(stamp, 32);
    _snwprintf(work_root, MAX_PATH, L"%s\\_work", tool_dir);
    work_root[MAX_PATH - 1] = 0;
    CreateDirectoryW(work_root, NULL);
    SetFileAttributesW(work_root, FILE_ATTRIBUTE_HIDDEN);
    _snwprintf(outdir, MAX_PATH, L"%s\\import_%s", work_root, stamp);
    outdir[MAX_PATH - 1] = 0;
    CreateDirectoryW(outdir, NULL);
    SetFileAttributesW(outdir, FILE_ATTRIBUTE_HIDDEN);
    game_path(curpod, MAX_PATH, L"JAPANESE.POD");
    game_path(bakdir, MAX_PATH, L"zh_cn_tools\\backups");
    CreateDirectoryW(bakdir, NULL);
    _snwprintf(bakpod, MAX_PATH, L"%s\\JAPANESE.POD.before_text_import_%s", bakdir, stamp);
    bakpod[MAX_PATH - 1] = 0;
    if (!CopyFileW(curpod, bakpod, TRUE)) {
        msg(hwnd, MB_ICONERROR, L"备份当前 JAPANESE.POD 失败，已停止导入。");
        return 0;
    }

    edits = (BrTextEdit *)calloc((size_t)gSheet.nrows, sizeof(BrTextEdit));
    if (!edits) {
        msg(hwnd, MB_ICONERROR, L"内存不足，无法导入。");
        return 0;
    }
    for (int i = 0; i < gSheet.nrows; i++) {
        TextRow *r = &gSheet.rows[i];
        int line_no;
        if (gSheet.col_file < 0 || gSheet.col_line < 0 || gSheet.col_chinese < 0) continue;
        if (!r->cells[gSheet.col_file] || !r->cells[gSheet.col_file][0]) continue;
        if (!r->cells[gSheet.col_line] || !r->cells[gSheet.col_line][0]) continue;
        line_no = (wcscmp(r->cells[gSheet.col_line], L"+") == 0) ? -1 : _wtoi(r->cells[gSheet.col_line]);
        edits[edit_count].file = r->cells[gSheet.col_file];
        edits[edit_count].line = line_no;
        edits[edit_count].english = cell_or_empty(r, gSheet.col_english);
        edits[edit_count].japanese = cell_or_empty(r, gSheet.col_japanese);
        edits[edit_count].chinese = cell_or_empty(r, gSheet.col_chinese);
        edit_count++;
    }

    _snwprintf(outpod, MAX_PATH, L"%s\\JAPANESE.POD", outdir);
    outpod[MAX_PATH - 1] = 0;
    if (!brfont_import_texts(gGameDir, basis_jp, edits, edit_count, outpod, err, 1024)) {
        free(edits);
        msg(hwnd, MB_ICONERROR, err[0] ? err : L"生成并校验 POD 失败，已停止覆盖游戏目录。");
        return 0;
    }
    free(edits);

    text_variant_path(basis_jp, variant, MAX_PATH);
    if (!CopyFileW(outpod, variant, FALSE) || !CopyFileW(outpod, curpod, FALSE)) {
        msg(hwnd, MB_ICONERROR, L"校验已通过，但覆盖游戏 POD 失败。请先关闭游戏；如果仍失败，请以管理员身份运行本工具。");
        return 0;
    }
    msg(hwnd, MB_ICONINFORMATION, L"译文已导入游戏。\r\n导入前的 JAPANESE.POD 已备份到 zh_cn_tools\\backups。");
    update_status();
    return 1;
}

static int CALLBACK enum_font_cb(const LOGFONTW *lf, const TEXTMETRICW *tm,
    DWORD type, LPARAM lp)
{
    (void)lf;
    (void)tm;
    (void)type;
    *(int *)lp = 1;
    return 0;
}

static int font_available(const wchar_t *face)
{
    HDC dc = GetDC(NULL);
    LOGFONTW lf;
    int found = 0;
    ZeroMemory(&lf, sizeof(lf));
    lf.lfCharSet = DEFAULT_CHARSET;
    wcsncpy(lf.lfFaceName, face, LF_FACESIZE - 1);
    EnumFontFamiliesExW(dc, &lf, enum_font_cb, (LPARAM)&found, 0);
    ReleaseDC(NULL, dc);
    return found;
}

static int CALLBACK enum_tool_font_cb(const LOGFONTW *lf, const TEXTMETRICW *tm,
    DWORD type, LPARAM lp)
{
    HWND combo = (HWND)lp;
    (void)tm;
    if (!(type & TRUETYPE_FONTTYPE)) return 1;
    if (!lf->lfFaceName[0] || lf->lfFaceName[0] == L'@') return 1;
    if (SendMessageW(combo, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)lf->lfFaceName) == CB_ERR)
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)lf->lfFaceName);
    return 1;
}

static void populate_game_font_combo(void)
{
    HDC dc;
    LOGFONTW lf;
    wchar_t keep[LF_FACESIZE] = L"";
    int sel;
    if (!gFontCombo) return;
    sel = (int)SendMessageW(gFontCombo, CB_GETCURSEL, 0, 0);
    if (sel >= 0)
        SendMessageW(gFontCombo, CB_GETLBTEXT, sel, (LPARAM)keep);
    SendMessageW(gFontCombo, CB_RESETCONTENT, 0, 0);
    ZeroMemory(&lf, sizeof(lf));
    lf.lfCharSet = DEFAULT_CHARSET;
    dc = GetDC(NULL);
    EnumFontFamiliesExW(dc, &lf, enum_tool_font_cb, (LPARAM)gFontCombo, 0);
    ReleaseDC(NULL, dc);
    if (keep[0]) {
        sel = (int)SendMessageW(gFontCombo, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)keep);
        if (sel >= 0) {
            SendMessageW(gFontCombo, CB_SETCURSEL, sel, 0);
            return;
        }
    }
    sel = (int)SendMessageW(gFontCombo, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)L"Noto Sans SC");
    if (sel < 0) sel = 0;
    SendMessageW(gFontCombo, CB_SETCURSEL, sel, 0);
}

static int selected_game_font(wchar_t *face, size_t cap)
{
    int sel = (int)SendMessageW(gFontCombo, CB_GETCURSEL, 0, 0);
    face[0] = 0;
    if (sel < 0) return 0;
    SendMessageW(gFontCombo, CB_GETLBTEXT, sel, (LPARAM)face);
    face[cap - 1] = 0;
    return face[0] != 0;
}

static void update_font_preview(const wchar_t *face)
{
    HDC dc;
    int dpi;
    HFONT font;
    if (!gFontPreview || !face || !face[0]) return;
    dc = GetDC(NULL);
    dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(NULL, dc);
    font = CreateFontW(-MulDiv(18, dpi, 72), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        face);
    if (!font) return;
    SendMessageW(gFontPreview, WM_SETFONT, (WPARAM)font, TRUE);
    if (gPreviewFont) DeleteObject(gPreviewFont);
    gPreviewFont = font;
}

static void refresh_font_result(void)
{
    wchar_t face[LF_FACESIZE], info[512], missing[128], example[512];
    BrFontReport report;
    gFontCanApply = 0;
    gFontMissingBad = 0;
    if (gFontApply) EnableWindow(gFontApply, FALSE);
    if (gFontMissing) SetWindowTextW(gFontMissing, L"");
    if (gFontExample) SetWindowTextW(gFontExample, L"");
    if (!gFontInfo || !gFontCombo) return;
    if (!selected_game_font(face, LF_FACESIZE)) {
        SetWindowTextW(gFontInfo, L"未选择字体。");
        return;
    }
    update_font_preview(face);
    if (!is_game_dir(gGameDir)) {
        _snwprintf(info, 512, L"字体：%s\r\n未识别到游戏目录，无法检查缺字。", face);
        SetWindowTextW(gFontInfo, info);
        return;
    }
    if (!brfont_check_game(gGameDir, face, &report)) {
        _snwprintf(info, 512, L"字体：%s\r\n检查失败。", face);
        SetWindowTextW(gFontInfo, info);
        return;
    }
    _snwprintf(info, 512, L"字体：%s\r\n游戏用字：%d", face, report.glyphs);
    SetWindowTextW(gFontInfo, info);
    if (report.missing) {
        gFontMissingBad = 1;
        _snwprintf(missing, 128, L"缺字：%d", report.missing);
        _snwprintf(example, 512, L"缺字示例：%s", report.sample[0] ? report.sample : L"无");
        if (gFontMissing) SetWindowTextW(gFontMissing, missing);
        if (gFontExample) SetWindowTextW(gFontExample, example);
        if (gFontMissing) InvalidateRect(gFontMissing, NULL, TRUE);
        return;
    }
    if (gFontMissing) SetWindowTextW(gFontMissing, L"缺字：0");
    if (gFontExample) SetWindowTextW(gFontExample, L"缺字示例：无");
    if (gFontMissing) InvalidateRect(gFontMissing, NULL, TRUE);
    gFontCanApply = 1;
    if (gFontApply) EnableWindow(gFontApply, TRUE);
}

static int choose_external_font(HWND hwnd)
{
    OPENFILENAMEW ofn;
    wchar_t path[MAX_PATH] = L"";
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"字体文件 (*.ttf;*.otf;*.ttc)\0*.ttf;*.otf;*.ttc\0所有文件\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return 0;
    if (gExternalFontPath[0])
        RemoveFontResourceExW(gExternalFontPath, FR_PRIVATE, 0);
    if (!AddFontResourceExW(path, FR_PRIVATE, 0)) {
        msg(hwnd, MB_ICONERROR, L"无法加载这个字体文件。");
        gExternalFontPath[0] = 0;
        return 0;
    }
    wcsncpy(gExternalFontPath, path, MAX_PATH - 1);
    gExternalFontPath[MAX_PATH - 1] = 0;
    SendMessageW(HWND_BROADCAST, WM_FONTCHANGE, 0, 0);
    populate_game_font_combo();
    refresh_font_result();
    return 1;
}

static int check_game_font(HWND hwnd)
{
    wchar_t face[LF_FACESIZE], info[512];
    BrFontReport report;
    if (!is_game_dir(gGameDir)) {
        SetWindowTextW(gFontInfo, L"未识别到游戏目录。");
        return 0;
    }
    if (!selected_game_font(face, LF_FACESIZE)) {
        SetWindowTextW(gFontInfo, L"请选择字体。");
        return 0;
    }
    if (!brfont_check_game(gGameDir, face, &report)) {
        SetWindowTextW(gFontInfo, L"检查字体失败。");
        return 0;
    }
    if (report.missing) {
        _snwprintf(info, 512, L"字体：%s\r\n游戏用字：%d\r\n缺字：%d\r\n示例：%s",
            face, report.glyphs, report.missing, report.sample);
        SetWindowTextW(gFontInfo, info);
        return 0;
    }
    _snwprintf(info, 512, L"字体：%s\r\n游戏用字：%d\r\n缺字：0\r\n可以应用。", face, report.glyphs);
    SetWindowTextW(gFontInfo, info);
    return 1;
}

static int apply_game_font(HWND hwnd)
{
    wchar_t face[LF_FACESIZE], err[1024], info[512];
    BrFontReport report;
    if (!is_game_dir(gGameDir)) {
        msg(hwnd, MB_ICONERROR, L"未识别到游戏目录。");
        return 0;
    }
    if (!selected_game_font(face, LF_FACESIZE)) {
        msg(hwnd, MB_ICONERROR, L"请选择字体。");
        return 0;
    }
    if (!brfont_check_game(gGameDir, face, &report)) {
        SetWindowTextW(gFontInfo, L"检查字体失败。");
        return 0;
    }
    if (report.missing) {
        _snwprintf(err, 1024, L"字体缺少 %d 个游戏用字，不能应用。\r\n示例：%s", report.missing, report.sample);
        SetWindowTextW(gFontInfo, err);
        return 0;
    }
    if (!brfont_apply_game(gGameDir, face, err, 1024)) {
        msg(hwnd, MB_ICONERROR, err[0] ? err : L"应用字体失败。");
        return 0;
    }
    _snwprintf(info, 512, L"已应用字体：%s\r\n已更新当前 JAPANESE.POD 和两个文本变体。", face);
    SetWindowTextW(gFontInfo, info);
    msg(hwnd, MB_ICONINFORMATION, L"字体已应用。请完全退出游戏后重新启动。");
    update_status();
    return 1;
}

static int restore_default_font(HWND hwnd)
{
    wchar_t src_en[MAX_PATH], src_jp[MAX_PATH], dst_en[MAX_PATH], dst_jp[MAX_PATH], dst_root[MAX_PATH];
    wchar_t err[1024];
    int keep = text_state();
    if (!is_game_dir(gGameDir)) {
        msg(hwnd, MB_ICONERROR, L"未识别到游戏目录。");
        return 0;
    }
    if (keep < 0)
        keep = (int)SendMessageW(gTextCombo, CB_GETCURSEL, 0, 0) == 1 ? 1 : 0;
    if (!default_text_variant_path(0, src_en, MAX_PATH) ||
        !default_text_variant_path(1, src_jp, MAX_PATH)) {
        msg(hwnd, MB_ICONERROR, L"缺少默认字体备份文件，无法恢复。");
        return 0;
    }
    text_variant_path(0, dst_en, MAX_PATH);
    text_variant_path(1, dst_jp, MAX_PATH);
    game_path(dst_root, MAX_PATH, L"JAPANESE.POD");
    if (!CopyFileW(src_en, dst_en, FALSE) ||
        !CopyFileW(src_jp, dst_jp, FALSE)) {
        _snwprintf(err, 1024, L"无法恢复文本变体。\r\n请先关闭游戏；如果仍失败，请以管理员身份运行本工具。");
        msg(hwnd, MB_ICONERROR, err);
        return 0;
    }
    if (!CopyFileW(keep ? dst_jp : dst_en, dst_root, FALSE)) {
        _snwprintf(err, 1024, L"无法覆盖 JAPANESE.POD。\r\n请先关闭游戏；如果仍失败，请以管理员身份运行本工具。");
        msg(hwnd, MB_ICONERROR, err);
        return 0;
    }
    SetWindowTextW(gFontInfo, L"已恢复默认字体。\r\n已更新当前 JAPANESE.POD 和两个文本变体。");
    if (gFontMissing) SetWindowTextW(gFontMissing, L"");
    if (gFontExample) SetWindowTextW(gFontExample, L"");
    gFontMissingBad = 0;
    msg(hwnd, MB_ICONINFORMATION, L"默认字体已恢复。请完全退出游戏后重新启动。");
    update_status();
    refresh_font_result();
    return 1;
}

static void load_embedded_font(void)
{
    HRSRC res = FindResourceW(gInst, MAKEINTRESOURCEW(IDR_NOTO_SC), L"RCDATA");
    if (!res) return;
    HGLOBAL hg = LoadResource(gInst, res);
    DWORD size = SizeofResource(gInst, res);
    void *ptr = LockResource(hg);
    DWORD nfonts = 0;
    if (ptr && size)
        gFontMem = AddFontMemResourceEx(ptr, size, NULL, &nfonts);
}

static HFONT create_ui_font(void)
{
    const wchar_t *faces[] = {
        L"Noto Sans SC",
        L"Microsoft YaHei UI",
        L"Microsoft YaHei",
        L"DengXian",
        L"SimSun",
        L"Segoe UI",
    };
    HDC dc = GetDC(NULL);
    int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(NULL, dc);

    for (int i = 0; i < (int)(sizeof(faces) / sizeof(faces[0])); i++) {
        if (!font_available(faces[i])) continue;
        HFONT font = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            faces[i]);
        if (font) return font;
    }
    return (HFONT)GetStockObject(DEFAULT_GUI_FONT);
}

static void show_tab(int idx)
{
    int sw = (idx == 0);
    int edit = (idx == 1);
    int font = (idx == 2);
    int repair = (idx == 3);
    ShowWindow(gTextLabel, sw ? SW_SHOW : SW_HIDE);
    ShowWindow(gTextCombo, sw ? SW_SHOW : SW_HIDE);
    ShowWindow(gVoiceLabel, sw ? SW_SHOW : SW_HIDE);
    ShowWindow(gVoiceCombo, sw ? SW_SHOW : SW_HIDE);
    ShowWindow(gVideoLabel, sw ? SW_SHOW : SW_HIDE);
    ShowWindow(gVideoCombo, sw ? SW_SHOW : SW_HIDE);
    ShowWindow(gApplySwitch, sw ? SW_SHOW : SW_HIDE);
    ShowWindow(gSwitchInfo, SW_HIDE);
    ShowWindow(gEditBasis, edit ? SW_SHOW : SW_HIDE);
    ShowWindow(gEditSearch, edit ? SW_SHOW : SW_HIDE);
    ShowWindow(gEditDirtyOnly, edit ? SW_SHOW : SW_HIDE);
    ShowWindow(gEditList, edit ? SW_SHOW : SW_HIDE);
    ShowWindow(gEditWarn, edit ? SW_SHOW : SW_HIDE);
    ShowWindow(gEditOriginalLabel, edit ? SW_SHOW : SW_HIDE);
    ShowWindow(gEditOriginal, edit ? SW_SHOW : SW_HIDE);
    ShowWindow(gEditChineseLabel, edit ? SW_SHOW : SW_HIDE);
    ShowWindow(gEditChinese, edit ? SW_SHOW : SW_HIDE);
    ShowWindow(gEditSave, edit ? SW_SHOW : SW_HIDE);
    ShowWindow(gEditImport, edit ? SW_SHOW : SW_HIDE);
    ShowWindow(gEditCount, edit ? SW_SHOW : SW_HIDE);
    ShowWindow(gDpiEnable, repair ? SW_SHOW : SW_HIDE);
    ShowWindow(gDpiRestore, repair ? SW_SHOW : SW_HIDE);
    ShowWindow(gDpiInfo, repair ? SW_SHOW : SW_HIDE);
    ShowWindow(gDpiNote, repair ? SW_SHOW : SW_HIDE);
    ShowWindow(gLaaEnable, repair ? SW_SHOW : SW_HIDE);
    ShowWindow(gLaaRestore, repair ? SW_SHOW : SW_HIDE);
    ShowWindow(gLaaInfo, repair ? SW_SHOW : SW_HIDE);
    ShowWindow(gLaaNote, repair ? SW_SHOW : SW_HIDE);
    ShowWindow(gFontCombo, font ? SW_SHOW : SW_HIDE);
    ShowWindow(gFontRefresh, font ? SW_SHOW : SW_HIDE);
    ShowWindow(gFontFile, font ? SW_SHOW : SW_HIDE);
    ShowWindow(gFontApply, font ? SW_SHOW : SW_HIDE);
    ShowWindow(gFontReset, font ? SW_SHOW : SW_HIDE);
    ShowWindow(gFontInfo, font ? SW_SHOW : SW_HIDE);
    ShowWindow(gFontMissing, font ? SW_SHOW : SW_HIDE);
    ShowWindow(gFontExample, font ? SW_SHOW : SW_HIDE);
    ShowWindow(gFontNote, font ? SW_SHOW : SW_HIDE);
    ShowWindow(gFontPreview, font ? SW_SHOW : SW_HIDE);
    if (edit && !gSheet.loaded) {
        gEditorBasis = (int)SendMessageW(gEditBasis, CB_GETCURSEL, 0, 0);
        if (load_text_sheet(gEditorBasis == 1)) refresh_text_list();
        else SetWindowTextW(gEditCount, L"未找到文本表。");
    }
    if (font) refresh_font_result();
}

static void create_ui(HWND hwnd)
{
    TCITEMW ti;

    mk(hwnd, L"STATIC", L"游戏目录", 0, 22, 26, 76, 24, 0);
    gPathCtl = mk(hwnd, L"EDIT", L"", ES_AUTOHSCROLL | ES_READONLY, 102, 22, 610, 26, IDC_GAME_PATH);
    mk(hwnd, L"BUTTON", L"浏览...", BS_PUSHBUTTON, 740, 21, 92, 28, IDC_BROWSE);

    gTab = mk(hwnd, WC_TABCONTROLW, L"", 0, 18, 68, 814, 515, IDC_TAB);
    ZeroMemory(&ti, sizeof(ti));
    ti.mask = TCIF_TEXT;
    ti.pszText = L"切换";
    TabCtrl_InsertItem(gTab, 0, &ti);
    ti.pszText = L"文本编辑";
    TabCtrl_InsertItem(gTab, 1, &ti);
    ti.pszText = L"游戏字体";
    TabCtrl_InsertItem(gTab, 2, &ti);
    ti.pszText = L"游戏修复";
    TabCtrl_InsertItem(gTab, 3, &ti);

    gSwitchInfo = mk(hwnd, L"STATIC", L"", 0, 48, 112, 320, 72, IDC_SWITCH_INFO);
    gTextLabel = mk(hwnd, L"STATIC", L"文本", 0, 140, 140, 90, 24, 0);
    gTextCombo = mk(hwnd, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, 250, 136, 330, 120, IDC_TEXT_COMBO);
    SendMessageW(gTextCombo, CB_ADDSTRING, 0, (LPARAM)L"基于英文");
    SendMessageW(gTextCombo, CB_ADDSTRING, 0, (LPARAM)L"基于日文");

    gVoiceLabel = mk(hwnd, L"STATIC", L"语音", 0, 140, 190, 90, 24, 0);
    gVoiceCombo = mk(hwnd, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, 250, 186, 330, 120, IDC_VOICE_COMBO);
    SendMessageW(gVoiceCombo, CB_ADDSTRING, 0, (LPARAM)L"英文语音");
    SendMessageW(gVoiceCombo, CB_ADDSTRING, 0, (LPARAM)L"日文语音");

    gVideoLabel = mk(hwnd, L"STATIC", L"视频字幕", 0, 140, 240, 90, 24, 0);
    gVideoCombo = mk(hwnd, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, 250, 236, 330, 120, IDC_VIDEO_COMBO);
    SendMessageW(gVideoCombo, CB_ADDSTRING, 0, (LPARAM)L"基于英文");
    SendMessageW(gVideoCombo, CB_ADDSTRING, 0, (LPARAM)L"基于日文");

    gApplySwitch = mk(hwnd, L"BUTTON", L"应用切换", BS_PUSHBUTTON, 250, 310, 330, 42, IDC_APPLY_SWITCH);

    gEditBasis = mk(hwnd, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, 48, 104, 150, 100, IDC_EDIT_BASIS);
    SendMessageW(gEditBasis, CB_ADDSTRING, 0, (LPARAM)L"英文文本");
    SendMessageW(gEditBasis, CB_ADDSTRING, 0, (LPARAM)L"日文文本");
    SendMessageW(gEditBasis, CB_SETCURSEL, 0, 0);
    gEditSearch = mk(hwnd, L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER, 215, 104, 410, 26, IDC_EDIT_SEARCH);
    gEditDirtyOnly = mk(hwnd, L"BUTTON", L"只看改过的", BS_AUTOCHECKBOX, 645, 106, 120, 24, IDC_EDIT_DIRTY_ONLY);
    gEditCount = mk(hwnd, L"STATIC", L"", 0, 48, 138, 740, 24, IDC_EDIT_COUNT);
    gEditList = mk(hwnd, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_OWNERDATA | WS_BORDER, 48, 166, 740, 240, IDC_EDIT_LIST);
    ListView_SetExtendedListViewStyle(gEditList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    LVCOLUMNW col;
    ZeroMemory(&col, sizeof(col));
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    col.pszText = L"ID"; col.cx = 55; col.iSubItem = 0; ListView_InsertColumn(gEditList, 0, &col);
    col.pszText = L"原文"; col.cx = 335; col.iSubItem = 1; ListView_InsertColumn(gEditList, 1, &col);
    col.pszText = L"中文译文"; col.cx = 335; col.iSubItem = 2; ListView_InsertColumn(gEditList, 2, &col);
    gEditWarn = mk(hwnd, L"STATIC", L"文本单行限制：每行不超过 76 GBK 字节。", 0, 48, 414, 740, 22, IDC_EDIT_WARN);
    gEditOriginalLabel = mk(hwnd, L"STATIC", L"原文", 0, 48, 442, 60, 20, 0);
    gEditOriginal = mk(hwnd, L"EDIT", L"", ES_MULTILINE | ES_READONLY | WS_BORDER | WS_VSCROLL, 48, 464, 360, 76, IDC_EDIT_ORIGINAL);
    gEditChineseLabel = mk(hwnd, L"STATIC", L"中文译文", 0, 428, 442, 90, 20, 0);
    gEditChinese = mk(hwnd, L"EDIT", L"", ES_MULTILINE | WS_BORDER | WS_VSCROLL | ES_AUTOVSCROLL, 428, 464, 360, 76, IDC_EDIT_CHINESE);
    gEditSave = mk(hwnd, L"BUTTON", L"保存译文", BS_PUSHBUTTON, 428, 550, 160, 34, IDC_EDIT_SAVE);
    gEditImport = mk(hwnd, L"BUTTON", L"导入游戏", BS_PUSHBUTTON, 628, 550, 160, 34, IDC_EDIT_IMPORT);

    gDpiInfo = mk(hwnd, L"STATIC", L"", 0, 90, 125, 650, 28, IDC_DPI_INFO);
    gDpiEnable = mk(hwnd, L"BUTTON", L"启用 DPI 修复", BS_PUSHBUTTON, 130, 168, 240, 42, IDC_DPI_ENABLE);
    gDpiRestore = mk(hwnd, L"BUTTON", L"恢复 DPI 默认", BS_PUSHBUTTON, 430, 168, 240, 42, IDC_DPI_RESTORE);
    gDpiNote = mk(hwnd, L"STATIC", L"说明：修复会写入当前用户的 Windows 兼容性设置；恢复默认会移除本工具写入的 DPI 标记。", 0,
        130, 225, 680, 24, 0);
    gLaaInfo = mk(hwnd, L"STATIC", L"", 0, 90, 302, 650, 28, IDC_LAA_INFO);
    gLaaEnable = mk(hwnd, L"BUTTON", L"启用 4GB 补丁", BS_PUSHBUTTON, 130, 345, 240, 42, IDC_LAA_ENABLE);
    gLaaRestore = mk(hwnd, L"BUTTON", L"恢复原版 exe", BS_PUSHBUTTON, 430, 345, 240, 42, IDC_LAA_RESTORE);
    gLaaNote = mk(hwnd, L"STATIC", L"说明：4GB 补丁会给 rayne1.exe 开启 Large Address Aware；首次启用前会自动备份原版 exe。", 0,
        130, 402, 680, 24, 0);

    gFontCombo = mk(hwnd, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, 48, 104, 430, 220, IDC_FONT_COMBO);
    gFontRefresh = mk(hwnd, L"BUTTON", L"刷新字体", BS_PUSHBUTTON, 500, 102, 110, 30, IDC_FONT_REFRESH);
    gFontFile = mk(hwnd, L"BUTTON", L"选择字体文件...", BS_PUSHBUTTON, 630, 102, 160, 30, IDC_FONT_FILE);
    gFontPreview = mk(hwnd, L"STATIC", L"《吸血莱恩》简体中文汉化包", SS_CENTER | SS_CENTERIMAGE | WS_BORDER,
        48, 154, 740, 70, IDC_FONT_PREVIEW);
    gFontInfo = mk(hwnd, L"STATIC", L"", 0, 48, 240, 420, 42, IDC_FONT_INFO);
    gFontMissing = mk(hwnd, L"STATIC", L"", 0, 500, 260, 220, 24, IDC_FONT_MISSING);
    gFontExample = mk(hwnd, L"EDIT", L"", ES_MULTILINE | ES_READONLY | WS_BORDER | WS_VSCROLL,
        48, 292, 740, 70, IDC_FONT_EXAMPLE);
    gFontApply = mk(hwnd, L"BUTTON", L"应用字体", BS_PUSHBUTTON, 48, 386, 180, 38, IDC_FONT_APPLY);
    gFontReset = mk(hwnd, L"BUTTON", L"恢复默认字体", BS_PUSHBUTTON, 260, 386, 180, 38, IDC_FONT_RESET);
    gFontNote = mk(hwnd, L"STATIC", L"说明：选择字体后自动检查缺字；应用会重建当前 JAPANESE.POD 和两个文本变体。", 0,
        48, 445, 740, 42, 0);
    populate_game_font_combo();

    gStatus = mk(hwnd, L"EDIT", L"", ES_MULTILINE | ES_READONLY | WS_BORDER, 18, 610, 814, 92, IDC_STATUS);

    EnumChildWindows(hwnd, set_child_font, (LPARAM)gUiFont);
    show_tab(0);
    update_status();
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msgid, WPARAM wp, LPARAM lp)
{
    switch (msgid) {
    case WM_CREATE:
        create_ui(hwnd);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_BROWSE:
            if (choose_game_dir(hwnd)) update_status();
            return 0;
        case IDC_EDIT_BASIS:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                commit_big_editor();
                commit_cell_edit();
                gEditorBasis = (int)SendMessageW(gEditBasis, CB_GETCURSEL, 0, 0);
                if (load_text_sheet(gEditorBasis == 1)) {
                    gEditorCurrentRow = -1;
                    SetWindowTextW(gEditOriginal, L"");
                    SetWindowTextW(gEditChinese, L"");
                    refresh_text_list();
                } else {
                    SetWindowTextW(gEditCount, L"未找到文本表。");
                }
            }
            return 0;
        case IDC_EDIT_SEARCH:
            if (HIWORD(wp) == EN_CHANGE) refresh_text_list();
            return 0;
        case IDC_EDIT_DIRTY_ONLY:
            refresh_text_list();
            return 0;
        case IDC_EDIT_CHINESE:
            if (HIWORD(wp) == EN_CHANGE) commit_big_editor_light();
            if (HIWORD(wp) == EN_KILLFOCUS) commit_big_editor();
            return 0;
        case IDC_EDIT_SAVE:
            save_text_sheet(hwnd);
            return 0;
        case IDC_EDIT_IMPORT:
            import_text_to_game(hwnd);
            return 0;
        case IDC_CELL_EDIT:
            if (HIWORD(wp) == EN_KILLFOCUS) commit_cell_edit();
            return 0;
        case IDC_APPLY_SWITCH:
            if (apply_switch(hwnd)) update_status();
            return 0;
        case IDC_DPI_ENABLE:
            if (write_dpi_tokens(hwnd, 1)) update_status();
            return 0;
        case IDC_DPI_RESTORE:
            if (write_dpi_tokens(hwnd, 0)) update_status();
            return 0;
        case IDC_LAA_ENABLE:
            if (patch_laa(hwnd)) update_status();
            return 0;
        case IDC_LAA_RESTORE:
            if (restore_laa(hwnd)) update_status();
            return 0;
        case IDC_FONT_REFRESH:
            populate_game_font_combo();
            refresh_font_result();
            return 0;
        case IDC_FONT_FILE:
            choose_external_font(hwnd);
            return 0;
        case IDC_FONT_APPLY:
            apply_game_font(hwnd);
            return 0;
        case IDC_FONT_RESET:
            restore_default_font(hwnd);
            return 0;
        case IDC_FONT_COMBO:
            if (HIWORD(wp) == CBN_SELCHANGE) refresh_font_result();
            return 0;
        }
        break;
    case WM_CTLCOLORSTATIC:
        if ((HWND)lp == gFontMissing && gFontMissingBad) {
            SetTextColor((HDC)wp, RGB(190, 0, 0));
            SetBkMode((HDC)wp, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
        if ((HWND)lp == gEditWarn && gEditHasUnsafeVoiceLine) {
            SetTextColor((HDC)wp, RGB(190, 0, 0));
            SetBkMode((HDC)wp, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
        break;
    case WM_NOTIFY:
        if (((NMHDR *)lp)->idFrom == IDC_TAB && ((NMHDR *)lp)->code == TCN_SELCHANGE) {
            commit_big_editor();
            commit_cell_edit();
            show_tab(TabCtrl_GetCurSel(gTab));
            return 0;
        }
        if (((NMHDR *)lp)->idFrom == IDC_EDIT_LIST) {
            NMHDR *hdr = (NMHDR *)lp;
            if (hdr->code == LVN_GETDISPINFO) {
                NMLVDISPINFOW *di = (NMLVDISPINFOW *)lp;
                static wchar_t idbuf[32];
                int row;
                if (!(di->item.mask & LVIF_TEXT)) return 0;
                if (di->item.iItem < 0 || di->item.iItem >= gFilteredCount) return 0;
                row = gFilteredRows[di->item.iItem];
                if (di->item.iSubItem == 0) {
                    _snwprintf(idbuf, 32, L"%d", gSheet.rows[row].id);
                    di->item.pszText = idbuf;
                } else if (di->item.iSubItem == 1) {
                    di->item.pszText =
                        gSheet.basis_jp && gSheet.col_japanese >= 0 ? gSheet.rows[row].cells[gSheet.col_japanese] :
                        (gSheet.col_english >= 0 ? gSheet.rows[row].cells[gSheet.col_english] : L"");
                } else if (di->item.iSubItem == 2) {
                    di->item.pszText = gSheet.rows[row].cells[gSheet.col_chinese];
                }
                return 0;
            }
            if (hdr->code == LVN_ITEMCHANGED) {
                NMLISTVIEW *lv = (NMLISTVIEW *)lp;
                if ((lv->uChanged & LVIF_STATE) && (lv->uNewState & LVIS_SELECTED)) {
                    commit_big_editor();
                    if (lv->iItem >= 0 && lv->iItem < gFilteredCount)
                        load_editor_row(gFilteredRows[lv->iItem]);
                }
                return 0;
            }
            if (hdr->code == NM_DBLCLK) {
                NMLISTVIEW *lv = (NMLISTVIEW *)lp;
                begin_cell_edit(hwnd, lv->iItem, lv->iSubItem);
                return 0;
            }
            if (hdr->code == NM_CUSTOMDRAW) {
                NMLVCUSTOMDRAW *cd = (NMLVCUSTOMDRAW *)lp;
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    int item = (int)cd->nmcd.dwItemSpec;
                    if (item >= 0 && item < gFilteredCount && gSheet.rows[gFilteredRows[item]].dirty)
                        cd->clrTextBk = RGB(255, 248, 210);
                    return CDRF_DODEFAULT;
                }
            }
        }
        break;
    case WM_DESTROY:
        if (gUiFont) DeleteObject(gUiFont);
        if (gPreviewFont) DeleteObject(gPreviewFont);
        if (gFontMem) RemoveFontMemResourceEx(gFontMem);
        if (gExternalFontPath[0]) RemoveFontResourceExW(gExternalFontPath, FR_PRIVATE, 0);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msgid, wp, lp);
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPWSTR cmdLine, int show)
{
    INITCOMMONCONTROLSEX icc;
    WNDCLASSW wc;
    MSG m;

    (void)hPrev;
    (void)cmdLine;
    gInst = hInstance;

    CoInitialize(NULL);
    load_embedded_font();
    gUiFont = create_ui_font();
    detect_from_self();
    if (!gGameDir[0]) detect_from_steam();

    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icc);

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = wndproc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"BloodRayneCnToolWindow";
    RegisterClassW(&wc);

    gMain = CreateWindowExW(0, wc.lpszClassName, APP_TITLE,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 875, 755,
        NULL, NULL, hInstance, NULL);
    ShowWindow(gMain, show);
    UpdateWindow(gMain);

    if (!gGameDir[0])
        choose_game_dir(gMain);
    update_status();

    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    CoUninitialize();
    return (int)m.wParam;
}
