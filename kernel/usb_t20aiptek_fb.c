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
#include <linux/mm.h>
#include <linux/hrtimer.h>



#define err(format, arg...)  printk(KERN_ERR "t20error: " format "\n" , ## arg)

#include <linux/fb.h>

#define USB_T20AIPTEK_VENDOR_ID		0x08ca
#define USB_T20AIPTEK_PRODUCT_ID	0x2137

/* table of devices that work with this driver */
static const struct usb_device_id t20_table[] = {
	{ USB_DEVICE(USB_T20AIPTEK_VENDOR_ID, USB_T20AIPTEK_PRODUCT_ID) },
	{ }					/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, t20_table);



/* our private defines */

#include "initmagic_t20aiptek.h"

#define T20FRAME_SIZE		(640*480*3)
#define T20FRAME_WIDTH		(640)
#define T20FRAME_HEIGHT		(480)
#define T20FRAME_PERSEC           (25)

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
 * 3 sux, so ill try other way but later...
 */
#define BYTES_PER_PIXEL	3
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
	.xres		= T20FRAME_WIDTH,
	.xres_virtual	= T20FRAME_WIDTH,
	.yres		= T20FRAME_HEIGHT,
	.yres_virtual	= T20FRAME_HEIGHT,
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
	bool			processed_urb;		/* indicates we haven't processed the urb */
	struct kref		kref;
	struct mutex		io_mutex;		/* synchronize I/O with disconnect */
	struct completion	bulk_in_completion;	/* to wait for an ongoing read */
	
	ktime_t framedelay;
	struct hrtimer frametimer;
	bool usbdirty;

	/*framebufferstuff follows :*/
	struct fb_info		info;			/* frame buffer info */
	struct mutex		fb_mutex;	/*let only atomic streamread */	
	void * 			fb_virt;	/* virt. address of the frame buffer */
	dma_addr_t		fb_phys;	/* phys. address of the frame buffer */
	
	u32		pseudo_palette[PALETTE_ENTRIES_NO];
					/* Fake palette of 16 colors */

};
#define to_t20aiptek_dev(d) container_of(d, struct usb_t20aiptek, kref)
#define fbinfo_to_t20aiptek_dev(d) container_of(d, struct usb_t20aiptek, info)

static struct usb_driver t20aiptek_driver;
//static void t20aiptek_draw_down(struct usb_t20aiptek *dev);

static int t20_fb_init(struct usb_t20aiptek * drvdata);
static int t20_beamer_init(struct usb_t20aiptek * drvdata);


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
	//struct usb_endpoint_descriptor *endpoint;
	size_t buffer_size;
	int retval = -ENOMEM;

	/* allocate memory for our device state and initialize it */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		dev_err(&(dev->udev->dev),"Out of memory");
		goto error;
	}
	kref_init(&dev->kref);
	mutex_init(&dev->io_mutex);
	mutex_init(&dev->fb_mutex);
	init_completion(&dev->bulk_in_completion);

	dev->udev = usb_get_dev(interface_to_usbdev(interface));
	dev->interface = interface;

	/* set up the endpoint information */
	/* use only the first bulk-in and bulk-out endpoints */
	iface_desc = interface->cur_altsetting;
	//do sophisticated stuff later :-D
#if 0
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
#else
	dev->bulk_input_endpointAddr	= INPUT_EP;
	dev->bulk_raw_endpointAddr	= RAW_EP;
	dev->bulk_command_endpointAddr	= COMMAND_EP;
	buffer_size = 512 ;
#endif 
	dev->bulk_in_size = buffer_size;
	dev->bulk_in_buffer = kmalloc(buffer_size, GFP_KERNEL);
	if (!dev->bulk_in_buffer) {
		dev_err(&interface->dev,"Could not allocate bulk_in_buffer");
		goto error;
	}
	dev->bulk_in_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->bulk_in_urb) {
		dev_err(&interface->dev,"Could not allocate bulk_in_urb");
		goto error;
	}


	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, dev);

	
	/*frambuffer initialisation :*/
	retval = t20_fb_init(dev);
	if(retval){
		dev_err(&interface->dev,"Problems during framebuffer initalisation. Scheiße!");
		goto error;
	}
	
	/*send init seq and switch on lamp*/
	retval = t20_beamer_init(dev);
	if(retval){
		dev_err(&interface->dev,"fuck that doesn't work. debug it!");
		goto error;
	}
	
	/* initialize frame sending updater */


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
static int t20_fb_release(struct usb_t20aiptek *drvdata);
static int t20_setcolreg(unsigned regno, unsigned red, unsigned green,
			    unsigned blue, unsigned transp, struct fb_info *info);

static void t20_sendimage(void);


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
	//struct usb_t20aiptek *drvdata = fbinfo_to_t20aiptek_dev(fbi);

	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
		/* turn on panel */
		//TODO
		fbinfo_to_t20aiptek_dev(fbi)->usbdirty = 1;
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

static int t20_setcolreg(unsigned regno, unsigned red, unsigned green,
			    unsigned blue, unsigned transp, struct fb_info *info)
{
	return 0;
}


