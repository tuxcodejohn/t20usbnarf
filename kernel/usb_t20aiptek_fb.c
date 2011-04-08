

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
//	int			open_count;		/* count the number of openers */
//	bool			ongoing_read;		/* a read is going on */
	bool			processed_urb;		/* indicates we haven't processed the urb */
	spinlock_t		err_lock;		/* lock for errors */
	struct kref		kref;
	struct mutex		io_mutex;		/* synchronize I/O with disconnect */
	struct completion	bulk_in_completion;	/* to wait for an ongoing read */
	/*framebufferstuff follows :*/
	struct fb_info		info;			/* frame buffer info */
	void			*fb_virt;	/* virt. address of the frame buffer */
	dma_addr_t		fb_phys;	/* phys. address of the frame buffer */
	int			fb_alloced;	/* Flag, was the fb memory alloced? */

	
	u32		pseudo_palette[PALETTE_ENTRIES_NO];
					/* Fake palette of 16 colors */

};
#define to_t20aiptek_dev(d) container_of(d, struct usb_t20aiptek, kref)
#define fbinfo_to_t20aiptek_dev(d) container_of(d, struct usb_t20aiptek, info)

static struct usb_driver t20aiptek_driver;
static void t20aiptek_draw_down(struct usb_t20aiptek *dev);

static void t20aiptek_delete(struct kref *kref)
{
	struct usb_t20aiptek *dev = to_t20aiptek_dev(kref);

	usb_free_urb(dev->bulk_in_urb);
	usb_put_dev(dev->udev);
	kfree(dev->bulk_in_buffer);
	kfree(dev);
}

static int t20aiptek_open(struct inode *inode, struct file *file)
{
	struct usb_t20aiptek *dev;
	struct usb_interface *interface;
	int subminor;
	int retval = 0;

	subminor = iminor(inode);

	interface = usb_find_interface(&t20aiptek_driver, subminor);
	if (!interface) {
		err("%s - error, can't find device for minor %d",
		     __func__, subminor);
		retval = -ENODEV;
		goto exit;
	}

	dev = usb_get_intfdata(interface);
	if (!dev) {
		retval = -ENODEV;
		goto exit;
	}

	/* increment our usage count for the device */
	kref_get(&dev->kref);

	/* lock the device to allow correctly handling errors
	 * in resumption */
	mutex_lock(&dev->io_mutex);

	if (!dev->open_count++) {
		retval = usb_autopm_get_interface(interface);
			if (retval) {
				dev->open_count--;
				mutex_unlock(&dev->io_mutex);
				kref_put(&dev->kref, t20aiptek_delete);
				goto exit;
			}
	} /* else { //uncomment this block if you want exclusive open
		retval = -EBUSY;
		dev->open_count--;
		mutex_unlock(&dev->io_mutex);
		kref_put(&dev->kref, t20aiptek_delete);
		goto exit;
	} */
	/* prevent the device from being autosuspended */

	/* save our object in the file's private structure */
	file->private_data = dev;
	mutex_unlock(&dev->io_mutex);

exit:
	return retval;
}

static int t20aiptek_release(struct inode *inode, struct file *file)
{
	struct usb_t20aiptek *dev;

	dev = file->private_data;
	if (dev == NULL)
		return -ENODEV;

	/* allow the device to be autosuspended */
	mutex_lock(&dev->io_mutex);
	if (!--dev->open_count && dev->interface)
		usb_autopm_put_interface(dev->interface);
	mutex_unlock(&dev->io_mutex);

	/* decrement the count on our device */
	kref_put(&dev->kref, t20aiptek_delete);
	return 0;
}

static int t20aiptek_flush(struct file *file, fl_owner_t id)
{
	struct usb_t20aiptek *dev;
	int res;

	dev = file->private_data;
	if (dev == NULL)
		return -ENODEV;

	/* wait for io to stop */
	mutex_lock(&dev->io_mutex);
	t20aiptek_draw_down(dev);

	/* read out errors, leave subsequent opens a clean slate */
	spin_lock_irq(&dev->err_lock);
	res = dev->errors ? (dev->errors == -EPIPE ? -EPIPE : -EIO) : 0;
	dev->errors = 0;
	spin_unlock_irq(&dev->err_lock);

	mutex_unlock(&dev->io_mutex);

	return res;
}

