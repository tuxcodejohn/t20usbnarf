#!/usr/bin/env python

import sys

import Image, ImageDraw

import t20

beamer = t20.ImageBeamer()

target_res = (640, 480)

if len(sys.argv) > 1:
    orig = Image.open(sys.argv[1])

    scale = min(float(target) / old for old, target in zip(orig.size, target_res))
    new_size = tuple(int(old * scale) for old in orig.size)

    scaled = orig.resize(new_size)

    top = tuple((target - new) / 2 for new, target in zip(new_size, target_res))

    im = Image.new(mode="RGB", size=target_res)
    im.paste(scaled, top)
else:
    im = Image.new(mode="RGB", size=target_res)

    draw = ImageDraw.Draw(im)
    draw.text((5, 5), "hello world!")

beamer.send_init()

beamer.send_image(im)

