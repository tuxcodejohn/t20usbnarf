import sys
import random
import time
import os.path

import usb.core
import usb.util

import Image, ImageDraw

from initdata import *

file_extensions = ['.jpg', '.jpeg', '.png']

def pack(raw):
    return ''.join(chr(i) for i in raw)

def full_write(ep, data):
    size = len(data)
    cur = 0

    while cur < size:
        delta = ep.write(data[cur:])
        cur += delta
        print "%i sent, %i remaining" % (delta, cur - size)

def read_list(path):
    for rel in sorted(os.listdir(path), key=str.lower):
        abs_path = os.path.join(path, rel)
        if os.path.isdir(abs_path):
            for sub in read_list(abs_path):
                yield sub
        elif os.path.splitext(rel)[1].lower() in file_extensions:
            yield abs_path

# find our device
dev = usb.core.find(idVendor=0x08ca, idProduct=0x2137)

# was it found?
if dev is None:
    raise ValueError('Device not found')

if len(sys.argv) > 1:
    files = []
    for directory in sys.argv[1:]:
        files.extend(read_list(directory))
else:
    files = list(read_list('.'))

# set the active configuration. With no arguments, the first
# configuration will be the active one
dev.set_configuration()


interface = dev[0][(0,0)]

interface.set_altsetting()

inp = interface[0]
raw = interface[1]
cmd = interface[2]

def run(lines):
    for line in lines:
        print ', '.join(hex(i) for i in line[8*3:])
        cmd.write(pack(line[8*3:]))

def batch(lines, wait=False):
    for line in lines:
        print ', '.join(hex(i) for i in line)

        cmd.write(pack(line))

        if line[0] == 0x05:
            print "read:", hex(inp.read(1)[0])
        elif line[0] == 0x25:
            print "read: ", inp.read(512)

        if wait:
            raw_input()

batch(phase0)

print "writing null bytes"

nullcmd = [
        0x11, 0x00, 0x00, 0x00, 0x00, 0xa0, 0x00, 0x78,
        0x00, 0x80, 0x02, 0xe0, 0x01, 0x00, 0x10, 0x00,
        0x10, 0x04, 0x00, 0x96, 0x00,
        ]

raw.write(pack(nullcmd))

# TODO: check whether this is the right amount!
for i in range(75):
    raw.write('\x00' * 512)

batch(phase1)

# non-working drawing

start = [
        0x11, 0x00, 0x00, 0x00, 0x00, 0x80, 0x02, 0xe0,
        0x01, 0x80, 0x02, 0xe0, 0x01, 0x00, 0x40, 0x00,
        0x40, 0x00, 0x00, 0x10, 0x0e,
        ]


def image_pixel(image):
    for pixel in image.getdata():
        for color in reversed(pixel):
            yield color

target_res = (640, 480)

rand = random.Random()

while True:
    fn = rand.choice(files)

    orig = Image.open(fn)

    scale = min(float(target) / old for old, target in zip(orig.size, target_res))
    new_size = tuple(int(old * scale) for old in orig.size)

    scaled = orig.resize(new_size, Image.ANTIALIAS)

    top = tuple((target - new) / 2 for new, target in zip(new_size, target_res))

    im = Image.new(mode="RGB", size=target_res)
    im.paste(scaled, top)

    data = ''.join(chr(i) for i in image_pixel(im))

    raw.write(pack(start))
    full_write(raw, data)

    time.sleep(15)

