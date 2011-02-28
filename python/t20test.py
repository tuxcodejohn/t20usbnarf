import usb.core
import usb.util

# find our device
dev = usb.core.find(idVendor=0x08ca, idProduct=0x2137)

# was it found?
if dev is None:
    raise ValueError('Device not found')

# set the active configuration. With no arguments, the first
# configuration will be the active one
dev.set_configuration()

# get an endpoint instance
ep = usb.util.find_descriptor(
        dev.get_interface_altsetting(),   # first interface
        # match the first OUT endpoint
        custom_match = \
            lambda e: \
                usb.util.endpoint_direction(e.bEndpointAddress) == \
                usb.util.ENDPOINT_OUT
    )

assert ep is not None

# write the data
ep.write('test')

