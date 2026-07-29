# Third-Party Notices

## Noto Sans SC

`src/tool/tool_ui_noto_sc_subset.ttf` is a subset of Noto Sans SC used only by the Win32 helper UI so Simplified Chinese text renders correctly on non-Chinese Windows systems.

Noto fonts are distributed under the SIL Open Font License. See:

- Google Fonts: https://fonts.google.com/noto/specimen/Noto+Sans+SC
- Noto documentation: https://notofonts.github.io/noto-docs/website/use/
- OFL: https://openfontlicense.org/

## Optional winmm.dll

`third_party/winmm.dll` is not included by default. If you choose to ship it, verify the upstream license and place the binary in `third_party/winmm.dll` before running `build.ps1`.

