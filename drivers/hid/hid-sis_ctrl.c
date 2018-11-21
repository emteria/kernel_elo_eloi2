/*
 *  Character device driver for SIS multitouch panels control
 *
 *  Copyright (c) 2009 SIS, Ltd.
 *
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/hid.h>
#include <linux/module.h>
#include <linux/usb.h>
#include "usbhid/usbhid.h"
#include <linux/init.h>

//update FW
#include <linux/fs.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>	//copy_from_user() & copy_to_user()

#include "hid-ids.h"
#include "hid-sis_ctrl.h"

static int sis_char_devs_count = 1;        /* device count */
static int sis_char_major = 0;
static struct cdev sis_char_cdev;
static struct class *sis_char_class = NULL;

static struct hid_device *hid_dev_backup = NULL;  //backup address
static struct urb *backup_urb = NULL;

#define DEBUG_HID_SIS_UPDATE_FW 0

#if DEBUG_HID_SIS_UPDATE_FW
	#define DBG_FW(fmt, arg...)	printk( fmt, ##arg )
	void sis_dbg_dump_array( u8 *ptr, u32 size)
	{
		u32 i;
		for (i=0; i<size; i++)
		{
			DBG_FW ("%02X ", ptr[i]);
			if( ((i+1)&0xF) == 0)
				DBG_FW ("\n");
		}
		if( size & 0xF)
			DBG_FW ("\n");
	}
#else
	#define DBG_FW(...)
	#define sis_dbg_dump_array(...)
#endif	// DEBUG_HID_SIS_UPDATE_FW


int sis_cdev_open(struct inode *inode, struct file *filp)	//20120306 Yuger ioctl for tool
{
	struct usbhid_device *usbhid;

	DBG_FW( "%s\n" , __FUNCTION__ );
	//20110511, Yuger, kill current urb by method of usbhid_stop
	if ( !hid_dev_backup )
	{
		printk( KERN_INFO "(stop)hid_dev_backup is not initialized yet" );
		return -1;
	}

	usbhid = hid_dev_backup->driver_data;

	printk( KERN_INFO "sys_sis_HID_stop\n" );

	//printk( KERN_INFO "hid_dev_backup->vendor, hid_dev_backup->product = %x %x\n", hid_dev_backup->vendor, hid_dev_backup->product );

	//20110602, Yuger, fix bug: not contact usb cause kernel panic
	if( !usbhid )
	{
		printk( KERN_INFO "(stop)usbhid is not initialized yet" );
		return -1;
	}
	else if ( !usbhid->urbin )
	{
		printk( KERN_INFO "(stop)usbhid->urbin is not initialized yet" );
		return -1;
	}
	else if (hid_dev_backup->vendor == USB_VENDOR_ID_SIS_TOUCH)
	{
		usb_fill_int_urb(backup_urb, usbhid->urbin->dev, usbhid->urbin->pipe,
			usbhid->urbin->transfer_buffer, usbhid->urbin->transfer_buffer_length,
			usbhid->urbin->complete, usbhid->urbin->context, usbhid->urbin->interval);

                clear_bit( HID_STARTED, &usbhid->iofl );
                set_bit( HID_DISCONNECTED, &usbhid->iofl );

                usb_kill_urb( usbhid->urbin );
                usb_free_urb( usbhid->urbin );
                usbhid->urbin = NULL;
		return 0;
	}
    else
	{
		printk (KERN_INFO "This is not a SiS device");
		return -801;
	}
}

int sis_cdev_release(struct inode *inode, struct file *filp)
{
	//20110505, Yuger, rebuild the urb which is at the same urb address, then re-submit it

	int ret;
	struct usbhid_device *usbhid;
	unsigned long flags;

	DBG_FW( "%s: " , __FUNCTION__ );

	if ( !hid_dev_backup )
	{
		printk( KERN_INFO "(stop)hid_dev_backup is not initialized yet" );
		return -1;
	}

	usbhid = hid_dev_backup->driver_data;

	printk( KERN_INFO "sys_sis_HID_start" );

	if( !usbhid )
	{
		printk( KERN_INFO "(start)usbhid is not initialized yet" );
		return -1;
	}

	if( !backup_urb )
	{
		printk( KERN_INFO "(start)backup_urb is not initialized yet" );
		return -1;
	}

	clear_bit( HID_DISCONNECTED, &usbhid->iofl );
	usbhid->urbin = usb_alloc_urb( 0, GFP_KERNEL );

	if( !backup_urb->interval )
	{
		printk( KERN_INFO "(start)backup_urb->interval does not exist" );
		return -1;
	}

	usb_fill_int_urb(usbhid->urbin, backup_urb->dev, backup_urb->pipe,
		backup_urb->transfer_buffer, backup_urb->transfer_buffer_length,
		backup_urb->complete, backup_urb->context, backup_urb->interval);
	usbhid->urbin->transfer_dma = usbhid->inbuf_dma;
	usbhid->urbin->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	set_bit( HID_STARTED, &usbhid->iofl );

	//method at hid_start_in
	spin_lock_irqsave( &usbhid->lock, flags );
	ret = usb_submit_urb( usbhid->urbin, GFP_ATOMIC );
	spin_unlock_irqrestore( &usbhid->lock, flags );
	//yy

	DBG_FW( "ret = %d", ret );

	return ret;
}

