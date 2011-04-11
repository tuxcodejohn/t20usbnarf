#!/usr/bin/env python

import sys
import time
import StringIO

import Image, ImageDraw

import t20

if len(sys.argv) <= 1:
    print "please give me a gif!"
    sys.exit(1)

beamer = t20.ImageBeamer()

target_res = (640, 480)

orig = Image.open(sys.argv[1])

scale = min(float(target) / old for old, target in zip(orig.size, target_res))
new_size = tuple(int(old * scale) for old in orig.size)

top = tuple((target - new) / 2 for new, target in zip(new_size, target_res))

im = Image.new(mode="RGB", size=target_res)

frames = []

try:
    while True:
        print orig.tell()

        scaled = orig.resize(new_size, Image.ANTIALIAS)

        im.paste(scaled, top)

        raw = StringIO.StringIO()

        for pixel in im.getdata():
            for color in reversed(pixel):
                raw.write(chr(color))

        frames.append(raw.getvalue())

        orig.seek(orig.tell() + 1)
except Exception as e:
    print "images extracted"

beamer.send_init()

while True:
    for raw in frames:
        beamer.send_raw_frame(raw)
        time.sleep((1 - (time.time() * 10) % 1) * 0.1)

