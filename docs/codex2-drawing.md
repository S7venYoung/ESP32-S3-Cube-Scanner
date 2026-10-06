# Codex2: direct-drawn dashboard

Reference: `S7venYoung/zmk-prospector` scanner-mode west manifest pins `S7venYoung/prospector-zmk-module` at `819bcd40cfa13a76f09413ce7662f04bee902b13`. Layout follows `scanner_codex2_theme.c`: black/yellow outlined metrics cards, condensed heavy lettering, six weekly segments, layer/WPM bar, and separate L/R battery outlines. Horizontal spacing is adapted from 280 to 240 pixels; no fabricated keyboard values.

The dashboard **does not load LVGL fonts or use label font rendering**. Existing Codex2 Impact-shaped coverage masks are baked into `codex_draw_masks.h`, then drawn directly into RGB565 canvas buffers. Dynamic values reuse buffers and repaint only when text changes. Alpha coverage retains the original anti-aliased shape. Oversize numeric values are proportionally drawn smaller instead of selecting another font or clipping digits. Canvas stride follows LVGL alignment and buffers prefer PSRAM, with allocation checks and deletion cleanup.

Native Chinese setup/volume/alarm notices remain unchanged and can still use the original system fonts; only dashboard text uses direct drawing. No account, serial, Wi-Fi or Xiaozhi behavior is changed by this theme update. Keyboard telemetry remains `--`; offline or unknown quotas are not presented as 100%.
