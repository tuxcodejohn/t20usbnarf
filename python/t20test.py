import usb.core
import usb.util

from initdata import *

def pack(raw):
    return ''.join(chr(i) for i in raw)

def run(lines):
    for line in lines:
        print ', '.join(hex(i) for i in line[8*3:])
        dev.write(3, pack(line[8*3:]))

# find our device
dev = usb.core.find(idVendor=0x08ca, idProduct=0x2137)

# was it found?
if dev is None:
    raise ValueError('Device not found')

# set the active configuration. With no arguments, the first
# configuration will be the active one
dev.set_configuration()

run(phase0)

print "writing null bytes"

nullcmd = [0x11, 0x00, 0x00, 0x00, 0x00, 0xa0, 0x00, 0x78, 0x00, 0x80, 0x02, 0xe0, 0x01, 0x00, 0x10, 0x00, 0x10, 0x04, 0x00, 0x96, 0x00]

dev.write(2, pack(nullcmd))

for i in range(77):
    dev.write(2, ''.join(chr(0) for i in range(512)))

run(phase1)

print "response", dev.read(1, 1)

run(phase2)

print "response", dev.read(1, 1)

run(phase3)

# non-working drawing

start = [0x11, 0x00, 0x00, 0x00, 0x00, 0x80, 0x02, 0xe0, 0x01, 0x80, 0x02, 0xe0, 0x01, 0x00, 0x40, 0x00, 0x40, 0x00, 0x00, 0x10, 0x0e]

dev.write(2, pack(start))

for i in range(1800):
    dev.write(2, ''.join(chr(0xFF) for i in range(512)))

