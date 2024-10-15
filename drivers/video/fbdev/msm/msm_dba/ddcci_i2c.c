#include <linux/module.h>
#include <linux/delay.h>

#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include "ddcci_i2c.h"
#include <linux/linkage.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <asm/unaligned.h>
#include <linux/uaccess.h>
#include <linux/crc-itu-t.h> /*For CRC*/

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>

#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/device.h>
#include <linux/gpio.h>


#define DEVICE_NAME "ddcci_i2c_device"
static const int ddcci_char_devs_count = 1;        /* device count */
static int ddcci_char_major;/* must 0 */
static struct cdev ddcci_char_cdev;
static struct class *ddcci_char_class;

/* Addresses to scan */
struct ddcci_data *ddcci_bak;/* must 0 */

struct deferred_ioctl {
    unsigned int cmd;
    uint8* arg;
    struct hlist_node deferred_work_node;
};

static int i2c_1_switch_gpio=-1;

static void ddcci_iodev_deferred_ioctl(struct deferred_ioctl *ioctl);

static int ddc_ci_write_cmd(uint32 dest_addr, uint32 src_addr,
uint32 content_len, const uint8 *content_buf, const char *what)
{
    int rc = 0;
    struct i2c_msg msg;
    uint8 checksum = 0;
    int i=0;

    u8 buffer[content_len+3];

    buffer[0]=src_addr;
    buffer[1]=0x80+content_len;
    for(i=0;i<content_len;i++)
        buffer[i+2]=content_buf[i];

    checksum=0x6E;
    buffer[content_len+2]=0;
    for (i = 0; i < content_len+3; i++) {
        checksum = checksum ^ buffer[i];
    }

    pr_debug("ddc_ci_write_cmd checksum is %x\n",checksum);
    buffer[content_len+2]=checksum;

    pr_debug("write data length is %d\n",content_len+3);
    for(i=0;i<content_len+3;i++)
        printk("write buffer is %x\n",buffer[i]);

    if (!ddcci_bak->client) {
        pr_err("%s: Invalid params\n", __func__);
        return -EINVAL;
    }
    pr_debug("%s: [%s:0x02%x] : W %d bytes\n", __func__,
         ddcci_bak->client->name, dest_addr, content_len);

    ddcci_bak->client->addr = dest_addr;

    msg.addr = dest_addr;
    msg.flags = 0;
    msg.len = content_len+3;
    msg.buf = buffer;

    if (i2c_transfer(ddcci_bak->client->adapter, &msg, 1) != 1) {
        pr_err("%s: i2c write failed\n", __func__);
        rc = -EIO;
    }

    return rc;
}

static int ddc_ci_read_cmd(uint32 dest_addr, uint32 src_addr,
uint32 content_len, uint8 *content_buf, const char *what)
{
    int rc = 0;
    struct i2c_msg msg[2];

    if (!ddcci_bak->client || !content_buf) {
        pr_err("%s: Invalid params\n", __func__);
        return -EINVAL;
    }

    ddcci_bak->client->addr = dest_addr;
    *content_buf=0x6f;
    //msg[0].addr = dest_addr;
    //msg[0].flags = 0;
    //msg[0].len = 1;
    //msg[0].buf = (uint8 *)&src_addr;

    msg[0].addr = dest_addr;
    msg[0].flags = I2C_M_RD;
    msg[0].len = content_len-1;
    msg[0].buf = content_buf+1;


    if (i2c_transfer(ddcci_bak->client->adapter, msg, 1) != 1) {
        pr_err("%s: i2c read failed\n", __func__);
        rc = -EIO;
    }

    pr_debug("%s: [%s:0x02%x] : R[0x%02x, 0x%02x]\n", __func__,
         ddcci_bak->client->name, dest_addr, content_len, *content_buf);
    return rc;
}

static ssize_t ddcci_cdev_write(struct file *file, const char __user *buf,
                                size_t count,
                                loff_t *f_pos)
{
    int ret = 0;
    pr_info("ddcci_cdev_write.\n");
    if (ddcci_bak == 0)
        return DDCCI_ERR_CLIENT;

    return ret;
}

/*for get system time*/
static ssize_t ddcci_cdev_read(struct file *file, char __user *buf,
                                size_t count,
                                loff_t *f_pos)
{
    int ret = 0;

    pr_info("ddcci_cdev_read.\n");
    if (ddcci_bak == 0)
        return DDCCI_ERR_CLIENT;

    return ret;
}

