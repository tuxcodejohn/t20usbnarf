

/*
 * USB Skeleton driver2.2 based driver for aiptek T20 mini usb projector
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/mutex.h>

#include <linux/fb.h>

#define USB_T20AIPTEK_VENDOR_ID		0x08ca
#define USB_T20AIPTEK_PRODUCT_ID	0x2137

/* table of devices that work with this driver */
static const struct usb_device_id t20aiptek_table[] = {
	{ USB_DEVICE(USB_T20AIPTEK_VENDOR_ID, USB_T20AIPTEK_PRODUCT_ID) },
	{ }					/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, t20aiptek_table);



/* our private defines */

#include "initmagic_t20aiptek.h"

#define FRAME_SIZE		(640*480*3)
#define FRAME_WIDTH		(640)
#define FRAME_HEIGHT		(480)

#define INPUT_EP		1
#define RAW_EP			2
#define COMMAND_EP		3

#define NULL_BULK_LEN		(75*512)

#define MAX_TRANSFER		(PAGE_SIZE - 512)
/* MAX_TRANSFER is chosen so that the VM is not stressed by
   allocations > PAGE_SIZE and the number of packets in a page
   is an integer 512 is the largest possible packet on EHCI */
#define WRITES_IN_FLIGHT	8
/* arbitrarily chosen */

/*
 * The hardware only handles a single mode: 640x480 24 bit true
 * color. Each pixel gets a word (32 bits) of memory.  Within each word,
 * the 8 most significant bits are ignored, the next 8 bits are the red
 * level, the next 8 bits are the green level and the 8 least
 * significant bits are the blue level.  Each row of the LCD uses 1024
 * words, but only the first 640 pixels are displayed with the other 384
 * words being ignored.  There are 480 rows.
 */
#define BYTES_PER_PIXEL	4
#define BITS_PER_PIXEL	(BYTES_PER_PIXEL * 8)

#define RED_SHIFT	16
#define GREEN_SHIFT	8
#define BLUE_SHIFT	0

#define PALETTE_ENTRIES_NO	16



/*
 * Here are the default fb_fix_screeninfo and fb_var_screeninfo structures
 */
static struct fb_fix_screeninfo t20aiptek_fb_fix = {
	.id =		"Aiptek T20 USB",
	.type =		FB_TYPE_PACKED_PIXELS,
	.visual =	FB_VISUAL_TRUECOLOR,
	.accel =	FB_ACCEL_NONE
};

static struct fb_var_screeninfo t20aiptek_fb_var = {
	.bits_per_pixel =	BITS_PER_PIXEL,
	.grayscale	= 0 ,/*well at least I paid for color device*/
	.height		= 30,  /*of course this is bullshit :-) */
	.width		= 40,
	.xres		= 1024,
	.xres_virtual	=  640,
	.yres		=  480,
	.yres_virtual	=  480,
	.red =		{ RED_SHIFT, 8, 0 },
	.green =	{ GREEN_SHIFT, 8, 0 },
	.blue =		{ BLUE_SHIFT, 8, 0 },
	.transp =	{ 0, 0, 0 },
	.activate =	FB_ACTIVATE_NOW
};

/* Structure to hold all of our device specific stuff */
struct usb_t20aiptek {
	struct usb_device	*udev;			/* the usb device for this device */
	struct usb_interface	*interface;		/* the interface for this device */
	struct semaphore	limit_sem;		/* limiting the number of writes in progress */
	struct usb_anchor	submitted;		/* in case we need to retract our submissions */
	struct urb		*bulk_in_urb;		/* the urb to read data with */
	unsigned char           *bulk_in_buffer;	/* the buffer to receive data */
	size_t			bulk_in_size;		/* the size of the receive buffer */
	size_t			bulk_in_filled;		/* number of bytes in the buffer */
	size_t			bulk_in_copied;		/* already copied to user space */
	__u8			bulk_in_endpointAddr;	/* the address of the bulk in endpoint */
	__u8			bulk_out_endpointAddr;	/* the address of the bulk out endpoint */
	__u8			bulk_input_endpointAddr;
	__u8			bulk_raw_endpointAddr;
	__u8 			bulk_command_endpointAddr;
	int			errors;			/* the last request tanked */
	bool			processed_urb;		/* indicates we haven't processed the urb */
	spinlock_t		err_lock;		/* lock for errors */
	struct kref		kref;
	struct mutex		io_mutex;		/* synchronize I/O with disconnect */
	struct completion	bulk_in_completion;	/* to wait for an ongoing read */
	/*framebufferstuff follows :*/
	struct fb_info		info;			/* frame buffer info */
	void			*fb_virt;	/* virt. address of the frame buffer */
	dma_addr_t		fb_phys;	/* phys. address of the frame buffer */
	
