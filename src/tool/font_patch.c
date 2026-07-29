#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "font_patch.h"

#define POD_HEADER_SIZE 0x120
#define POD_ENTRY_SIZE 20
#define TEX_HEADER_SIZE 792
#define ATLAS_SIZE 1024
#define CELL_PAD 1
#define FONT_PIXEL_SIZE 19
#define CP_GBK 936

typedef struct {
    char *name;
    uint32_t size;
    uint32_t offset;
    uint32_t ts;
    uint32_t crc;
    BYTE *data;
} PodEntry;

typedef struct {
    BYTE *raw;
    size_t raw_size;
    BYTE header[POD_HEADER_SIZE];
    PodEntry *entries;
    int count;
    BYTE *audit;
    size_t audit_size;
} Pod;

typedef struct {
    uint32_t code;
    int x, y, w, h, bearing;
    double u0, v0, u1, v1;
} Glyph;

typedef struct {
    WCHAR ch;
    uint32_t code;
} NeededGlyph;

static uint32_t g_crc_table[256];
static int g_crc_ready;

static void crc_init(void)
{
    if (g_crc_ready) return;
    for (int i = 0; i < 256; i++) {
        uint32_t c = (uint32_t)i << 24;
        for (int j = 0; j < 8; j++)
            c = (c & 0x80000000U) ? ((c << 1) ^ 0x04C11DB7U) : (c << 1);
        g_crc_table[i] = c;
    }
    g_crc_ready = 1;
}

static uint32_t crc32_mpeg2(const BYTE *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    crc_init();
    for (size_t i = 0; i < len; i++)
        crc = (crc << 8) ^ g_crc_table[((crc >> 24) ^ data[i]) & 0xFF];
    return crc;
}

static uint32_t rd32(const BYTE *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr32(BYTE *p, uint32_t v)
{
    p[0] = (BYTE)v;
    p[1] = (BYTE)(v >> 8);
    p[2] = (BYTE)(v >> 16);
    p[3] = (BYTE)(v >> 24);
}

static int read_file(const wchar_t *path, BYTE **out, size_t *out_size)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    LARGE_INTEGER sz;
    DWORD got;
    BYTE *buf;
    if (h == INVALID_HANDLE_VALUE) return 0;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 0x7fffffff) {
        CloseHandle(h);
        return 0;
    }
    buf = (BYTE *)malloc((size_t)sz.QuadPart);
    if (!buf) {
        CloseHandle(h);
        return 0;
    }
    if (!ReadFile(h, buf, (DWORD)sz.QuadPart, &got, NULL) || got != (DWORD)sz.QuadPart) {
        free(buf);
        CloseHandle(h);
        return 0;
    }
    CloseHandle(h);
    *out = buf;
    *out_size = (size_t)sz.QuadPart;
    return 1;
}

static int write_file(const wchar_t *path, const BYTE *data, size_t size)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD put;
    int ok;
    if (h == INVALID_HANDLE_VALUE) return 0;
    ok = WriteFile(h, data, (DWORD)size, &put, NULL) && put == (DWORD)size;
    CloseHandle(h);
    return ok;
}

static void pod_free(Pod *pod)
{
    if (!pod) return;
    if (pod->entries) {
        for (int i = 0; i < pod->count; i++) {
            free(pod->entries[i].name);
            free(pod->entries[i].data);
        }
    }
    free(pod->entries);
    free(pod->audit);
    free(pod->raw);
    memset(pod, 0, sizeof(*pod));
}

static int pod_load(const wchar_t *path, Pod *pod)
{
    BYTE *raw = NULL;
    size_t raw_size = 0;
    uint32_t n, idx, names, name_end;
    memset(pod, 0, sizeof(*pod));
    if (!read_file(path, &raw, &raw_size)) return 0;
    if (raw_size < POD_HEADER_SIZE || memcmp(raw, "POD3", 4) != 0) {
        free(raw);
        return 0;
    }
    n = rd32(raw + 0x58);
    idx = rd32(raw + 0x108);
    if (n > 10000 || idx >= raw_size || idx + n * POD_ENTRY_SIZE > raw_size) {
        free(raw);
        return 0;
    }
    names = idx + n * POD_ENTRY_SIZE;
    name_end = names;
    pod->entries = (PodEntry *)calloc(n, sizeof(PodEntry));
    if (!pod->entries) {
        free(raw);
        return 0;
    }
    memcpy(pod->header, raw, POD_HEADER_SIZE);
    pod->count = (int)n;
    for (uint32_t i = 0; i < n; i++) {
        BYTE *ent = raw + idx + i * POD_ENTRY_SIZE;
        uint32_t no = rd32(ent);
        uint32_t size = rd32(ent + 4);
        uint32_t off = rd32(ent + 8);
        uint32_t ts = rd32(ent + 12);
        uint32_t crc = rd32(ent + 16);
        uint32_t pos = names + no;
        uint32_t end = pos;
        if (pos >= raw_size || off > raw_size || size > raw_size || off + size > raw_size) {
            pod_free(pod);
            free(raw);
            return 0;
        }
        while (end < raw_size && raw[end]) end++;
        if (end >= raw_size) {
            pod_free(pod);
            free(raw);
            return 0;
        }
        if (end + 1 > name_end) name_end = end + 1;
        pod->entries[i].name = _strdup((const char *)(raw + pos));
        pod->entries[i].size = size;
        pod->entries[i].offset = off;
        pod->entries[i].ts = ts;
        pod->entries[i].crc = crc;
        pod->entries[i].data = (BYTE *)malloc(size ? size : 1);
        if (!pod->entries[i].name || !pod->entries[i].data) {
            pod_free(pod);
            free(raw);
            return 0;
        }
        memcpy(pod->entries[i].data, raw + off, size);
    }
    if (name_end < raw_size) {
        pod->audit_size = raw_size - name_end;
        pod->audit = (BYTE *)malloc(pod->audit_size);
        if (!pod->audit) {
            pod_free(pod);
            free(raw);
            return 0;
        }
        memcpy(pod->audit, raw + name_end, pod->audit_size);
    }
    pod->raw = raw;
    pod->raw_size = raw_size;
    return 1;
}

