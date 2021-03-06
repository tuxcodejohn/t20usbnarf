import StringIO

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
        #print "%i sent, %i remaining" % (delta, cur - size)

class Beamer:
    '''Class representing an USB beamer'''

    resolution = (640, 480)

    def __init__(self):
        '''Find a beamer. No device initialization, yet (see send_init())'''

        # find our device
        self.dev = dev = usb.core.find(idVendor=0x08ca, idProduct=0x2137)

        # was it found?
        if dev is None:
            raise ValueError('Device not found')

        dev.set_configuration()

        # there is only one configuration and one interface
        self.interface = interface = dev[0][(0,0)]
        interface.set_altsetting()

        # save endpoints
        self.inp = interface[0]
        self.raw = interface[1]
        self.cmd = interface[2]

    def cmd_batch(self, lines, wait=False):
        '''Process a voodoo batch and its input'''

        cmd = self.cmd
        inp = self.inp

        for line in lines:
            print ', '.join(hex(i) for i in line)

            cmd.write(pack(line))

            if line[0] == 0x05:
                print "read:", hex(inp.read(1)[0])
            elif line[0] == 0x25:
                print "read: ", inp.read(512)

            if wait:
                raw_input()

    def send_init(self):
        '''Send the init voodoo'''

        # TODO: this is pure voodoo ... research!
        raw = self.raw

        self.cmd_batch(phase0)

        print "writing null bytes"

        nullcmd = [
                0x11, 0x00, 0x00, 0x00, 0x00, 0xa0, 0x00, 0x78,
                0x00, 0x80, 0x02, 0xe0, 0x01, 0x00, 0x10, 0x00,
                0x10, 0x04, 0x00, 0x96, 0x00,
                ]

        raw.write(pack(nullcmd))

        full_write(raw, '\x00' * (75*512))

        self.cmd_batch(phase1)

    def send_raw_frame(self, raw_data):
        '''Send 640x480 Pixel RGB data'''

        raw = self.raw

        start = [
                0x11, 0x00, 0x00, 0x00, 0x00, 0x80, 0x02, 0xe0,
                0x01, 0x80, 0x02, 0xe0, 0x01, 0x00, 0x40, 0x00,
                0x40, 0x00, 0x00, 0x10, 0x0e,
                ]

        raw.write(pack(start))

        full_write(raw, raw_data)

# TODO: move away to loose PIL dependency in the core
class ImageBeamer(Beamer):
    '''Beamer with ability to send PIL images'''

    def send_image(self, im):
        if im.size != self.resolution:
            raise ValueError("send_image() sends only scaled images")

        raw = StringIO.StringIO()

        for pixel in im.getdata():
            # we need GBR
            for color in reversed(pixel):
                raw.write(chr(color))

        self.send_raw_frame(raw.getvalue())