//SiS 817 only
ssize_t sis_cdev_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	int actual_length = 0, timeout = 0;
	u8 *rep_data = NULL;
	u16 size = 0;
	long rep_ret;
	struct usb_interface *intf = to_usb_interface(hid_dev_backup->dev.parent);
	struct usb_device *dev = interface_to_usbdev(intf);

	DBG_FW( "%s\n" , __FUNCTION__ );

	size = (((u16)(buf[64] & 0xff)) << 24) + (((u16)(buf[65] & 0xff)) << 16) +
		(((u16)(buf[66] & 0xff)) << 8) + (u16)(buf[67] & 0xff);
	timeout = (((int)(buf[68] & 0xff)) << 24) + (((int)(buf[69] & 0xff)) << 16) +
		(((int)(buf[70] & 0xff)) << 8) + (int)(buf[71] & 0xff);

	rep_data = kzalloc(size, GFP_KERNEL);
	if (!rep_data)
		return -12;

	if ( copy_from_user( rep_data, (void*)buf, size) )
	{
		printk( KERN_INFO "copy_to_user fail\n" );
		//free allocated data
		kfree( rep_data );
		rep_data = NULL;
		return -19;
	}

	rep_ret = usb_interrupt_msg(dev, backup_urb->pipe,
		rep_data, size, &actual_length, timeout);

	DBG_FW( "%s: rep_data = ", __FUNCTION__);
	sis_dbg_dump_array( rep_data, 8);

	if( rep_ret == 0 )
	{
		rep_ret = actual_length;
		if ( copy_to_user( (void*)buf, rep_data, rep_ret ) )
		{
			printk( KERN_INFO "copy_to_user fail\n" );
			//free allocated data
			kfree( rep_data );
			rep_data = NULL;
			return -19;
		}
	}

	//free allocated data
	kfree( rep_data );
	rep_data = NULL;
	DBG_FW( "%s: rep_ret = %ld\n", __FUNCTION__,rep_ret );
	return rep_ret;
}

ssize_t sis_cdev_write( struct file *file, const char __user *buf, size_t count, loff_t *f_pos )
{
	int actual_length = 0;
	u8 *rep_data = NULL;
	long rep_ret;
	struct usb_interface *intf = to_usb_interface( hid_dev_backup->dev.parent );
	struct usb_device *dev = interface_to_usbdev( intf );
	struct usbhid_device *usbhid = hid_dev_backup->driver_data;

	u16 size = (((u16)(buf[64] & 0xff)) << 24) + (((u16)(buf[65] & 0xff)) << 16) +
		(((u16)(buf[66] & 0xff)) << 8) + (u16)(buf[67] & 0xff);
	int timeout = (((int)(buf[68] & 0xff)) << 24) + (((int)(buf[69] & 0xff)) << 16) +
		(((int)(buf[70] & 0xff)) << 8) + (int)(buf[71] & 0xff);

	DBG_FW( "%s: 817 method, " , __FUNCTION__ );
	DBG_FW("timeout = %d, size %d\n", timeout, size);

	rep_data = kzalloc(size, GFP_KERNEL);
	if (!rep_data)
		return -12;

	if ( copy_from_user( rep_data, (void*)buf, size) )
	{
		printk( KERN_INFO "copy_to_user fail\n" );
		//free allocated data
		kfree( rep_data );
		rep_data = NULL;
		return -19;
	}

	rep_ret = usb_interrupt_msg( dev, usbhid->urbout->pipe,
		rep_data, size, &actual_length, timeout );

	DBG_FW( "%s: rep_data = ", __FUNCTION__);
	sis_dbg_dump_array( rep_data, size);

	if( rep_ret == 0 )
	{
		rep_ret = actual_length;
		if ( copy_to_user( (void*)buf, rep_data, rep_ret ) )
		{
			printk( KERN_INFO "copy_to_user fail\n" );
			//free allocated data
			kfree( rep_data );
			rep_data = NULL;
			return -19;
		}
	}
	DBG_FW( "%s: rep_ret = %ld\n", __FUNCTION__,rep_ret );

	//free allocated data
	kfree( rep_data );
	rep_data = NULL;

	DBG_FW( "End of sys_sis_HID_IO\n" );
	return rep_ret;
}