static int pod_write(const wchar_t *path, Pod *pod)
{
    size_t names_size = 0, data_size = 0, total, p;
    BYTE *out, *idxp, *namep;
    for (int i = 0; i < pod->count; i++) {
        names_size += strlen(pod->entries[i].name) + 1;
        data_size += pod->entries[i].size;
    }
    total = POD_HEADER_SIZE + data_size + (size_t)pod->count * POD_ENTRY_SIZE + names_size + pod->audit_size;
    out = (BYTE *)calloc(1, total);
    if (!out) return 0;
    memcpy(out, pod->header, POD_HEADER_SIZE);
    p = POD_HEADER_SIZE;
    idxp = out + POD_HEADER_SIZE + data_size;
    namep = idxp + (size_t)pod->count * POD_ENTRY_SIZE;
    size_t noff = 0;
    for (int i = 0; i < pod->count; i++) {
        PodEntry *e = &pod->entries[i];
        memcpy(out + p, e->data, e->size);
        e->offset = (uint32_t)p;
        e->crc = crc32_mpeg2(e->data, e->size);
        wr32(idxp + i * POD_ENTRY_SIZE, (uint32_t)noff);
        wr32(idxp + i * POD_ENTRY_SIZE + 4, e->size);
        wr32(idxp + i * POD_ENTRY_SIZE + 8, e->offset);
        wr32(idxp + i * POD_ENTRY_SIZE + 12, e->ts);
        wr32(idxp + i * POD_ENTRY_SIZE + 16, e->crc);
        strcpy((char *)namep + noff, e->name);
        noff += strlen(e->name) + 1;
        p += e->size;
    }
    if (pod->audit_size) memcpy(namep + names_size, pod->audit, pod->audit_size);
    wr32(out + 0x58, (uint32_t)pod->count);
    wr32(out + 0x108, POD_HEADER_SIZE + (uint32_t)data_size);
    wr32(out + 0x110, (uint32_t)names_size);
    wr32(out + 4, crc32_mpeg2(out + 8, POD_HEADER_SIZE - 8));
    int ok = write_file(path, out, total);
    free(out);
    return ok;
}

static PodEntry *pod_find(Pod *pod, const char *name)
{
    for (int i = 0; i < pod->count; i++) {
        if (_stricmp(pod->entries[i].name, name) == 0)
            return &pod->entries[i];
    }
    return NULL;
}

static void replace_entry(PodEntry *e, BYTE *data, uint32_t size);

static PodEntry *pod_find_basename(Pod *pod, const wchar_t *file)
{
    char want[MAX_PATH];
    int n = WideCharToMultiByte(CP_ACP, 0, file, -1, want, MAX_PATH, NULL, NULL);
    if (n <= 0) return NULL;
    for (int i = 0; i < pod->count; i++) {
        const char *name = pod->entries[i].name;
        const char *base = strrchr(name, '\\');
        base = base ? base + 1 : name;
        if (_stricmp(base, want) == 0) return &pod->entries[i];
    }
    return NULL;
}

static wchar_t *wide_from_gbk(const BYTE *data, uint32_t size)
{
    int n = MultiByteToWideChar(CP_GBK, 0, (LPCCH)data, size, NULL, 0);
    wchar_t *w;
    if (n <= 0) return NULL;
    w = (wchar_t *)calloc((size_t)n + 1, sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_GBK, 0, (LPCCH)data, size, w, n);
    return w;
}

static void apply_non_gbk_fallbacks(wchar_t *s)
{
    for (; s && *s; s++) {
        switch (*s) {
        case L'™': *s = L'?'; break;
        case L'©': *s = L'?'; break;
        case L'®': *s = L'?'; break;
        case L'€': *s = L'?'; break;
        case L'‐':
        case L'‑':
        case L'−': *s = L'-'; break;
        case L'•': *s = L'·'; break;
        }
    }
}