static void t20aiptek_read_bulk_callback(struct urb *urb)
{
	struct usb_t20aiptek *dev;

	dev = urb->context;

	spin_lock(&dev->err_lock);
	/* sync/async unlink faults aren't errors */
	if (urb->status) {
		if (!(urb->status == -ENOENT ||
		    urb->status == -ECONNRESET ||
		    urb->status == -ESHUTDOWN))
			err("%s - nonzero write bulk status received: %d",
			    __func__, urb->status);

		dev->errors = urb->status;
	} else {
		dev->bulk_in_filled = urb->actual_length;
	}
	dev->ongoing_read = 0;
	spin_unlock(&dev->err_lock);

	complete(&dev->bulk_in_completion);
}

static int t20aiptek_do_read_io(struct usb_t20aiptek *dev, size_t count)
{
	int rv;

	/* prepare a read */
	usb_fill_bulk_urb(dev->bulk_in_urb,
			dev->udev,
			usb_rcvbulkpipe(dev->udev,
				dev->bulk_in_endpointAddr),
			dev->bulk_in_buffer,
			min(dev->bulk_in_size, count),
			t20aiptek_read_bulk_callback,
			dev);
	/* tell everybody to leave the URB alone */
	spin_lock_irq(&dev->err_lock);
	dev->ongoing_read = 1;
	spin_unlock_irq(&dev->err_lock);

	/* do it */
	rv = usb_submit_urb(dev->bulk_in_urb, GFP_KERNEL);
	if (rv < 0) {
		err("%s - failed submitting read urb, error %d",
			__func__, rv);
		dev->bulk_in_filled = 0;
		rv = (rv == -ENOMEM) ? rv : -EIO;
		spin_lock_irq(&dev->err_lock);
		dev->ongoing_read = 0;
		spin_unlock_irq(&dev->err_lock);
	}

	return rv;
}

static ssize_t t20aiptek_read(struct file *file, char *buffer, size_t count,
			 loff_t *ppos)
{
	struct usb_t20aiptek *dev;
	int rv;
	bool ongoing_io;

	dev = file->private_data;

	/* if we cannot read at all, return EOF */
	if (!dev->bulk_in_urb || !count)
		return 0;

	/* no concurrent readers */
	rv = mutex_lock_interruptible(&dev->io_mutex);
	if (rv < 0)
		return rv;

	if (!dev->interface) {		/* disconnect() was called */
		rv = -ENODEV;
		goto exit;
	}

	/* if IO is under way, we must not touch things */
retry:
	spin_lock_irq(&dev->err_lock);
	ongoing_io = dev->ongoing_read;
	spin_unlock_irq(&dev->err_lock);

	if (ongoing_io) {
		/* nonblocking IO shall not wait */
		if (file->f_flags & O_NONBLOCK) {
			rv = -EAGAIN;
			goto exit;
		}
		/*
		 * IO may take forever
		 * hence wait in an interruptible state
		 */
		rv = wait_for_completion_interruptible(&dev->bulk_in_completion);
		if (rv < 0)
			goto exit;
		/*
		 * by waiting we also semiprocessed the urb
		 * we must finish now
		 */
		dev->bulk_in_copied = 0;
		dev->processed_urb = 1;
	}

	if (!dev->processed_urb) {
		/*
		 * the URB hasn't been processed
		 * do it now
		 */
		wait_for_completion(&dev->bulk_in_completion);
		dev->bulk_in_copied = 0;
		dev->processed_urb = 1;
	}

	/* errors must be reported */
	rv = dev->errors;
	if (rv < 0) {
		/* any error is reported once */
		dev->errors = 0;
		/* to preserve notifications about reset */
		rv = (rv == -EPIPE) ? rv : -EIO;
		/* no data to deliver */
		dev->bulk_in_filled = 0;
		/* report it */
		goto exit;
	}

	/*
	 * if the buffer is filled we may satisfy the read
	 * else we need to start IO
	 */

	if (dev->bulk_in_filled) {
		/* we had read data */
		size_t available = dev->bulk_in_filled - dev->bulk_in_copied;
		size_t chunk = min(available, count);

		if (!available) {
			/*
			 * all data has been used
			 * actual IO needs to be done
			 */
			rv = t20aiptek_do_read_io(dev, count);
			if (rv < 0)
				goto exit;
			else
				goto retry;
		}
		/*
		 * data is available
		 * chunk tells us how much shall be copied
		 */

		if (copy_to_user(buffer,
				 dev->bulk_in_buffer + dev->bulk_in_copied,
				 chunk))
			rv = -EFAULT;
		else
			rv = chunk;

		dev->bulk_in_copied += chunk;

		/*
		 * if we are asked for more than we have,
		 * we start IO but don't wait
		 */
		if (available < count)
			t20aiptek_do_read_io(dev, count - chunk);
	} else {
		/* no data in the buffer */
		rv = t20aiptek_do_read_io(dev, count);
		if (rv < 0)
			goto exit;
		else if (!(file->f_flags & O_NONBLOCK))
			goto retry;
		rv = -EAGAIN;
	}
exit:
	mutex_unlock(&dev->io_mutex);
	return rv;
}