//for ioctl
static const struct file_operations sis_cdev_fops = {
	.owner	= THIS_MODULE,
	.read	= sis_cdev_read,
	.write	= sis_cdev_write,
	.open	= sis_cdev_open,
	.release= sis_cdev_release,
};

//for ioctl
int sis_setup_chardev(struct hid_device *hdev)
{

	dev_t dev = MKDEV(sis_char_major, 0);
	int alloc_ret = 0;
	int cdev_err = 0;
	int input_err = 0;
	struct device *class_dev = NULL;
	void *ptr_err;

	printk("sis_setup_chardev.\n");
	hid_dev_backup = hdev;

	backup_urb = usb_alloc_urb(0, GFP_KERNEL); //0721test
	if (!backup_urb) {
		dev_err(&hdev->dev, "cannot allocate backup_urb\n");
		return -ENOMEM;
	}
/*
	if (nd == NULL)
	{
          input_err = -ENOMEM;
          goto error;
	}
*/
	// dynamic allocate driver handle
	if (hdev->product == USB_DEVICE_ID_SISF817_TOUCH)
		alloc_ret = alloc_chrdev_region(&dev, 0, sis_char_devs_count, SISF817_DEVICE_NAME);
	else
		alloc_ret = alloc_chrdev_region(&dev, 0, sis_char_devs_count, SIS817_DEVICE_NAME);

	if (alloc_ret)
		goto error;

	sis_char_major  = MAJOR(dev);
	cdev_init(&sis_char_cdev, &sis_cdev_fops);
	sis_char_cdev.owner = THIS_MODULE;
	cdev_err = cdev_add(&sis_char_cdev, MKDEV(sis_char_major, 0), sis_char_devs_count);
	if (cdev_err)
		goto error;

	if (hdev->product == USB_DEVICE_ID_SISF817_TOUCH)
		printk(KERN_INFO "%s driver(major %d) installed.\n", SISF817_DEVICE_NAME, sis_char_major);
	else
		printk(KERN_INFO "%s driver(major %d) installed.\n", SIS817_DEVICE_NAME, sis_char_major);

	// register class
	if (hdev->product == USB_DEVICE_ID_SISF817_TOUCH)
		sis_char_class = class_create(THIS_MODULE, SISF817_DEVICE_NAME);
	else
		sis_char_class = class_create(THIS_MODULE, SIS817_DEVICE_NAME);

	if(IS_ERR(ptr_err = sis_char_class))
	{
		goto err2;
	}

	if (hdev->product == USB_DEVICE_ID_SISF817_TOUCH)
		class_dev = device_create(sis_char_class, NULL, MKDEV(sis_char_major, 0), NULL, SISF817_DEVICE_NAME);
	else
		class_dev = device_create(sis_char_class, NULL, MKDEV(sis_char_major, 0), NULL, SIS817_DEVICE_NAME);

	if(IS_ERR(ptr_err = class_dev))
	{
		goto err;
	}

	return 0;
error:
	if (cdev_err == 0)
		cdev_del(&sis_char_cdev);
	if (alloc_ret == 0)
		unregister_chrdev_region(MKDEV(sis_char_major, 0), sis_char_devs_count);
	if(input_err != 0)
	{
		printk("sis_ts_bak error!\n");
	}
err:
	device_destroy(sis_char_class, MKDEV(sis_char_major, 0));
err2:
	class_destroy(sis_char_class);
	return -1;
}
EXPORT_SYMBOL(sis_setup_chardev);

void sis_deinit_chardev(void)
{
	//for ioctl
	dev_t dev;
	printk(KERN_INFO "sis_remove\n");

	dev = MKDEV(sis_char_major, 0);
	cdev_del(&sis_char_cdev);
	unregister_chrdev_region(dev, sis_char_devs_count);
	device_destroy(sis_char_class, MKDEV(sis_char_major, 0));
	class_destroy(sis_char_class);
	usb_kill_urb( backup_urb );
	usb_free_urb( backup_urb );
	backup_urb = NULL;
	hid_dev_backup = NULL;

}
EXPORT_SYMBOL(sis_deinit_chardev);

MODULE_DESCRIPTION("SiS 817 Touchscreen Control Driver");
MODULE_LICENSE("GPL");
