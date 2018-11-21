#ifndef __HID_SIS_CTRL_H__
#define __HID_SIS_CTRL_H__

#define SIS817_DEVICE_NAME "sis_aegis_hid_touch_device"
#define SISF817_DEVICE_NAME "sis_aegis_hid_bridge_touch_device"

#define CTRL 0
#define ENDP_01 1
#define ENDP_02 2
#define DIR_IN 0x1

int sis_cdev_open(struct inode *inode, struct file *filp);
int sis_cdev_release(struct inode *inode, struct file *filp);
ssize_t sis_cdev_read(struct file *file, char __user *buf, size_t count, loff_t *ppos);
ssize_t sis_cdev_write( struct file *file, const char __user *buf, size_t count, loff_t *f_pos );
int sis_setup_chardev(struct hid_device *hdev);
void sis_deinit_chardev(void);

#endif	// __HID_SIS_CTRL_H__
