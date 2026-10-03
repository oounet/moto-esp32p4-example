# Third-party notices / 第三方声明

The root MIT license covers OOU application/adaptation/example code unless a file states otherwise. It does not relicense the following dependencies or grant Moto SDK redistribution rights.

| Material | Origin and license |
| --- | --- |
| `components/jd9165` | Board vendor's Espressif JD9165 driver; retained Apache-2.0 notices, full text in `LICENSES/Apache-2.0.txt` |
| `main/fonts/hud_font.c` | Noto Sans CJK SC subset bitmap; SIL OFL 1.1 in `main/fonts/OFL.txt`; https://github.com/notofonts/noto-cjk |
| LVGL, including built-in fonts | Official LVGL 9.3.0, downloaded separately; retain upstream `LICENCE.txt` and font notices; https://github.com/lvgl/lvgl |
| ESP-IDF / Espressif components | Downloaded through ESP-IDF; component licenses remain in managed_components. The tinyH264 SIMD allocator fix produces a local derived archive without publishing the dependency archive. |
| Moto SDK | Proprietary binary dependency supplied separately; never included in public source or covered by root MIT |

原仪表参考图片、截图提取字形、Arial/DIN 系统字体派生数据未纳入本仓库。公开 UI 使用 LVGL 原生控件和上游可再分发字体，中文 HUD 保留 Noto OFL 字库。测试 JPEG 是解码自检色条，不是用户素材。