static BYTE *gbk_from_wide_dup(const wchar_t *s, uint32_t *out_size)
{
    BOOL used_default = FALSE;
    int n;
    BYTE *b;
    wchar_t *tmp = _wcsdup(s ? s : L"");
    if (!tmp) return NULL;
    apply_non_gbk_fallbacks(tmp);
    n = WideCharToMultiByte(CP_GBK, 0, tmp, -1, NULL, 0, NULL, &used_default);
    if (n <= 0) {
        free(tmp);
        return NULL;
    }
    b = (BYTE *)malloc((size_t)n);
    if (!b) {
        free(tmp);
        return NULL;
    }
    WideCharToMultiByte(CP_GBK, 0, tmp, -1, (LPSTR)b, n, NULL, &used_default);
    *out_size = (uint32_t)(n - 1);
    free(tmp);
    return b;
}

static wchar_t **split_crlf_lines(const wchar_t *text, int *out_count)
{
    int count = 1;
    const wchar_t *p;
    wchar_t **lines;
    int idx = 0;
    for (p = text; p && *p; p++) {
        if (*p == L'\r' && p[1] == L'\n') {
            count++;
            p++;
        }
    }
    lines = (wchar_t **)calloc((size_t)count, sizeof(wchar_t *));
    if (!lines) return NULL;
    p = text;
    while (idx < count) {
        const wchar_t *start = p;
        size_t len = 0;
        while (p && *p && !(*p == L'\r' && p[1] == L'\n')) {
            p++;
            len++;
        }
        lines[idx] = (wchar_t *)calloc(len + 1, sizeof(wchar_t));
        if (!lines[idx]) {
            for (int i = 0; i < idx; i++) free(lines[i]);
            free(lines);
            return NULL;
        }
        wcsncpy_s(lines[idx], len + 1, start, len);
        idx++;
        if (p && *p == L'\r' && p[1] == L'\n') p += 2;
        else break;
    }
    *out_count = idx;
    return lines;
}

static void free_lines(wchar_t **lines, int count)
{
    if (!lines) return;
    for (int i = 0; i < count; i++) free(lines[i]);
    free(lines);
}

static wchar_t *join_crlf_lines(wchar_t **lines, int count)
{
    size_t total = 1;
    wchar_t *out;
    size_t pos = 0;
    for (int i = 0; i < count; i++) total += wcslen(lines[i]) + 2;
    out = (wchar_t *)calloc(total, sizeof(wchar_t));
    if (!out) return NULL;
    for (int i = 0; i < count; i++) {
        size_t n = wcslen(lines[i]);
        wcscpy_s(out + pos, total - pos, lines[i]);
        pos += n;
        if (i + 1 < count) {
            out[pos++] = L'\r';
            out[pos++] = L'\n';
        }
    }
    out[pos] = 0;
    return out;
}

static const wchar_t *edit_text_for_basis(const BrTextEdit *e, int basis_jp)
{
    if (e->chinese && e->chinese[0]) return e->chinese;
    if (basis_jp && e->japanese) return e->japanese;
    return e->english ? e->english : L"";
}

static wchar_t *replace_text_line(const wchar_t *old_line, const BrTextEdit *edit, int basis_jp)
{
    const wchar_t *zh = edit_text_for_basis(edit, basis_jp);
    if (_wcsicmp(edit->file, L"MSGLIST.TXT") == 0) {
        size_t need = wcslen(edit->english ? edit->english : L"") + wcslen(zh) + 8;
        wchar_t *out = (wchar_t *)calloc(need, sizeof(wchar_t));
        if (!out) return NULL;
        _snwprintf(out, need, L"\"%s\", \"%s\"", edit->english ? edit->english : L"", zh);
        return out;
    } else {
        const wchar_t *c1 = wcschr(old_line, L',');
        const wchar_t *c2 = c1 ? wcschr(c1 + 1, L',') : NULL;
        size_t prefix = c2 ? (size_t)(c2 - old_line + 1) : 0;
        size_t need = prefix + 1 + wcslen(zh) + 1;
        wchar_t *out = (wchar_t *)calloc(need, sizeof(wchar_t));
        if (!out) return NULL;
        if (prefix) {
            wcsncpy_s(out, need, old_line, prefix);
            wcscat_s(out, need, L" ");
            wcscat_s(out, need, zh);
        } else {
            wcscpy_s(out, need, zh);
        }
        return out;
    }
}

static int msglist_left_matches(const wchar_t *line, const wchar_t *english)
{
    const wchar_t *p;
    size_t len;
    if (!line || !english || line[0] != L'"') return 0;
    p = wcschr(line + 1, L'"');
    if (!p) return 0;
    len = (size_t)(p - (line + 1));
    return wcslen(english) == len && wcsncmp(line + 1, english, len) == 0;
}