static void t20aiptek_write_bulk_callback(struct urb *urb)
{
	struct usb_t20aiptek *dev;

	dev = urb->context;

	/* sync/async unlink faults aren't errors */
	if (urb->status) {
		if (!(urb->status == -ENOENT ||
		    urb->status == -ECONNRESET ||
		    urb->status == -ESHUTDOWN))
			err("%s - nonzero write bulk status received: %d",
			    __func__, urb->status);

		spin_lock(&dev->err_lock);
		dev->errors = urb->status;
		spin_unlock(&dev->err_lock);
	}

	/* free up our allocated buffer */
	usb_free_coherent(urb->dev, urb->transfer_buffer_length,
			  urb->transfer_buffer, urb->transfer_dma);
	up(&dev->limit_sem);
}

static ssize_t t20aiptek_write(struct file *file, const char *user_buffer,
			  size_t count, loff_t *ppos)
{
	struct usb_t20aiptek *dev;
	int retval = 0;
	struct urb *urb = NULL;
	char *buf = NULL;
	size_t writesize = min(count, (size_t)MAX_TRANSFER);

	dev = file->private_data;

	/* verify that we actually have some data to write */
	if (count == 0)
		goto exit;

	/*
	 * limit the number of URBs in flight to stop a user from using up all
	 * RAM
	 */
	if (!(file->f_flags & O_NONBLOCK)) {
		if (down_interruptible(&dev->limit_sem)) {
			retval = -ERESTARTSYS;
			goto exit;
		}
	} else {
		if (down_trylock(&dev->limit_sem)) {
			retval = -EAGAIN;
			goto exit;
		}
	}

	spin_lock_irq(&dev->err_lock);
	retval = dev->errors;
	if (retval < 0) {
		/* any error is reported once */
		dev->errors = 0;
		/* to preserve notifications about reset */
		retval = (retval == -EPIPE) ? retval : -EIO;
	}
	spin_unlock_irq(&dev->err_lock);
	if (retval < 0)
		goto error;

	/* create a urb, and a buffer for it, and copy the data to the urb */
	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb) {
		retval = -ENOMEM;
		goto error;
	}

	buf = usb_alloc_coherent(dev->udev, writesize, GFP_KERNEL,
				 &urb->transfer_dma);
	if (!buf) {
		retval = -ENOMEM;
		goto error;
	}

	if (copy_from_user(buf, user_buffer, writesize)) {
		retval = -EFAULT;
		goto error;
	}

	/* this lock makes sure we don't submit URBs to gone devices */
	mutex_lock(&dev->io_mutex);
	if (!dev->interface) {		/* disconnect() was called */
		mutex_unlock(&dev->io_mutex);
		retval = -ENODEV;
		goto error;
	}

	/* initialize the urb properly */
	usb_fill_bulk_urb(urb, dev->udev,
			  usb_sndbulkpipe(dev->udev, dev->bulk_out_endpointAddr),
			  buf, writesize, t20aiptek_write_bulk_callback, dev);
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	usb_anchor_urb(urb, &dev->submitted);

	/* send the data out the bulk port */
	retval = usb_submit_urb(urb, GFP_KERNEL);
	mutex_unlock(&dev->io_mutex);
	if (retval) {
		err("%s - failed submitting write urb, error %d", __func__,
		    retval);
		goto error_unanchor;
	}

	/*
	 * release our reference to this urb, the USB core will eventually free
	 * it entirely
	 */
	usb_free_urb(urb);


	return writesize;