	u32		pseudo_palette[PALETTE_ENTRIES_NO];
					/* Fake palette of 16 colors */

};
#define to_t20aiptek_dev(d) container_of(d, struct usb_t20aiptek, kref)
#define fbinfo_to_t20aiptek_dev(d) container_of(d, struct usb_t20aiptek, info)

static struct usb_driver t20aiptek_driver;
static void t20aiptek_draw_down(struct usb_t20aiptek *dev);

static void t20_delete(struct kref *kref)
{
	struct usb_t20aiptek *dev = to_t20aiptek_dev(kref);

	usb_free_urb(dev->bulk_in_urb);
	usb_put_dev(dev->udev);
	kfree(dev->bulk_in_buffer);
	kfree(dev);
}



static int t20_probe(struct usb_interface *interface,
		      const struct usb_device_id *id)
{
	struct usb_t20aiptek *dev;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	size_t buffer_size;
	int i;
	int retval = -ENOMEM;

	/* allocate memory for our device state and initialize it */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		err("Out of memory");
		goto error;
	}
	kref_init(&dev->kref);
	sema_init(&dev->limit_sem, WRITES_IN_FLIGHT);
	mutex_init(&dev->io_mutex);
	spin_lock_init(&dev->err_lock);
	init_usb_anchor(&dev->submitted);
	init_completion(&dev->bulk_in_completion);

	dev->udev = usb_get_dev(interface_to_usbdev(interface));
	dev->interface = interface;

	/* set up the endpoint information */
	/* use only the first bulk-in and bulk-out endpoints */
	iface_desc = interface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (!dev->bulk_in_endpointAddr &&
		    usb_endpoint_is_bulk_in(endpoint)) {
			/* we found a bulk in endpoint */
			buffer_size = le16_to_cpu(endpoint->wMaxPacketSize);
			dev->bulk_in_size = buffer_size;
			dev->bulk_in_endpointAddr = endpoint->bEndpointAddress;
			dev->bulk_in_buffer = kmalloc(buffer_size, GFP_KERNEL);
			if (!dev->bulk_in_buffer) {
				err("Could not allocate bulk_in_buffer");
				goto error;
			}
			dev->bulk_in_urb = usb_alloc_urb(0, GFP_KERNEL);
			if (!dev->bulk_in_urb) {
				err("Could not allocate bulk_in_urb");
				goto error;
			}
		}

		if (!dev->bulk_out_endpointAddr &&
		    usb_endpoint_is_bulk_out(endpoint)) {
			/* we found a bulk out endpoint */
			dev->bulk_out_endpointAddr = endpoint->bEndpointAddress;
		}
	}
	if (!(dev->bulk_in_endpointAddr && dev->bulk_out_endpointAddr)) {
		err("Could not find both bulk-in and bulk-out endpoints");
		goto error;
	}

	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, dev);

	
	/*frambuffer initialisation :*/
	retval = t20aiptek_fb_init(dev)
	if(retval){
		err("Problems during framebuffer initalisation. Scheiße!");
		goto error;
	}
	return 0;

error:
	if (dev)
		/* this frees allocated memory */
		kref_put(&dev->kref, t20_delete);
	return retval;
}

static void t20_fillrect(struct fb_info *info, const struct fb_fillrect *rect);
static void t20_copyarea(struct fb_info *info, const struct fb_copyarea *area);
static void t20_imageblit(struct fb_info *info, const struct fb_image *image);
static int t20_blank(int blank_mode, struct fb_info *fbi);