static int update_text_entry(Pod *pod, const BrTextEdit *edits, int edit_count, const wchar_t *file, int basis_jp, wchar_t *err, size_t errcch)
{
    PodEntry *e = pod_find_basename(pod, file);
    wchar_t *text, *joined;
    wchar_t **lines;
    int line_count = 0;
    BYTE *gbk;
    uint32_t gbk_size;
    if (!e) return 1;
    text = wide_from_gbk(e->data, e->size);
    if (!text) {
        _snwprintf(err, errcch, L"无法解码文本：%s", file);
        return 0;
    }
    lines = split_crlf_lines(text, &line_count);
    free(text);
    if (!lines) return 0;
    for (int i = 0; i < edit_count; i++) {
        wchar_t *nl;
        if (_wcsicmp(edits[i].file, file) != 0) continue;
        if (edits[i].line < 0) {
            if (_wcsicmp(file, L"MSGLIST.TXT") == 0) {
                for (int j = 0; j < line_count; j++) {
                    if (msglist_left_matches(lines[j], edits[i].english)) {
                        nl = replace_text_line(lines[j], &edits[i], basis_jp);
                        if (!nl) {
                            free_lines(lines, line_count);
                            return 0;
                        }
                        free(lines[j]);
                        lines[j] = nl;
                        break;
                    }
                }
            }
            continue;
        }
        if (edits[i].line >= line_count) continue;
        nl = replace_text_line(lines[edits[i].line], &edits[i], basis_jp);
        if (!nl) {
            free_lines(lines, line_count);
            return 0;
        }
        free(lines[edits[i].line]);
        lines[edits[i].line] = nl;
    }
    if (_wcsicmp(file, L"MSGLIST.TXT") == 0 && line_count > 5) {
        free(lines[3]);
        lines[3] = _wcsdup(L"1");
        free(lines[5]);
        lines[5] = _wcsdup(L"\"ＭＳ Ｐゴシック\"");
    }
    joined = join_crlf_lines(lines, line_count);
    free_lines(lines, line_count);
    if (!joined) return 0;
    gbk = gbk_from_wide_dup(joined, &gbk_size);
    free(joined);
    if (!gbk) return 0;
    replace_entry(e, gbk, gbk_size);
    return 1;
}

static int code_loaded_from_fnt(Pod *pod, uint32_t code)
{
    PodEntry *fnte = pod_find(pod, "DATA\\DBCSFONT.FNT");
    char *txt, *ctx = NULL, *line;
    int body = 0, first = 0, second = 0, rows_seen = 0;
    if (!fnte) return 0;
    txt = (char *)malloc(fnte->size + 1);
    if (!txt) return 0;
    memcpy(txt, fnte->data, fnte->size);
    txt[fnte->size] = 0;
    line = strtok_s(txt, "\n", &ctx);
    while (line) {
        while (*line == '\r' || *line == ' ' || *line == '\t') line++;
        if (*line && strncmp(line, "//", 2) != 0) {
            body++;
            if (body == 3) sscanf_s(line, "%d,%d", &first, &second);
            else if (body >= 4) {
                unsigned c = 0;
                if (sscanf_s(line, "%u,", &c) == 1 && c == code) {
                    free(txt);
                    return 1;
                }
                rows_seen++;
                if (first && second && rows_seen > second - first + 1) break;
            }
        }
        line = strtok_s(NULL, "\n", &ctx);
    }
    free(txt);
    return 0;
}

static int pod_text_glyphs_loadable(Pod *pod, wchar_t *missing, size_t missing_cch)
{
    missing[0] = 0;
    for (int i = 0; i < pod->count; i++) {
        PodEntry *e = &pod->entries[i];
        wchar_t *w;
        if (!is_txt_name(e->name)) continue;
        w = wide_from_gbk(e->data, e->size);
        if (!w) continue;
        for (wchar_t *p = w; *p; p++) {
            char mb[4];
            int n;
            uint32_t code;
            if (*p <= 0x7f) continue;
            n = WideCharToMultiByte(CP_GBK, 0, p, 1, mb, sizeof(mb), NULL, NULL);
            if (n != 2) continue;
            code = ((BYTE)mb[0] << 8) | (BYTE)mb[1];
            if (!code_loaded_from_fnt(pod, code) && !wcschr(missing, *p)) {
                size_t len = wcslen(missing);
                if (len + 2 < missing_cch) {
                    missing[len] = *p;
                    missing[len + 1] = 0;
                }
            }
        }
        free(w);
    }
    return missing[0] == 0;
}

static int is_txt_name(const char *name)
{
    size_t n = strlen(name);
    return n > 4 && _stricmp(name + n - 4, ".TXT") == 0;
}

static int glyph_cmp(const void *a, const void *b)
{
    const NeededGlyph *ga = (const NeededGlyph *)a;
    const NeededGlyph *gb = (const NeededGlyph *)b;
    return (ga->code > gb->code) - (ga->code < gb->code);
}

static int fnt_glyph_cmp(const void *a, const void *b)
{
    const Glyph *ga = (const Glyph *)a;
    const Glyph *gb = (const Glyph *)b;
    return (ga->code > gb->code) - (ga->code < gb->code);
}