static void t20_fillrect(struct fb_info *info, const struct fb_fillrect *rect){
	struct usb_t20aiptek *dev = fbinfo_to_t20aiptek_dev(info);
	mutex_lock(&dev->fb_mutex);
	sys_fillrect(info, rect);	
	dev->usbdirty = 1;
	//t20_sendimage();
	mutex_unlock(&dev->fb_mutex);
}

static void t20_copyarea(struct fb_info *info, const struct fb_copyarea *area){
	struct usb_t20aiptek *dev = fbinfo_to_t20aiptek_dev(info);

	mutex_lock(&dev->fb_mutex);
	sys_copyarea(info , area);
	dev->usbdirty = 1;
	//t20_sendimage();
	mutex_unlock(&dev->fb_mutex);
}

static void t20_imageblit(struct fb_info *info, const struct fb_image *image){
	struct usb_t20aiptek *dev = fbinfo_to_t20aiptek_dev(info);
	mutex_lock(&dev->fb_mutex);
	sys_imageblit(info,image);
	dev->usbdirty = 1;
	//t20_sendimage();
	mutex_unlock(&dev->fb_mutex);
}

static void t20_sendimage(void)
{
	return;
}


static int  t20_fb_init(struct usb_t20aiptek * drvdata)
{
	int rc;
	size_t fbsize = t20aiptek_fb_var.xres_virtual * t20aiptek_fb_var.yres_virtual * BYTES_PER_PIXEL;

	/* allocate frambuffer memory */
	drvdata->fb_virt = kmalloc(fbsize, GFP_KERNEL);
	if (!drvdata->fb_virt) {
		dev_err(drvdata->info.dev, "Could not allocate frame buffer memory\n");
		rc = -ENOMEM;
		goto err_nallok;
	}

	/* Clear (turn to black) the framebuffer */
	memset(drvdata->fb_virt, 0, fbsize);


	/* Fill struct fb_info */
	drvdata->info.device = &(drvdata->udev->dev);
	drvdata->info.screen_base = (void __iomem *)drvdata->fb_virt;
	drvdata->info.fbops = &t20aiptekfb_ops;
	drvdata->info.fix = t20aiptek_fb_fix;
	drvdata->info.fix.smem_start = drvdata->fb_phys;
	drvdata->info.fix.smem_len = fbsize;
	drvdata->info.fix.line_length = drvdata->info.var.xres_virtual  * BYTES_PER_PIXEL;
	drvdata->info.pseudo_palette = drvdata->pseudo_palette;
	drvdata->info.flags = FBINFO_DEFAULT;
	drvdata->info.var = t20aiptek_fb_var;


	/* Allocate a colour map */
	rc = fb_alloc_cmap(&drvdata->info.cmap, PALETTE_ENTRIES_NO, 0);
	if (rc) {
		dev_err(drvdata->info.dev, "Fail to allocate colormap (%d entries)\n",
				PALETTE_ENTRIES_NO);
		goto err_cmap;
	}

	/* Register new frame buffer */
	rc = register_framebuffer(&drvdata->info);
	if (rc) {
		dev_err(drvdata->info.dev, "Could not register frame buffer\n");
		goto err_regfb;
	}
	/* Put a banner in the log (for DEBUG) */
	dev_dbg(drvdata->info.dev, "fb: phys=%llx, virt=%p, size=%x\n",
			(unsigned long long)drvdata->fb_phys, drvdata->fb_virt,(unsigned int) fbsize);


	/* “Configuration” */
	drvdata->framedelay = ktime_set(0, (NSEC_PER_SEC / T20FRAME_PERSEC));

	return 0;	/* success */

err_regfb:
	fb_dealloc_cmap(&drvdata->info.cmap);

err_cmap:
	kfree(drvdata->fb_virt);

	/* Turn off the display */
	//TODO

err_nallok:
	kfree(drvdata);
	dev_set_drvdata(drvdata->info.dev, NULL);

	return rc;
}


