#!/usr/bin/env python3
"""Original secondary-menu artwork; offline only (Pillow + resvg 2.6.2).

python tools/resource-tools/prepare_menu_icons.py --resvg-module \
  .local/wood-fish-tools/node_modules/@resvg/resvg-js
Produces the editable SVG contact sheet and flash-resident RGB565 images.
No SVG renderer, palette decode, SD access or runtime allocation in firmware.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]

def rect(x, y, w, h, color, radius=0):
    return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{radius}" fill="{color}"/>'

def circle(x, y, r, color):
    return f'<circle cx="{x}" cy="{y}" r="{r}" fill="{color}"/>'

def path(d, color):
    return f'<path d="{d}" fill="{color}"/>'

def rotate(body, angle=-5):
    return f'<g transform="rotate({angle} 74 74)">{body}</g>'

def artwork():
    record = circle(77, 78, 69, '#244c43') + circle(73, 73, 69, '#8be6c0') + circle(73, 73, 62, '#2e685b')
    for r in range(33, 61, 6):
        record += f'<circle cx="73" cy="73" r="{r}" fill="none" stroke="#397a69" stroke-width="2"/>'
    record += circle(73, 73, 27, '#f2e9d6') + circle(73, 73, 5, '#2e685b')
    wave = ''.join(rect(8+i*27, 74-h/2, 19, h, '#e4f1df' if i%2 else '#86e6c1', 10)
                   for i,h in enumerate([52,98,136,98,52]))
    chat = rect(29,48,113,87,'#8774ba',23) + rect(1,8,124,99,'#ddcaff',24)
    chat += path('M21 100H48L21 124Z','#ddcaff')
    chat += ''.join(rect(36+i*22,58-h/2,12,h,'#655086',6) for i,h in enumerate([28,48,28]))
    paw = ''.join(f'<ellipse cx="{x}" cy="{y}" rx="15" ry="21" fill="#e7d7ff" transform="rotate({r} {x} {y})"/>'
                  for x,y,r in [(17,61,-28),(51,30,-12),(97,30,12),(131,61,28)])
    paw += path('M29 116C29 88 51 67 74 67S119 88 119 116C119 145 91 132 74 132S29 145 29 116Z','#bca2e8')
    clock = circle(77,78,69,'#715d3e') + circle(73,73,69,'#fbd47a') + circle(73,73,59,'#f7efd9')
    clock += rect(70,18,6,12,'#b9a77e',3)+rect(70,116,6,12,'#b9a77e',3)+rect(18,70,12,6,'#b9a77e',3)+rect(116,70,12,6,'#b9a77e',3)
    clock += '<path d="M73 37V73L103 89" fill="none" stroke="#3e4355" stroke-width="8" stroke-linecap="round" stroke-linejoin="round"/>'
    timer = rect(9,32,134,110,'#79533f',48)+rect(5,27,134,110,'#f3a879',48)
    timer += path('M39 17L62 12L68 1H80L86 14L111 15L91 28L83 40L70 29L49 36L57 24Z','#88cba5')
    # Outline numerals keep raster generation independent of installed fonts.
    timer += '<path d="M47 57Q47 46 59 46T71 57Q71 64 48 79H72M100 46H81V61Q103 55 103 69Q103 83 81 78" fill="none" stroke="#4e3a37" stroke-width="6" stroke-linecap="round" stroke-linejoin="round"/>'
    timer += rect(19,91,106,3,'#be7957') + ''.join(rect(31+i*20,110,5,12,'#85533e',2) for i in range(5))
    calendar = rect(10,16,132,125,'#32415c',13)+rect(5,11,132,125,'#e9effa',13)
    calendar += rect(5,11,132,34,'#7acdff',13)+rect(5,31,132,14,'#7acdff')
    calendar += rect(32,1,8,25,'#e9effa',4)+rect(104,1,8,25,'#e9effa',4)
    for i in range(15):
        x,y=18+i%5*23,61+i//5*23
        if i==7: calendar += rect(x-4,y-4,22,22,'#7acdff',5)
        calendar += rect(x,y,14,14,'#167cab' if i==7 else '#b9c7dd',3)
    weather = circle(104,45,40,'#8f7447')+circle(100,40,40,'#fbd47a')
    weather += rect(8,83,134,58,'#497897',29)+rect(3,78,134,58,'#e9f3ff',29)+circle(64,76,38,'#e9f3ff')+circle(107,88,26,'#e9f3ff')
    fish = path('M6 103C6 64 37 49 78 52C120 54 141 79 136 112C130 142 34 150 13 128Z','#c6a477')
    fish += path('M5 96C5 61 36 48 76 50C116 51 132 73 130 104C126 132 34 140 13 122Z','#f4dfbb')
    fish += '<path d="M49 82Q80 90 104 65M8 104L56 95" fill="none" stroke="#11151e" stroke-width="8" stroke-linecap="round"/>'
    fish += '<g transform="rotate(-35 99 26)">'+rect(53,22,87,9,'#c6a477',5)+rect(45,12,33,30,'#f4dfbb',12)+'</g>'
    tiles = ''.join(rect(7+i%2*69,7+i//2*69,61,61,c,12) for i,c in enumerate(['#fbd47a','#ffc0a0','#ff9db7','#c8acff']))
    # Seven-segment numerals, not an external font asset.
    segments={'2':'abged','4':'fgbc','8':'abcdefg','1':'bc','6':'afgedc'}
    def digit(n,x,y):
        parts={'a':(3,0,13,3),'b':(16,3,3,11),'c':(16,17,3,11),'d':(3,28,13,3),'e':(0,17,3,11),'f':(0,3,3,11),'g':(3,14,13,3)}
        return ''.join(rect(x+parts[s][0],y+parts[s][1],parts[s][2],parts[s][3],'#292c39',1) for s in segments[n])
    tiles += digit('2',28,22)+digit('4',97,22)+digit('8',28,91)+digit('1',85,91)+digit('6',108,91)
    wifi = ''
    for d,c in [('M8 42Q74 -5 140 42','#c8d9f1'),('M30 74Q74 34 118 74','#9aafcf'),('M52 105Q74 85 96 105','#c8d9f1')]:
        wifi += f'<path d="{d}" fill="none" stroke="{c}" stroke-width="14" stroke-linecap="round"/>'
    wifi += circle(74,132,12,'#e9effa')
    device=rect(23,8,109,134,'#4b5a74',18)+rect(18,2,109,134,'#ccd9ef',18)+rect(30,15,85,75,'#5c7399',10)
    device+=circle(73,33,6,'#e9effa')+rect(68,46,10,29,'#e9effa',3)+rect(55,104,36,6,'#7d91b2',3)+rect(55,116,36,6,'#7d91b2',3)
    account=rect(10,9,127,127,'#4b5a74',20)+rect(5,3,127,127,'#e9effa',20)+circle(69,42,23,'#8aa1c3')
    account+=path('M27 110V100C27 67 112 67 112 100V110Z','#8aa1c3')+circle(120,121,23,'#315747')+circle(117,118,23,'#8be6c0')
    account+='<path d="M106 118L114 125L128 110" fill="none" stroke="#224e42" stroke-width="5" stroke-linecap="round" stroke-linejoin="round"/>'
    return [record,rotate(wave),rotate(chat),rotate(paw),rotate(clock),rotate(timer),rotate(calendar),rotate(weather),fish,rotate(tiles,-6),rotate(wifi),rotate(device),rotate(account)]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--resvg-module',required=True,type=Path)
    args=parser.parse_args()
    icons=artwork()
    target=ROOT/'main/assets/secondary_menu_icons.svg'
    sheet='<svg xmlns="http://www.w3.org/2000/svg" width="120" height="1560">\n'
    for i,body in enumerate(icons):
        sheet+=f'<svg y="{i*120}" width="120" height="120" viewBox="-8 -8 164 164">{body}</svg>\n'
    target.write_text(sheet+'</svg>\n')
    with tempfile.TemporaryDirectory(prefix='speaker-menu-icons-') as tmp:
        png=Path(tmp)/'icons.png'
        subprocess.run(['node','-e',"const fs=require('fs');const {Resvg}=require(process.argv[1]);fs.writeFileSync(process.argv[3],new Resvg(fs.readFileSync(process.argv[2])).render().asPng());",str(args.resvg_module.resolve()),str(target),str(png)],check=True)
        source=Image.open(png).convert('RGBA')
        background=Image.new('RGBA',source.size,(17,21,30,255))
        source=Image.alpha_composite(background,source).convert('RGB')
        out=['// Generated by tools/resource-tools/prepare_menu_icons.py; original project artwork.\n#include <lvgl.h>\n']
        for i in range(len(icons)):
            pixels=[]
            for r,g,b in source.crop((0,i*120,120,(i+1)*120)).getdata():
                value=((r>>3)<<11)|((g>>2)<<5)|(b>>3)
                pixels.extend([value&255,value>>8])
            out.append(f'static const uint8_t pixels_{i}[] = {{\n')
            out.extend(','.join(str(v) for v in pixels[j:j+32])+',\n' for j in range(0,len(pixels),32))
            out.append('};\n')
        out.append('const lv_image_dsc_t speaker_secondary_icons[13] = {\n')
        for i in range(len(icons)):
            out.append(f'{{.header = {{.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565, .w = 120, .h = 120, .stride = 240}}, .data_size = sizeof(pixels_{i}), .data = pixels_{i}}},\n')
        out.append('};\n')
        (ROOT/'main/assets/secondary_menu_icons.c').write_text(''.join(out))
    print('13 original 120x120 icons; 374400 bytes RGB565 in flash')

if __name__=='__main__':
    main()
