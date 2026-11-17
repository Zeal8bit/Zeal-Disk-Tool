# Lucide icons

The graphical frontend uses a subset of the [Lucide](https://lucide.dev) icon
set for toolbars, navigation, file types and status indicators.

- Upstream: <https://lucide.dev>, sources fetched from the Iconify API
  (`https://api.iconify.design/lucide/<name>.svg`).
- License: ISC, see [LICENSE](LICENSE).
- The SVGs keep `stroke="currentColor"`; `src/gui/ModernStyle.cpp` replaces it
  with the colour requested at draw time and lets `Fl_SVG_Image` rasterise the
  icon at the exact pixel size, so the icons stay crisp at any UI scale.
- `scripts/generate-icons.py` embeds these files into `include/ui_icons.h`.
  Run it after adding or updating an icon, then rebuild.

Bundled icons: chevron-down, chevron-left, chevron-right, chevron-up,
download, ellipsis, eraser, file, file-archive, file-cog, file-digit,
file-image, file-text, folder, folder-open, folder-plus, hard-drive, house,
plus, search, settings, trash-2, upload.
