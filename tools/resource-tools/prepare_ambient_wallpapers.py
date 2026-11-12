#!/usr/bin/env python3
"""Convert the checked-in generated artwork to the device's BMP/RGB565 assets."""
from pathlib import Path
from PIL import Image

folder = Path(__file__).resolve().parents[2] / 'main/assets/ambient'
with (folder / 'wallpapers.c').open('w') as output:
    output.write('#include <stdint.h>\nconst uint16_t speaker_ambient_wallpapers[4][57600] = {\n')
    for scene in ('rain', 'shore', 'forest', 'cafe'):
        image = Image.open(folder / (scene+'.png')).convert('RGB').resize((240,240),Image.Resampling.LANCZOS)
        image.save(folder / (scene+'.bmp'))
        pixels = [((r>>3)<<11)|((g>>2)<<5)|(b>>3) for r,g,b in image.getdata()]
        output.write('{\n')
        for start in range(0,len(pixels),16):
            output.write(','.join(hex(value) for value in pixels[start:start+16])+',\n')
        output.write('},\n')
    output.write('};\n')