static int ddcci_cdev_open(struct inode *inode, struct file *filp)
{
    pr_debug("ddcci_cdev_open.\n");
    if (ddcci_bak == 0)
        return DDCCI_ERR_CLIENT;

    return 0; /* success */
}

static int ddcci_cdev_release(struct inode *inode, struct file *filp)
{
    pr_debug("ddcci_cdev_release.\\n");
    return 0;
}

int ddc_ci_get_vcp(uint8 vcp_code, uint8 *result_data)
{
    int Status = 0;

    uint8 vcp_data[2];
    uint32 dest_addr = 0x37;
    uint32 src_addr = 0x51;

    vcp_data[0] = DDC_CI_CMD_GET_VCP;
    vcp_data[1] = vcp_code;

    Status = ddc_ci_write_cmd(dest_addr, src_addr, 2, vcp_data, "GetVCP");
    msleep(110);
    Status |= ddc_ci_read_cmd(0x37,0x00,12,result_data,"VCPRely");

    return Status;
}

int ddc_ci_set_vcp(uint8 vcp_code, uint8 high_byte, uint8 low_byte)
{
    int Status = 0;

    uint8 vcp_data[4];
    uint32 dest_addr = 0x37;
    uint32 src_addr = 0x51;

    vcp_data[0] = DDC_CI_CMD_SET_VCP;
    vcp_data[1] = vcp_code;
    vcp_data[2] = high_byte;
    vcp_data[3] = low_byte;

    Status = ddc_ci_write_cmd(dest_addr, src_addr, 4, vcp_data, "SetVCP");

    /* according to ddcci standard, at least 50ms interval is necessary between two set commands */
    msleep(60);

    return Status;
}

int ddc_ci_capability_request(uint8 high_offset, uint8 low_offset, uint8 *result_data)
{
    int Status = 0;
    uint32 dest_addr = 0x37;
    uint32 src_addr = 0x51;
    uint8 cap_data[3];
    cap_data[0] = 0xF3;
    cap_data[1] = high_offset;//offset high byte
    cap_data[2] = low_offset;//offset low byte

    Status = ddc_ci_write_cmd(dest_addr, src_addr, 3, cap_data, "CapReq");
    msleep(200);
    Status |= ddc_ci_read_cmd(0x37,0x00,37,result_data,"CapRely");

    return Status;
}

int ddc_ci_timing_request(uint8 *result_data)
{
    int Status = 0;
    uint32 dest_addr = 0x37;
    uint32 src_addr = 0x51;
    uint8 timing_data = 0x07;

    Status = ddc_ci_write_cmd(dest_addr, src_addr, 1, &timing_data, "TimeReq");
    msleep(110);
    Status |= ddc_ci_read_cmd(0x37,0x00,9,result_data,"TimeRely");

    return Status;
}

int ddc_ci_vcp_reset(void)
{
    int Status = 0;
    uint32 dest_addr = 0x37;
    uint32 src_addr = 0x51;
    uint8 timing_data = 0x09;

    Status = ddc_ci_write_cmd(dest_addr, src_addr, 1, &timing_data, "VcpReset");

    return Status;
}

int ddc_ci_save_setting(void)
{
    int Status = 0;
    uint32 dest_addr = 0x37;
    uint32 src_addr = 0x51;
    uint8 timing_data = 0x0C;

    Status = ddc_ci_write_cmd(dest_addr, src_addr, 1, &timing_data, "SaveSet");

    return Status;
}

void ddcci_i2c_switch(u8 path)
{
    if (gpio_is_valid(i2c_1_switch_gpio)) {
        switch (path) {
        case I2C_SWITCH_TO_EDID:
            gpio_set_value(i2c_1_switch_gpio, 0);
            break;

        case I2C_SWITCH_TO_DDC:
        default:
            gpio_set_value(i2c_1_switch_gpio, 1);
            break;
        }
        msleep(5);
    }
}
EXPORT_SYMBOL(ddcci_i2c_switch);