static int collect_needed(Pod *pods, int npods, NeededGlyph **out, int *out_count)
{
    BYTE seen_hi[65536 / 8];
    NeededGlyph *items = NULL;
    int count = 0, cap = 0;
    memset(seen_hi, 0, sizeof(seen_hi));
    for (int p = 0; p < npods; p++) {
        Pod *pod = &pods[p];
        for (int i = 0; i < pod->count; i++) {
            PodEntry *e = &pod->entries[i];
            if (!is_txt_name(e->name)) continue;
            int wlen = MultiByteToWideChar(CP_GBK, MB_ERR_INVALID_CHARS, (LPCCH)e->data, e->size, NULL, 0);
            if (wlen <= 0) wlen = MultiByteToWideChar(CP_GBK, 0, (LPCCH)e->data, e->size, NULL, 0);
            if (wlen <= 0) continue;
            WCHAR *w = (WCHAR *)malloc((size_t)wlen * sizeof(WCHAR));
            if (!w) return 0;
            MultiByteToWideChar(CP_GBK, 0, (LPCCH)e->data, e->size, w, wlen);
            for (int j = 0; j < wlen; j++) {
                char mb[4];
                int mblen;
                uint32_t code;
                WCHAR ch = w[j];
                if (ch < 0x80) continue;
                if (ch >= 65536) continue;
                mblen = WideCharToMultiByte(CP_GBK, 0, &ch, 1, mb, sizeof(mb), NULL, NULL);
                if (mblen != 2) continue;
                if (seen_hi[ch >> 3] & (1 << (ch & 7))) continue;
                seen_hi[ch >> 3] |= (1 << (ch & 7));
                if (count == cap) {
                    cap = cap ? cap * 2 : 512;
                    NeededGlyph *tmp = (NeededGlyph *)realloc(items, (size_t)cap * sizeof(NeededGlyph));
                    if (!tmp) {
                        free(w);
                        free(items);
                        return 0;
                    }
                    items = tmp;
                }
                code = ((BYTE)mb[0] << 8) | (BYTE)mb[1];
                items[count].ch = ch;
                items[count].code = code;
                count++;
            }
            free(w);
        }
    }
    qsort(items, count, sizeof(NeededGlyph), glyph_cmp);
    *out = items;
    *out_count = count;
    return 1;
}

static int parse_ascii_glyphs(const BYTE *fnt, uint32_t size, Glyph **out, int *out_count, char *meta, size_t metacap)
{
    char *txt = (char *)malloc(size + 1);
    char *ctx = NULL, *line;
    int body = 0, cap = 0, count = 0;
    Glyph *items = NULL;
    if (!txt) return 0;
    memcpy(txt, fnt, size);
    txt[size] = 0;
    line = strtok_s(txt, "\n", &ctx);
    while (line) {
        while (*line == '\r' || *line == ' ' || *line == '\t') line++;
        char *r = strchr(line, '\r');
        if (r) *r = 0;
        if (*line && strncmp(line, "//", 2) != 0) {
            body++;
            if (body == 2) {
                strncpy(meta, line, metacap - 1);
                meta[metacap - 1] = 0;
            } else if (body >= 4) {
                Glyph g;
                if (sscanf_s(line, "%u,%d,%d,%d,%d,%d,%lf,%lf,%lf,%lf",
                    &g.code, &g.x, &g.y, &g.w, &g.h, &g.bearing, &g.u0, &g.v0, &g.u1, &g.v1) == 10 &&
                    g.code < 0x100) {
                    if (count == cap) {
                        cap = cap ? cap * 2 : 128;
                        Glyph *tmp = (Glyph *)realloc(items, (size_t)cap * sizeof(Glyph));
                        if (!tmp) {
                            free(items);
                            free(txt);
                            return 0;
                        }
                        items = tmp;
                    }
                    items[count++] = g;
                }
            }
        }
        line = strtok_s(NULL, "\n", &ctx);
    }
    free(txt);
    if (!meta[0] || count == 0) {
        free(items);
        return 0;
    }
    *out = items;
    *out_count = count;
    return 1;
}

static int font_has_all(const wchar_t *face, NeededGlyph *glyphs, int count, BrFontReport *report)
{
    HDC dc = GetDC(NULL);
    HFONT font = CreateFontW(-FONT_PIXEL_SIZE, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
        GB2312_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, face);
    HFONT old;
    WORD idx;
    report->glyphs = count;
    report->missing = 0;
    report->sample[0] = 0;
    if (!dc || !font) {
        if (dc) ReleaseDC(NULL, dc);
        return 0;
    }
    old = (HFONT)SelectObject(dc, font);
    for (int i = 0; i < count; i++) {
        idx = 0;
        GetGlyphIndicesW(dc, &glyphs[i].ch, 1, &idx, GGI_MARK_NONEXISTING_GLYPHS);
        if (idx == 0xFFFF) {
            size_t n = wcslen(report->sample);
            report->missing++;
            if (n + 2 < sizeof(report->sample) / sizeof(report->sample[0])) {
                report->sample[n] = glyphs[i].ch;
                report->sample[n + 1] = 0;
            }
        }
    }
    SelectObject(dc, old);
    DeleteObject(font);
    ReleaseDC(NULL, dc);
    return 1;
}

