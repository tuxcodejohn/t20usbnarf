import usb.core
import usb.util

from initdata import *

def pack(raw):
    return ''.join(chr(i) for i in raw)

def full_write(ep, data):
    size = len(data)
    cur = 0

    while cur < size:
        delta = ep.write(data[cur:])
        cur += delta
        print "%i sent, %i remaining" % (delta, cur)


# find our device
dev = usb.core.find(idVendor=0x08ca, idProduct=0x2137)

# was it found?
if dev is None:
    raise ValueError('Device not found')

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

nullcmd = [0x11, 0x00, 0x00, 0x00, 0x00, 0xa0, 0x00, 0x78, 0x00, 0x80, 0x02, 0xe0, 0x01, 0x00, 0x10, 0x00, 0x10, 0x04, 0x00, 0x96, 0x00]

raw.write(pack(nullcmd))

# TODO: check whether this is the right amount!
for i in range(75):
    raw.write('\x00' * 512)

batch(phase1)

# non-working drawing

start = [0x11, 0x00, 0x00, 0x00, 0x00, 0x80, 0x02, 0xe0, 0x01, 0x80, 0x02, 0xe0, 0x01, 0x00, 0x40, 0x00, 0x40, 0x00, 0x00, 0x10, 0x0e]

raw.write(pack(start))

data = '\xff\xff\xff' * (640*480)

full_write(raw, data)

#for i in range(1800):
    #raw.write(''.join(chr(0xff if i%3 else 0x00) for i in range(512)))

# batch(post)