static void ddcci_iodev_deferred_ioctl(struct deferred_ioctl *ioctl)
{
    pr_debug("+++++ %s ++++++\n", __func__);



    switch (ioctl->cmd) {
    case DDC_CI_CID_SET_VCP:
        if (ddc_ci_set_vcp(ioctl->arg[0], ioctl->arg[1], ioctl->arg[2]) < 0) {
            printk("DDC_CI_CID_SET_VCP, hdmi_msm_ddc_ci_set_vcp fail\n");
        }
        break;

    case DDC_CI_CID_VCP_RESET:
        if (ddc_ci_vcp_reset() < 0) {
            printk("DDC_CI_CID_VCP_RESET, hdmi_msm_ddc_ci_vcp_reset fail\n");
        }
        break;

    case DDC_CI_CID_SAVE_SETTING:
        if (ddc_ci_save_setting() < 0) {
            printk("DDC_CI_CID_SAVE_SETTING, hdmi_msm_ddc_ci_save_setting fail\n");
        }
        break;

    default:
        printk("%s, unknow cmd(%u)\n", __func__, ioctl->cmd);
        break;
    }
    kfree(ioctl->arg);
    kfree(ioctl);

    printk("----- %s ------\n", __func__);
}

static long ddcci_iodev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int ret;
    uint8 payload[4];
    uint8 *content_buf;
    void __user *argp = (void __user *)arg;
    struct deferred_ioctl *ioctl;

    switch (cmd) {
    case DDC_CI_CID_GET_VCP:
        content_buf = memdup_user(argp, 11);//11 bytes
        if (IS_ERR(content_buf)) {
            return PTR_ERR(content_buf);
        }

        payload[0] = content_buf[0];

        ret =ddc_ci_get_vcp(payload[0], content_buf);

        if (ret < 0) {
            return -EIO;
        }

        if (copy_to_user(argp, content_buf, 11)) {
            return -EFAULT;
        }

        kfree(content_buf);
        break;

    case DDC_CI_CID_SET_VCP:
        ioctl = kzalloc(sizeof(struct deferred_ioctl), GFP_KERNEL);
        ioctl->cmd = cmd;
        ioctl->arg = memdup_user(argp, 3);
        if (IS_ERR(ioctl->arg)) {
            printk("DDC_CI_CID_SET_VCP, memdup_user fail: %p\n", ioctl->arg);
            return PTR_ERR(ioctl->arg);
        }
        ddcci_iodev_deferred_ioctl(ioctl);
        break;

    case DDC_CI_CID_TIMING_REQ:
        content_buf = memdup_user(argp, 9);
        if (IS_ERR(content_buf)) {
            return PTR_ERR(content_buf);
        }

        ret = ddc_ci_timing_request(content_buf);

        if (ret < 0) {
            return -EIO;
        }

        if (copy_to_user(argp, content_buf, 9)) {
            return -EFAULT;
        }

        kfree(content_buf);
        break;

    case DDC_CI_CID_VCP_RESET:
        ret = ddc_ci_vcp_reset();
        if (ret < 0) {
            return -EIO;
        }
        break;

    case DDC_CI_CID_SAVE_SETTING:
        ret = ddc_ci_save_setting();
        if (ret < 0) {
            return -EIO;
        }
        break;

    case DDC_CI_CID_CAP_REQ:
        content_buf = memdup_user(argp, 37);
        if (IS_ERR(content_buf)) {
            return PTR_ERR(content_buf);
        }

        payload[0] = content_buf[0];    //high offset
        payload[1] = content_buf[1];    //low offset

        ret = ddc_ci_capability_request(payload[0], payload[1], content_buf);

        if (ret < 0) {
            return -EIO;
        }

        if (copy_to_user(argp, content_buf, 37)) {
            return -EFAULT;
        }

        kfree(content_buf);
        break;
    default:
        pr_err("%s: ERROR!! ddcci_iodev_ioctl(), Invalid argument!\n", __func__);
        return -EINVAL;
    }

    return 0;
}


static const struct file_operations ddcci_cdev_fops = {
    .owner  = THIS_MODULE,
    .read   = ddcci_cdev_read,
    .write  = ddcci_cdev_write,
    .open   = ddcci_cdev_open,
    .release    = ddcci_cdev_release,
    .unlocked_ioctl = ddcci_iodev_ioctl
};