static BYTE *build_tex_and_fnt(Pod *pod, const wchar_t *face, NeededGlyph *needed, int needed_count,
    BYTE **out_fnt, uint32_t *out_tex_size, uint32_t *out_fnt_size)
{
    PodEntry *texe = pod_find(pod, "ART\\DBCSFONT.TEX");
    PodEntry *fnte = pod_find(pod, "DATA\\DBCSFONT.FNT");
    Glyph *ascii = NULL, *all = NULL;
    int ascii_count = 0, all_count = 0, all_cap = 0;
    char meta[128] = "";
    BYTE *alpha = NULL, *tex = NULL, *fnt = NULL;
    int ascii_bottom = 0, pen_x = 0, pen_y, row_h = 0;
    HDC dc = NULL;
    HBITMAP bmp = NULL;
    void *bits = NULL;
    HFONT font = NULL, oldfont = NULL;
    BITMAPINFO bi;
    if (!texe || !fnte || texe->size < TEX_HEADER_SIZE) return NULL;
    if (!parse_ascii_glyphs(fnte->data, fnte->size, &ascii, &ascii_count, meta, sizeof(meta))) return NULL;
    alpha = (BYTE *)calloc(ATLAS_SIZE * ATLAS_SIZE, 1);
    if (!alpha) goto fail;
    if (texe->size >= TEX_HEADER_SIZE + ATLAS_SIZE * ATLAS_SIZE * 2) {
        BYTE *orig_alpha = texe->data + TEX_HEADER_SIZE + ATLAS_SIZE * ATLAS_SIZE;
        for (int i = 0; i < ascii_count; i++) {
            Glyph *g = &ascii[i];
            if (g->x + g->w <= ATLAS_SIZE && g->y + g->h <= ATLAS_SIZE) {
                for (int yy = 0; yy < g->h; yy++)
                    memcpy(alpha + (g->y + yy) * ATLAS_SIZE + g->x,
                           orig_alpha + (g->y + yy) * ATLAS_SIZE + g->x, g->w);
            }
            if (g->y + g->h > ascii_bottom) ascii_bottom = g->y + g->h;
        }
    }
    all_cap = ascii_count + needed_count;
    all = (Glyph *)calloc(all_cap, sizeof(Glyph));
    if (!all) goto fail;
    memcpy(all, ascii, (size_t)ascii_count * sizeof(Glyph));
    all_count = ascii_count;
    dc = CreateCompatibleDC(NULL);
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 96;
    bi.bmiHeader.biHeight = -96;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!dc || !bmp || !bits) goto fail;
    SelectObject(dc, bmp);
    font = CreateFontW(-FONT_PIXEL_SIZE, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
        GB2312_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, face);
    if (!font) goto fail;
    oldfont = (HFONT)SelectObject(dc, font);
    SetBkColor(dc, RGB(0, 0, 0));
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    pen_y = ascii_bottom + CELL_PAD;
    for (int i = 0; i < needed_count; i++) {
        RECT rc = { 0, 0, 96, 96 };
        BYTE *px = (BYTE *)bits;
        int minx = 96, miny = 96, maxx = -1, maxy = -1;
        memset(bits, 0, 96 * 96 * 4);
        ExtTextOutW(dc, FONT_PIXEL_SIZE / 4, 0, ETO_CLIPPED, &rc, &needed[i].ch, 1, NULL);
        for (int y = 0; y < 96; y++) {
            for (int x = 0; x < 96; x++) {
                BYTE b = px[(y * 96 + x) * 4 + 0];
                BYTE g = px[(y * 96 + x) * 4 + 1];
                BYTE r = px[(y * 96 + x) * 4 + 2];
                BYTE v = (BYTE)(((int)r + g + b) / 3);
                if (v) {
                    if (x < minx) minx = x;
                    if (x > maxx) maxx = x;
                    if (y < miny) miny = y;
                    if (y > maxy) maxy = y;
                }
            }
        }
        Glyph gph;
        ZeroMemory(&gph, sizeof(gph));
        gph.code = needed[i].code;
        if (maxx < minx || maxy < miny) {
            all[all_count++] = gph;
            continue;
        }
        gph.w = maxx - minx + 1;
        gph.h = maxy - miny + 1;
        gph.bearing = miny;
        if (pen_x + gph.w + CELL_PAD > ATLAS_SIZE) {
            pen_x = 0;
            pen_y += row_h + CELL_PAD;
            row_h = 0;
        }
        if (pen_y + gph.h >= ATLAS_SIZE) goto fail;
        gph.x = pen_x;
        gph.y = pen_y;
        for (int yy = 0; yy < gph.h; yy++) {
            for (int xx = 0; xx < gph.w; xx++) {
                int sx = minx + xx, sy = miny + yy;
                BYTE b = px[(sy * 96 + sx) * 4 + 0];
                BYTE gg = px[(sy * 96 + sx) * 4 + 1];
                BYTE r = px[(sy * 96 + sx) * 4 + 2];
                alpha[(gph.y + yy) * ATLAS_SIZE + gph.x + xx] = (BYTE)(((int)r + gg + b) / 3);
            }
        }
        pen_x += gph.w + CELL_PAD;
        if (gph.h > row_h) row_h = gph.h;
        all[all_count++] = gph;
    }
    qsort(all, all_count, sizeof(Glyph), fnt_glyph_cmp);
    size_t fcap = (size_t)all_count * 96 + 256;
    fnt = (BYTE *)malloc(fcap);
    if (!fnt) goto fail;
    int pos = snprintf((char *)fnt, fcap,
        "// .FNT version\n1001\n// charSpacing, lineHeight, lineSpacing, shadowXOffset, shadowYOffset\n%s\n// firstChar, charCount\n%u,%u\n",
        meta, all[0].code, all[0].code + all_count - 1);
    for (int i = 0; i < all_count; i++) {
        Glyph *g = &all[i];
        pos += snprintf((char *)fnt + pos, fcap - pos, "%u,%d,%d,%d,%d,%d,%.6f,%.6f,%.6f,%.6f\n",
            g->code, g->x, g->y, g->w, g->h, g->bearing,
            (double)g->x / ATLAS_SIZE, (double)g->y / ATLAS_SIZE,
            (double)(g->x + g->w) / ATLAS_SIZE, (double)(g->y + g->h) / ATLAS_SIZE);
    }
    tex = (BYTE *)malloc(TEX_HEADER_SIZE + ATLAS_SIZE * ATLAS_SIZE * 2);
    if (!tex) goto fail;
    memcpy(tex, texe->data, TEX_HEADER_SIZE);
    wr32(tex + 0, 2);
    wr32(tex + 4, 2);
    wr32(tex + 8, ATLAS_SIZE);
    wr32(tex + 12, ATLAS_SIZE);
    memset(tex + TEX_HEADER_SIZE, 1, ATLAS_SIZE * ATLAS_SIZE);
    memcpy(tex + TEX_HEADER_SIZE + ATLAS_SIZE * ATLAS_SIZE, alpha, ATLAS_SIZE * ATLAS_SIZE);
    *out_tex_size = TEX_HEADER_SIZE + ATLAS_SIZE * ATLAS_SIZE * 2;
    *out_fnt_size = (uint32_t)pos;
    *out_fnt = fnt;
    if (oldfont) SelectObject(dc, oldfont);
    DeleteObject(font);
    DeleteObject(bmp);
    DeleteDC(dc);
    free(alpha);
    free(ascii);
    free(all);
    return tex;
