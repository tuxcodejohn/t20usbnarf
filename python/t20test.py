import usb.core
import usb.util

import initdata

# find our device
dev = usb.core.find(idVendor=0x08ca, idProduct=0x2137)

# was it found?
if dev is None:
    raise ValueError('Device not found')

# set the active configuration. With no arguments, the first
# configuration will be the active one
dev.set_configuration()


# send the initial foo
for line in initdata.data:
    dev.write(3, ''.join(chr(i) for i in line[17:]))

# non-working drawing

#start = [0x11, 0x00, 0x00, 0x00, 0x00, 0x80, 0x02, 0xe0, 0x01, 0x80, 0x02, 0xe0, 0x01, 0x00, 0x40, 0x00, 0x40, 0x00, 0x00, 0x10, 0x0e]

#dev.write(3, ''.join(chr(i) for i in start))

#for i in range(1800):
    #dev.write(3, ''.join(chr(0) for i in range(512)))