static ssize_t ddcci_show_edid(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    ssize_t ret = 0;
    int i=0;

    uint8 edid[256]={0};

    //ddc_ci_write_cmd(0x50,0,0,0,"edid");

    ddc_ci_read_cmd(0x50,0,256,edid,"edid");

    for(i=0;i<32;i++)
    {

        pr_info("%x,%x,%x,%x,%x,%x,%x,%x\n",edid[i*8],edid[i*8+1],edid[i*8+2],edid[i*8+3],edid[i*8+4],edid[i*8+5],edid[i*8+6],edid[i*8+7]);
    }

    ret = snprintf(buf, PAGE_SIZE, "ddcci_show_edid\n");

    return ret;
}

static ssize_t ddcci_set_vcp(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    ssize_t ret = 0;

    int Status = 0;

    uint8 vcp_data[4];
    uint32 dest_addr = 0x37;
    uint32 src_addr = 0x51;

    vcp_data[0] = DDC_CI_CMD_SET_VCP;
    vcp_data[1] = 0x10;//set backlight
    vcp_data[2] = 0x00;
    vcp_data[3] = 0x60;

    //printk("%s, code: %x, high: %x, low: %x\n", __func__, vcp_code, high_byte, low_byte);
    Status = ddc_ci_write_cmd(dest_addr, src_addr, 4, vcp_data, "SetVCP");

    return ret;
}

static ssize_t ddcci_get_vcp(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    ssize_t ret = 0;

    int Status = 0;
    uint8 buffer[12]={0};


    uint8 vcp_data[2];
    uint32 dest_addr = 0x37;
    uint32 src_addr = 0x51;

    vcp_data[0] = DDC_CI_CMD_GET_VCP;
    vcp_data[1] = 0x10;//get backlight brightness

    Status = ddc_ci_write_cmd(dest_addr, src_addr, 2, vcp_data, "GetVCP");
    msleep(110);
    Status |= ddc_ci_read_cmd(0x37,0x00,12,buffer,"VCPRely");

    printk("ddcci_get_vcp,return data is %x,%x,%x,%x\n",buffer[0],buffer[1],buffer[2],buffer[3]);
    printk(" %x,%x,%x,%x\n",buffer[4],buffer[5],buffer[6],buffer[7]);
    printk(" %x,%x,%x,%x\n",buffer[8],buffer[9],buffer[10],buffer[11]);

    return ret;
}


static ssize_t ddcci_get_timing(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    ssize_t ret = 0;
    int Status = 0;
    uint8 buffer[8]={0};


    uint8 vcp_data=0x07;
    uint32 dest_addr = 0x37;
    uint32 src_addr = 0x51;

    Status = ddc_ci_write_cmd(dest_addr, src_addr, 1, &vcp_data, "GetTiming");
    msleep(50);
    Status |= ddc_ci_read_cmd(dest_addr,0x00,9,buffer,"TimingRely");

    printk("ddcci_get_timing,return data is %x,%x,%x,%x,%x,%x,%x,%x\n",buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7]);

    return ret;
}

static DEVICE_ATTR(edid, S_IRUGO, ddcci_show_edid, NULL);
static DEVICE_ATTR(set_vcp, S_IRUGO, ddcci_set_vcp, NULL);
static DEVICE_ATTR(get_vcp, S_IRUGO, ddcci_get_vcp, NULL);
static DEVICE_ATTR(get_timing, S_IRUGO, ddcci_get_timing, NULL);

static struct attribute *ddcci_fs_attrs[] = {
    &dev_attr_edid.attr,
    &dev_attr_set_vcp.attr,
    &dev_attr_get_vcp.attr,
    &dev_attr_get_timing.attr,
    NULL
};

static struct attribute_group ddcci_fs_attr_group = {
    .attrs = ddcci_fs_attrs
};