static struct fb_ops t20aiptekfb_ops =
{
	.owner			= THIS_MODULE,
	.fb_setcolreg		= t20_setcolreg,
	.fb_blank		= t20_blank,
	.fb_fillrect		= t20_fillrect,
	.fb_copyarea		= t20_copyarea,
	.fb_imageblit		= t20_imageblit,
};


static int
t20_blank(int blank_mode, struct fb_info *fbi)
{
	struct usb_t20aiptek *drvdata = fbinfo_to_t20aiptek_dev(fbi);

	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
		/* turn on panel */
		//TODO
		break;

	case FB_BLANK_NORMAL:
	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_HSYNC_SUSPEND:
	case FB_BLANK_POWERDOWN:
		/* turn off panel */
		//TODO	
	default:
		break;

	}
	return 0; /* success */
}

static void t20_fillrect(struct fb_info *info, const struct fb_fillrect *rect){
	sys_fillrect(info, rect);
	t20_sendimage();
}

static void t20_copyarea(struct fb_info *info, const struct fb_copyarea *area){
	sys_copyarea(info , area);
	t20_sendimage();
}

static void t20_imageblit(struct fb_info *info, const struct fb_image *image){
	sys_imageblit(info,image);
	t20_sendimage();
}

static void t20aiptek_fb_init(struct usb_t20aiptek * drvdata)
{
	int rc;
	size_t fbsize = t20aiptek_fb_var.xvirt * t20aiptek_fb_var.yvirt * BYTES_PER_PIXEL;

	/* allocate frambuffer memory */
	drvdata->fb_virt = dma_alloc_coherent(dev, PAGE_ALIGN(fbsize),
			&drvdata->fb_phys, GFP_KERNEL);
	if (!drvdata->fb_virt) {
		dev_err(dev, "Could not allocate frame buffer memory\n");
		rc = -ENOMEM;
		goto err_nallok;
	}

	/* Clear (turn to black) the framebuffer */
	memset_io((void __iomem *)drvdata->fb_virt, 0, fbsize);


	/* Fill struct fb_info */
	drvdata->info.device = dev;
	drvdata->info.screen_base = (void __iomem *)drvdata->fb_virt;
	drvdata->info.fbops = &t20aiptekfb_ops;
	drvdata->info.fix = t20aiptek_fb_fix;
	drvdata->info.fix.smem_start = drvdata->fb_phys;
	drvdata->info.fix.smem_len = fbsize;
	drvdata->info.fix.line_length = pdata->xvirt * BYTES_PER_PIXEL;
	drvdata->info.pseudo_palette = drvdata->pseudo_palette;
	drvdata->info.flags = FBINFO_DEFAULT;
	drvdata->info.var = t20aiptek_fb_var;


	/* Allocate a colour map */
	rc = fb_alloc_cmap(&drvdata->info.cmap, PALETTE_ENTRIES_NO, 0);
	if (rc) {
		dev_err(dev, "Fail to allocate colormap (%d entries)\n",
				PALETTE_ENTRIES_NO);
		goto err_cmap;
	}

	/* Register new frame buffer */
	rc = register_framebuffer(&drvdata->info);
	if (rc) {
		dev_err(dev, "Could not register frame buffer\n");
		goto err_regfb;
	}
	/* Put a banner in the log (for DEBUG) */
	dev_dbg(dev, "fb: phys=%llx, virt=%p, size=%x\n",
			(unsigned long long)drvdata->fb_phys, drvdata->fb_virt, fbsize);

	return 0;	/* success */

err_regfb:
	fb_dealloc_cmap(&drvdata->info.cmap);

err_cmap:
	dma_free_coherent(dev, PAGE_ALIGN(fbsize), drvdata->fb_virt,
			drvdata->fb_phys);

	/* Turn off the display */
	//TODO

err_nallok:
	kfree(drvdata);
	dev_set_drvdata(dev, NULL);

	return rc;
}



static void t20_disconnect(struct usb_interface *interface)
{
	struct usb_t20aiptek *dev;

	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	/* give back our minor */
	usb_deregister_dev(interface, &t20aiptek_class);

	/* prevent more I/O from starting */
	mutex_lock(&dev->io_mutex);
	dev->interface = NULL;
	mutex_unlock(&dev->io_mutex);

	usb_kill_anchored_urbs(&dev->submitted);

	/* decrement our usage count */
	kref_put(&dev->kref, t20_delete);

	dev_info(&interface->dev, "USB Skeleton #%d now disconnected", minor);
}