fail:
    if (oldfont) SelectObject(dc, oldfont);
    if (font) DeleteObject(font);
    if (bmp) DeleteObject(bmp);
    if (dc) DeleteDC(dc);
    free(alpha);
    free(ascii);
    free(all);
    free(fnt);
    free(tex);
    return NULL;
}

static void replace_entry(PodEntry *e, BYTE *data, uint32_t size)
{
    free(e->data);
    e->data = data;
    e->size = size;
}

static void pod_path(const wchar_t *game_dir, const wchar_t *rel, wchar_t *out)
{
    _snwprintf(out, MAX_PATH, L"%s\\%s", game_dir, rel);
    out[MAX_PATH - 1] = 0;
}

static int load_target_pods(const wchar_t *game_dir, Pod pods[3], wchar_t paths[3][MAX_PATH], int *count, wchar_t *err, size_t errcch)
{
    const wchar_t *rels[] = {
        L"JAPANESE.POD",
        L"zh_cn_tools\\variants\\text_en\\JAPANESE.POD",
        L"zh_cn_tools\\variants\\text_jp\\JAPANESE.POD",
    };
    *count = 0;
    for (int i = 0; i < 3; i++) {
        DWORD attr;
        pod_path(game_dir, rels[i], paths[*count]);
        attr = GetFileAttributesW(paths[*count]);
        if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (!pod_load(paths[*count], &pods[*count])) {
            _snwprintf(err, errcch, L"无法读取 POD：%s", paths[*count]);
            return 0;
        }
        (*count)++;
    }
    if (*count == 0) {
        _snwprintf(err, errcch, L"没有找到可修改的 JAPANESE.POD。");
        return 0;
    }
    return 1;
}

int brfont_check_game(const wchar_t *game_dir, const wchar_t *face, BrFontReport *report)
{
    Pod pods[3];
    wchar_t paths[3][MAX_PATH], err[512];
    int npods = 0, ok = 0;
    NeededGlyph *needed = NULL;
    int needed_count = 0;
    memset(pods, 0, sizeof(pods));
    memset(report, 0, sizeof(*report));
    if (!load_target_pods(game_dir, pods, paths, &npods, err, 512)) return 0;
    if (collect_needed(pods, npods, &needed, &needed_count) &&
        font_has_all(face, needed, needed_count, report))
        ok = 1;
    free(needed);
    for (int i = 0; i < npods; i++) pod_free(&pods[i]);
    return ok;
}