static int t20_beamer_init(struct usb_t20aiptek  *dev )
{

	int i,pipe,retval;
	size_t len;
	const char * data;
	int actlen;
	const char nullcmd_data[]= "\x11\x00\x00\x00\x00\xa0\x00\x78\x00\x80\x02\xe0\x01\x00\x10\x00\x10\x04\x00\x96\x00";
	char * null_space;
	struct usb_device *usbdev = dev->udev;

	pipe = usb_sndbulkpipe(usbdev, dev->bulk_command_endpointAddr);
	for(i = 0; i < ARRAY_SIZE(phase0) ; ++i) {
		data = phase0[i];
		len = command_length(data);
		retval = usb_bulk_msg(usbdev, pipe, (void *)data, len, &actlen, HZ * 10);
		if (retval || (len!=actlen)) {
			dev_err(&(usbdev->dev),"Mhhh sending stuff didnt work");
			return 1;
		}
		/*if ( *data = 0x04) {*/
			/*wait_for_10_ms*/
		/*}*/
	}

	retval = usb_bulk_msg(usbdev , 
			usb_sndbulkpipe(usbdev , dev->bulk_raw_endpointAddr) ,
			(void *)nullcmd_data ,
			ARRAY_SIZE(nullcmd_data),
			&actlen,
			HZ *10 );
	if (retval || (len!=actlen)) {
		dev_err(&(usbdev->dev),"Mhhh sending stuff didnt work");
		return 1;
	}
	if (NULL_BULK_LEN < PAGE_SIZE)
		null_space = (char *) get_zeroed_page(GFP_KERNEL);
	else 
		 { null_space = 0; //TODO done right....
		 }

	retval = usb_bulk_msg(usbdev , 
			usb_sndbulkpipe(usbdev , dev->bulk_raw_endpointAddr),
			null_space,
			NULL_BULK_LEN, 
			&actlen ,
			HZ *10 );
	if (retval || (NULL_BULK_LEN != actlen)) {
		dev_err(&(usbdev->dev),"Mhhh sending zeros didnt work");
		return 1;
	}

	free_page((unsigned long)null_space);
	for(i = 0; i < sizeof(phase1)/sizeof(phase1[0]); ++i) {
		data = phase1[i];
		len = command_length(data);
		retval = usb_bulk_msg(usbdev , 
				usb_sndbulkpipe(usbdev , dev->bulk_out_endpointAddr),
				(void *)data,
				len, 
				&actlen ,
				HZ *10 );
		if (retval || (len!=actlen)) {
			dev_err(&usbdev->dev,"Mhhh sending phase1[%d]",i);
			return 1;
		}
	}

	return 0;
}


static void t20_disconnect(struct usb_interface *interface)
{
	struct usb_t20aiptek *dev;

	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	/* prevent more I/O from starting */
	mutex_lock(&dev->io_mutex);
	dev->interface = NULL;
	mutex_unlock(&dev->io_mutex);

	 t20_fb_release(dev);
	/* decrement our usage count */
	kref_put(&dev->kref, t20_delete);
	dev_info(&interface->dev, "USB stuffz now disconnected");
}


static int t20_fb_release(struct usb_t20aiptek *drvdata)
{

	//TODO maybe do blanking stuff 

	unregister_framebuffer(&drvdata->info);
	fb_dealloc_cmap(&drvdata->info.cmap);
	kfree(drvdata->fb_virt);
	kfree(drvdata);
	dev_set_drvdata(drvdata->info.dev, NULL);
	return 0;
}

static struct usb_driver t20aiptek_driver = {
	.name =		"t20aiptek",
	.probe =	t20_probe,
	.disconnect =	t20_disconnect,
	//.suspend =	t20_suspend, //Maybe we cant
	//.resume =	t20_resume,  //do this ?? -> more reversing needed
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




/* hrtimer interrupt service routine: */
	static enum hrtimer_restart 
_t20framesend_timer_ISR(struct hrtimer *hrt)
{
	struct usb_t20aiptek *t20;
	t20 = container_of(hrt,struct usb_t20aiptek,frametimer);
	
	if (t20->usbdirty)
	{
		/*register softIRQ work*/


	}
	hrtimer_forward_now(hrt,t20->framedelay);
	return HRTIMER_RESTART; 
}





module_init(usb_t20aiptek_init);
module_exit(usb_t20aiptek_exit);

MODULE_AUTHOR("john at tuxcode dot org, thammi at chaossource dot net ; <<</>> c3d2.de");
MODULE_DESCRIPTION("Aiptek T20 USB mini projector frame buffer driver");

MODULE_LICENSE("GPL"); 


#if 0 // Eine Halde mit Altcode:
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

static void fetch_senddata(struct usb_t20aiptek *dev){

	int i;
	mutex_lock(&dev->fb_mutex);
	for i 


static int simple_io(
        struct usbtest_dev      *tdev,
        struct urb              *urb,
        int                     iterations,
        int                     vary,
        int                     expected,
        const char              *label
)
{
        struct usb_device       *udev = urb->dev;
        int                     max = urb->transfer_buffer_length;
        struct completion       completion;
        int                     retval = 0;

        urb->context = &completion;
        while (retval == 0 && iterations-- > 0) {
                init_completion(&completion);
                if (usb_pipeout(urb->pipe))
                        simple_fill_buf(urb);
                retval = usb_submit_urb(urb, GFP_KERNEL);
                if (retval != 0)
                        break;

                /* NOTE:  no timeouts; can't be broken out of by interrupt */
                wait_for_completion(&completion);
                retval = urb->status;
                urb->dev = udev;
                if (retval == 0 && usb_pipein(urb->pipe))
                        retval = simple_check_buf(tdev, urb);

                if (vary) {
                        int     len = urb->transfer_buffer_length;

                        len += vary;
                        len %= max;
                        if (len == 0)
                                len = (vary < max) ? vary : max;
                        urb->transfer_buffer_length = len;
                }

                /* FIXME if endpoint halted, clear halt (and log) */
        }
        urb->transfer_buffer_length = max;

        if (expected != retval)
                dev_err(&udev->dev,
                        "%s failed, iterations left %d, status %d (not %d)\n",
                                label, iterations, retval, expected);
        return retval;
}

#endif