error_unanchor:
	usb_unanchor_urb(urb);
error:
	if (urb) {
		usb_free_coherent(dev->udev, writesize, buf, urb->transfer_dma);
		usb_free_urb(urb);
	}
	up(&dev->limit_sem);

exit:
	return retval;
}

static const struct file_operations t20aiptek_fops = {
	.owner =	THIS_MODULE,
	.read =		t20aiptek_read,
	.write =	t20aiptek_write,
	.open =		t20aiptek_open,
	.release =	t20aiptek_release,
	.flush =	t20aiptek_flush,
	.llseek =	noop_llseek,
};

/*
 * usb class driver info in order to get a minor number from the usb core,
 * and to have the device registered with the driver core
 */
static struct usb_class_driver t20aiptek_class = {
	.name =		"t20aiptek%d",
	.fops =		&t20aiptek_fops,
	.minor_base =	USB_T20AIPTEK_MINOR_BASE,
};

static int t20aiptek_probe(struct usb_interface *interface,
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
	
	/* we can register the device now, as it is ready */
	retval = usb_register_dev(interface, &t20aiptek_class);
	if (retval) {
		/* something prevented us from registering this driver */
		err("Not able to get a minor for this device.");
		usb_set_intfdata(interface, NULL);
		goto error;
	}

	/* let the user know what node this device is now attached to */
	dev_info(&interface->dev,
		 "USB Skeleton device now attached to USBSkel-%d",
		 interface->minor);
	return 0;

error:
	if (dev)
		/* this frees allocated memory */
		kref_put(&dev->kref, t20aiptek_delete);
	return retval;
}

static struct fb_ops t20aiptekfb_ops =
{
	.owner			= THIS_MODULE,
	.fb_setcolreg		= t20aiptekfb_setcolreg,
	.fb_blank		= t20aiptekfb_blank,
	.fb_fillrect		= cfb_fillrect,
	.fb_copyarea		= cfb_copyarea,
	.fb_imageblit		= cfb_imageblit,
};

static void t20aiptek_fb_init(struct usb_t20aiptek * drvdata)
{
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
	


static void t20aiptek_disconnect(struct usb_interface *interface)
{
	struct usb_t20aiptek *dev;
	int minor = interface->minor;

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
	kref_put(&dev->kref, t20aiptek_delete);

	dev_info(&interface->dev, "USB Skeleton #%d now disconnected", minor);
}

static void t20aiptek_draw_down(struct usb_t20aiptek *dev)
{
	int time;

	time = usb_wait_anchor_empty_timeout(&dev->submitted, 1000);
	if (!time)
		usb_kill_anchored_urbs(&dev->submitted);
	usb_kill_urb(dev->bulk_in_urb);
}

static int t20aiptek_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct usb_t20aiptek *dev = usb_get_intfdata(intf);

	if (!dev)
		return 0;
	t20aiptek_draw_down(dev);
	return 0;
}

static int t20aiptek_resume(struct usb_interface *intf)
{
	return 0;
}

static int t20aiptek_pre_reset(struct usb_interface *intf)
{
	struct usb_t20aiptek *dev = usb_get_intfdata(intf);

	mutex_lock(&dev->io_mutex);
	t20aiptek_draw_down(dev);

	return 0;
}

static int t20aiptek_post_reset(struct usb_interface *intf)
{
	struct usb_t20aiptek *dev = usb_get_intfdata(intf);

	/* we are sure no URBs are active - no locking needed */
	dev->errors = -EPIPE;
	mutex_unlock(&dev->io_mutex);

	return 0;
}

static struct usb_driver t20aiptek_driver = {
	.name =		"t20aiptek",
	.probe =	t20aiptek_probe,
	.disconnect =	t20aiptek_disconnect,
	.suspend =	t20aiptek_suspend, //Maybe we cant
	.resume =	t20aiptek_resume,  //do this ?? -> more reversing needed
	.pre_reset =	t20aiptek_pre_reset,
	.post_reset =	t20aiptek_post_reset,
	.id_table =	t20aiptek_table,
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
