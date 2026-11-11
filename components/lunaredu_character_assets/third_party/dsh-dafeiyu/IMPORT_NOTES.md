# BigFish asset import

The project owner confirmed they hold redistribution authorization for the
legacy BigFish runtime assets. The 49 files listed in `source-manifest.json`
were retrieved from the pinned upstream commit and checked against its Git
blob SHA-1 and byte length before inclusion. The upstream asset notice remains
in `ASSET_LICENSE.md` and is retained verbatim.

The four `legacy/dafeiyu/dragging/` files are excluded. The upstream notice
identifies them as a separate community contribution and says redistribution
requires permission from those contributors; the authorization scope has not
been independently established for that separate contribution.

Run `python components/lunaredu_character_assets/tools/prepare_bigfish.py
<output-directory>` with Pillow (from
`tools/resource-tools/requirements.txt`) to create centered transparent 160 ×
160 PNG frames, plus 160 × 160 little-endian RGB565 frames composited against
black for the direct RGB565 display surface, and a SHA-256 output manifest.
The converter checks every source frame against `source-manifest.json` before
processing. These staged files are not compiled into firmware; runtime
rendering consumes the `.rgb565` outputs from the SD card at
`/characters/dafeiyu/<clip>/<frame>.rgb565` (for example,
`/characters/dafeiyu/idle/00.rgb565`). Copy each generated clip directory under
the SD card's `characters/dafeiyu/` directory. The display requests a frame by
ID; the network/storage owner reads and validates exactly 51,200 bytes and
publishes an immutable snapshot. Missing or invalid files leave the current
frame intact and return an explicit error; the display then uses its existing
geometry renderer. Browser upload/installation and device-side animation
performance validation remain separate work.