static int t20_fb_release(struct usb_t20aiptek *drvdata)
{

	//TODO maybe do blanking stuff

	unregister_framebuffer(&drvdata->info);
	fb_dealloc_cmap(&drvdata->info.cmap);
	dma_free_coherent(dev, PAGE_ALIGN(drvdata->info.fix.smem_len),
				  drvdata->fb_virt, drvdata->fb_phys);

	kfree(drvdata);
	dev_set_drvdata(dev, NULL);
	return 0;
}

static struct usb_driver t20aiptek_driver = {
	.name =		"t20aiptek",
	.probe =	t20_probe,
	.disconnect =	t20_disconnect,
	.suspend =	t20_suspend, //Maybe we cant
	.resume =	t20_resume,  //do this ?? -> more reversing needed
	.id_table =	t20_table,
	.supports_autosuspend = 1,
};

static int __init usb_t20aiptek_init(void)
{
	int result;

	/* register this driver with the USB subsystem */
	result = usb_register(&t20aiptek_driver);
	if (result)
		err("usb_register failed. Error number %d", result);

	return result;
}

static void __exit usb_t20aiptek_exit(void)
{
	/* deregister this driver with the USB subsystem */
	usb_deregister(&t20aiptek_driver);
}

module_init(usb_t20aiptek_init);
module_exit(usb_t20aiptek_exit);

MODULE_AUTHOR("john at tuxcode dot org, FIXME THAMMI ; <<</>> c3d2.de");
MODULE_DESCRIPTION("Aiptek T20 USB mini projector frame buffer driver");

MODULE_LICENSE("GPL"); 

int init_beamer(beamer_handle beamer) {
	int i, transferred;

	for(i = 0; i < sizeof(phase0)/sizeof(phase0[0]); ++i) {
		char* data = phase0[i];
		size_t len = command_length(data);

		libusb_bulk_transfer(beamer, (COMMAND_EP | LIBUSB_ENDPOINT_OUT), data, len, &transferred, 2000);
	}

	char nullcmd_data[] = "\x11\x00\x00\x00\x00\xa0\x00\x78\x00\x80\x02\xe0\x01\x00\x10\x00\x10\x04\x00\x96\x00";
	size_t nullcmd_len = 21;

	libusb_bulk_transfer(beamer, (RAW_EP | LIBUSB_ENDPOINT_OUT), nullcmd_data, nullcmd_len, &transferred, 2000);

	char* null_space = malloc(NULL_BULK_LEN);
	memset(null_space, 0, NULL_BULK_LEN);
	libusb_bulk_transfer(beamer, (RAW_EP | LIBUSB_ENDPOINT_OUT), null_space, NULL_BULK_LEN, &transferred, 2000);
	free(null_space);

	for(i = 0; i < sizeof(phase1)/sizeof(phase1[0]); ++i) {
		char* data = phase1[i];
		size_t len = command_length(data);

		libusb_bulk_transfer(beamer, (COMMAND_EP | LIBUSB_ENDPOINT_OUT), data, len, &transferred, 2000);
	}

	return 0;
}

int send_white_image(beamer_handle beamer) {
	char* data = malloc(FRAME_SIZE);
	memset(data, 0xff, FRAME_SIZE);
	send_raw_image(beamer, data);
	free(data);
}

int send_raw_image(beamer_handle beamer, char* data) {
	char* startcmd_data = "\x11\x00\x00\x00\x00\x80\x02\xe0\x01\x80\x02\xe0\x01\x00\x40\x00\x40\x00\x00\x10\x0e";
	size_t startcmd_len = 21;
	int transferred;

	puts("sending frame");

	libusb_bulk_transfer(beamer, (RAW_EP | LIBUSB_ENDPOINT_OUT), startcmd_data, startcmd_len, &transferred, 2000);

	libusb_bulk_transfer(beamer, (RAW_EP | LIBUSB_ENDPOINT_OUT), data, FRAME_SIZE, &transferred, 2000);
}
