#pragma once

#include <windows.h>

typedef struct {
    int glyphs;
    int missing;
    wchar_t sample[256];
} BrFontReport;

typedef struct {
    const wchar_t *file;
    int line;
    const wchar_t *english;
    const wchar_t *japanese;
    const wchar_t *chinese;
} BrTextEdit;

int brfont_check_game(const wchar_t *game_dir, const wchar_t *face, BrFontReport *report);
int brfont_apply_game(const wchar_t *game_dir, const wchar_t *face, wchar_t *err, size_t errcch);
int brfont_import_texts(const wchar_t *game_dir, int basis_jp, const BrTextEdit *edits,
    int edit_count, const wchar_t *out_pod, wchar_t *err, size_t errcch);
