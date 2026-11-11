"""Create the bounded numeric Teko subset used by the home face (offline only)."""
from pathlib import Path
import hashlib
from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.pens.transformPen import TransformPen

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'components/lunaredu_character_assets/third_party/teko/Teko[wght].ttf'
DESTINATION = ROOT / 'main/assets/home_digits.ttf'


def main():
    if hashlib.sha256(SOURCE.read_bytes()).hexdigest() != 'd1321889f262bbbff632e7976349853399cd097b6f382d4b19790c915c13c1ae':
        raise SystemExit('Unexpected upstream Teko source')
    font = instantiateVariableFont(TTFont(SOURCE), {'wght': 400}, inplace=True)
    options = subset.Options()
    options.recalc_timestamp = False
    worker = subset.Subsetter(options=options)
    worker.populate(text='0123456789:-')
    worker.subset(font)
    # Stable, centred tabular digits: "11" must occupy the same pair of cells
    # as "00" and "23", without a layout jump at the next minute or hour.
    cmap = font.getBestCmap()
    advance = max(font['hmtx'][cmap[ord(c)]][0] for c in '0123456789')
    for c in '0123456789-':
        name = cmap[ord(c)]
        glyph = font['glyf'][name]
        shift = round((advance - (glyph.xMax - glyph.xMin)) / 2 - glyph.xMin)
        pen = TTGlyphPen(font.getGlyphSet())
        glyph.draw(TransformPen(pen, (1, 0, 0, 1, shift, 0)), font['glyf'])
        replacement = pen.glyph()
        replacement.recalcBounds(font['glyf'])
        font['glyf'][name] = replacement
        font['hmtx'][name] = (advance, replacement.xMin)
    # Digits have no descenders. Use their real bounds instead of the original
    # multilingual line box so LVGL can align them without transformed layers.
    glyphs = [font['glyf'][name] for name in font.getBestCmap().values()]
    top = max(g.yMax for g in glyphs if g.numberOfContours)
    bottom = min(g.yMin for g in glyphs if g.numberOfContours)
    font['hhea'].ascent = top + 20
    font['hhea'].descent = bottom - 20
    font['hhea'].lineGap = 0
    metrics = font['OS/2']
    metrics.sTypoAscender, metrics.sTypoDescender, metrics.sTypoLineGap = top + 20, bottom - 20, 0
    metrics.usWinAscent, metrics.usWinDescent = top + 20, max(0, 20 - bottom)
    # Name the modified font, retaining original copyright and license records.
    names = {1: 'Speaker Home Digits', 2: 'Regular', 3: 'SpeakerHomeDigits-1',
             4: 'Speaker Home Digits Regular', 6: 'SpeakerHomeDigits-Regular',
             16: 'Speaker Home Digits', 17: 'Regular'}
    for record in font['name'].names:
        if record.nameID in names:
            record.string = names[record.nameID].encode(record.getEncoding())
    font.recalcTimestamp = False
    font.save(DESTINATION, reorderTables=True)
    data = DESTINATION.read_bytes()
    print(DESTINATION, len(data), hashlib.sha256(data).hexdigest())


if __name__ == '__main__':
    main()