int brfont_apply_game(const wchar_t *game_dir, const wchar_t *face, wchar_t *err, size_t errcch)
{
    Pod pods[3];
    wchar_t paths[3][MAX_PATH];
    int npods = 0, ok = 0;
    NeededGlyph *needed = NULL;
    int needed_count = 0;
    BrFontReport report;
    memset(pods, 0, sizeof(pods));
    err[0] = 0;
    if (!load_target_pods(game_dir, pods, paths, &npods, err, errcch)) goto done;
    if (!collect_needed(pods, npods, &needed, &needed_count)) {
        _snwprintf(err, errcch, L"收集游戏用字失败。");
        goto done;
    }
    if (!font_has_all(face, needed, needed_count, &report)) {
        _snwprintf(err, errcch, L"无法检查字体。");
        goto done;
    }
    if (report.missing > 0) {
        _snwprintf(err, errcch, L"字体缺少 %d 个游戏用字，不能应用。\r\n示例：%s", report.missing, report.sample);
        goto done;
    }
    for (int i = 0; i < npods; i++) {
        BYTE *fnt = NULL, *tex = NULL;
        uint32_t fnt_size = 0, tex_size = 0;
        PodEntry *texe = pod_find(&pods[i], "ART\\DBCSFONT.TEX");
        PodEntry *fnte = pod_find(&pods[i], "DATA\\DBCSFONT.FNT");
        if (!texe || !fnte) {
            _snwprintf(err, errcch, L"POD 缺少 DBCSFONT：%s", paths[i]);
            goto done;
        }
        tex = build_tex_and_fnt(&pods[i], face, needed, needed_count, &fnt, &tex_size, &fnt_size);
        if (!tex || !fnt) {
            free(tex);
            free(fnt);
            _snwprintf(err, errcch, L"生成字体图集失败。");
            goto done;
        }
        replace_entry(texe, tex, tex_size);
        replace_entry(fnte, fnt, fnt_size);
        if (!pod_write(paths[i], &pods[i])) {
            _snwprintf(err, errcch, L"写入失败：%s\r\n请先关闭游戏，必要时以管理员身份运行。", paths[i]);
            goto done;
        }
    }
    ok = 1;
done:
    free(needed);
    for (int i = 0; i < npods; i++) pod_free(&pods[i]);
    return ok;
}

int brfont_import_texts(const wchar_t *game_dir, int basis_jp, const BrTextEdit *edits,
    int edit_count, const wchar_t *out_pod, wchar_t *err, size_t errcch)
{
    wchar_t base[MAX_PATH], missing[512];
    Pod pod;
    int ok = 0;
    err[0] = 0;
    memset(&pod, 0, sizeof(pod));
    _snwprintf(base, MAX_PATH, L"%s\\zh_cn_tools\\variants\\%s\\JAPANESE.POD",
        game_dir, basis_jp ? L"text_jp" : L"text_en");
    base[MAX_PATH - 1] = 0;
    if (!pod_load(base, &pod)) {
        _snwprintf(base, MAX_PATH, L"%s\\zh_cn_tools\\defaults\\%s\\JAPANESE.POD",
            game_dir, basis_jp ? L"text_jp" : L"text_en");
        base[MAX_PATH - 1] = 0;
        if (!pod_load(base, &pod)) {
            _snwprintf(err, errcch, L"无法读取文本模板 POD：%s", base);
            return 0;
        }
    }

    for (int i = 0; i < edit_count; i++) {
        int first = 1;
        if (!edits[i].file || !edits[i].file[0] || edits[i].line < 0) continue;
        for (int j = 0; j < i; j++) {
            if (edits[j].file && _wcsicmp(edits[j].file, edits[i].file) == 0) {
                first = 0;
                break;
            }
        }
        if (!first) continue;
        if (!update_text_entry(&pod, edits, edit_count, edits[i].file, basis_jp, err, errcch))
            goto done;
    }

    if (!pod_text_glyphs_loadable(&pod, missing, 512)) {
        _snwprintf(err, errcch,
            L"译文包含当前游戏字库没有的字，已停止导入。\r\n"
            L"缺字示例：%s\r\n"
            L"请先在“游戏字体”页选择字体并应用，或换用已有字。",
            missing);
        goto done;
    }

    if (!pod_write(out_pod, &pod)) {
        _snwprintf(err, errcch, L"写出临时 POD 失败：%s", out_pod);
        goto done;
    }
    pod_free(&pod);
    memset(&pod, 0, sizeof(pod));
    if (!pod_load(out_pod, &pod)) {
        _snwprintf(err, errcch, L"写出后无法重新读取 POD，已停止覆盖。");
        return 0;
    }
    if (!pod_text_glyphs_loadable(&pod, missing, 512)) {
        _snwprintf(err, errcch, L"写出后字库校验失败，已停止覆盖。");
        goto done;
    }
    ok = 1;

done:
    pod_free(&pod);
    return ok;
}