static int ddcci_setup_chardev(struct ddcci_data *ddcci)
{
    dev_t dev = MKDEV(ddcci_char_major, 0);
    int alloc_ret = 0;
    int cdev_err = 0;
    int input_err = 0;
    struct device *class_dev = NULL;
    void *ptr_err;
    int err = 0;
    pr_info("ddcci_setup_chardev.\n");
    if (ddcci == NULL) {
        input_err = -ENOMEM;
        goto error;
    }
    /* dynamic allocate driver handle */
    alloc_ret = alloc_chrdev_region(&dev, 0,
            ddcci_char_devs_count, DEVICE_NAME);
    if (alloc_ret)
        goto error;
    ddcci_char_major = MAJOR(dev);
    cdev_init(&ddcci_char_cdev, &ddcci_cdev_fops);
    ddcci_char_cdev.owner = THIS_MODULE;
    cdev_err = cdev_add(&ddcci_char_cdev,
        MKDEV(ddcci_char_major, 0), ddcci_char_devs_count);
    if (cdev_err)
        goto error;
    pr_info("%s driver(major %d) installed.\n",
            DEVICE_NAME, ddcci_char_major);
    /* register class */
    ddcci_char_class = class_create(THIS_MODULE, DEVICE_NAME);
    err = IS_ERR(ptr_err = ddcci_char_class);
    if (err)
        goto err2;
    class_dev = device_create(ddcci_char_class, NULL,
        MKDEV(ddcci_char_major, 0), NULL, DEVICE_NAME);
    err = IS_ERR(ptr_err = class_dev);
    if (err)
        goto err;

    err = sysfs_create_group(&class_dev->kobj,
            &ddcci_fs_attr_group);
    return 0;
error:
    if (cdev_err == 0)
        cdev_del(&ddcci_char_cdev);
    if (alloc_ret == 0)
        unregister_chrdev_region
                (MKDEV(ddcci_char_major, 0), ddcci_char_devs_count);
    if (input_err != 0)
        pr_err("ddcci_bak error!\n");
err:
    device_destroy(ddcci_char_class, MKDEV(ddcci_char_major, 0));
err2:
    class_destroy(ddcci_char_class);
    return DDCCI_ERR;
}

static int ddcci_i2c_probe(
    struct i2c_client *client, const struct i2c_device_id *id)
{
    int ret = 0;
    struct ddcci_data *ddcci_data = NULL;
    struct device_node *np = client->dev.of_node;

    pr_info("ddcci_i2c_probe\n");

    i2c_1_switch_gpio = of_get_named_gpio(np, "i2c_switch-gpio", 0);

    pr_info("i2c_switch-gpio is %d\n",i2c_1_switch_gpio);

    if (gpio_is_valid(i2c_1_switch_gpio)) {
        ret = devm_gpio_request_one(&client->dev, i2c_1_switch_gpio,
                        GPIOF_DIR_OUT, "i2c_1_switch_gpio");
        gpio_export(i2c_1_switch_gpio,true);
        if (ret < 0) {
            printk("could not acquire i2c_switch gpio (err=%d)\n", ret);
        }
    }
    ddcci_data = kzalloc(sizeof(struct ddcci_data), GFP_KERNEL);
    if (ddcci_data == NULL) {
        ret = -ENOMEM;
        goto err_alloc_data_failed;
    }
    ddcci_data->client=client;
    ddcci_bak=ddcci_data;


    ret = ddcci_setup_chardev(ddcci_data);
    if (ret)
        pr_err("ddcci_setup_chardev fail\n");

    pr_info("ddcci DDCCI I2C SLAVE ADDRESS: %d\n", DDCCI_SLAVE_ADDR);
    return 0;
err_alloc_data_failed:
    return ret;
}

static int ddcci_i2c_remove(struct i2c_client *client)
{

    return 0;
}

static const struct i2c_device_id ddcci_i2c_id[] = {
    { DDCCI_I2C_NAME, 0 },
    { }
};

MODULE_DEVICE_TABLE(i2c, ddcci_i2c_id);

static struct i2c_driver ddcci_i2c_driver = {
    .probe      = ddcci_i2c_probe,
    .remove     = ddcci_i2c_remove,
    .id_table   = ddcci_i2c_id,
    .driver = {
        .name   = DDCCI_I2C_NAME,
        .owner = THIS_MODULE,
    },
};

static int __init ddcci_i2c_init(void)
{
    pr_info("ddcci_i2c_init\n");
    return i2c_add_driver(&ddcci_i2c_driver);
}

static void __exit ddcci_i2c_exit(void)
{
    dev_t dev;

    pr_info("ddcci_i2c_exit\n");
    i2c_del_driver(&ddcci_i2c_driver);


    dev = MKDEV(ddcci_char_major, 0);
    cdev_del(&ddcci_char_cdev);
    unregister_chrdev_region(dev, ddcci_char_devs_count);
    device_destroy(ddcci_char_class, MKDEV(ddcci_char_major, 0));
    class_destroy(ddcci_char_class);

}

module_init(ddcci_i2c_init);
module_exit(ddcci_i2c_exit);
MODULE_DESCRIPTION("I2C Driver For DDCCI");
MODULE_LICENSE("GPL");

