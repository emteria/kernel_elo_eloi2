#include <linux/bcd.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/of_device.h>
#include <linux/delay.h>
#include <linux/debugfs.h>

static const struct i2c_device_id usb2517_id[] = {
	{ "usb2517", 0},
	{ "usb2517_ext", 1},
	{ }
};
MODULE_DEVICE_TABLE(i2c, usb2517_id);

/*2017-10-16 Jack W Lu: Add PWRUSB boost mode control for PP2.0 {*/
static unsigned char ex_usb2517_pwrusb_boost_mode = 0x0;
static unsigned char input_boost_mode = 0x0FF;
/*2017-10-16 Jack W Lu: Add PWRUSB boost mode control for PP2.0 }*/

struct usb2517_data {
	struct i2c_client *client;
	struct dentry *debugfs;
	u8 features;//internal HUB == 0, external HUB == 1
	int usb2517_chip_reset;/*for reset: LOW enable, APQ GPIOxx*/
	int usb2517_port_ctl_data;
};

static struct usb2517_data usb2517_internal_hub_data = {
	.features = 0,
	.usb2517_chip_reset = -1,
	.usb2517_port_ctl_data = 0,
};
static struct usb2517_data usb2517_external_hub_data = {
	.features = 1,
	.usb2517_chip_reset = -1,
	.usb2517_port_ctl_data = 0,
};

/*2017-6-10 Jack W Lu: add interface for ttyUSB, get dynamic port number {*/
char ex_hub_p_0dis_cfd[] = "1.2.5.6";
char ex_hub_p_1dis_cfd[] = "1.2.5.5";
char ex_hub_p_2dis_cfd[] = "1.2.5.4";
char ex_hub_p_3dis_cfd[] = "1.2.5.3";
//IN HUB2.0 port4 disabled
char ex_hub_4dp_0dis_cfd[] = "1.2.4.6";
char ex_hub_4dp_1dis_cfd[] = "1.2.4.5";
char ex_hub_4dp_2dis_cfd[] = "1.2.4.4";
char ex_hub_4dp_3dis_cfd[] = "1.2.4.3";
char * exhub_get_cfd_port_number(void)
{
	int port_dis_num = 0;
	u8 bitmask = 1;
	int loop = 0;
	u8 ctl_data = (u8)usb2517_external_hub_data.usb2517_port_ctl_data;
	u8 inctl_data = (u8)usb2517_internal_hub_data.usb2517_port_ctl_data;
	if(usb2517_external_hub_data.usb2517_chip_reset < 0)
	{
		printk("no ex hub in this proj\n");
		return NULL;
	}

	for(loop=0;loop < 8;loop++)
	{
		if((ctl_data&(bitmask<<loop)) != 0)
			port_dis_num++;
	}
	printk("exhub_get_port_number port_dis_num = %d\n", port_dis_num);

	switch(port_dis_num)
	{
		case 0:
			if((inctl_data&(0x10))!=0)//in hub2.0 port 4 is disabled
				return ex_hub_4dp_0dis_cfd;
			else
				return ex_hub_p_0dis_cfd;
		case 1:
			if((inctl_data&(0x10))!=0)//in hub2.0 port 4 is disabled
				return ex_hub_4dp_1dis_cfd;
			else
				return ex_hub_p_1dis_cfd;
		case 2:
			if((inctl_data&(0x10))!=0)//in hub2.0 port 4 is disabled
				return ex_hub_4dp_2dis_cfd;
			else
				return ex_hub_p_2dis_cfd;
		case 3:
			if((inctl_data&(0x10))!=0)//in hub2.0 port 4 is disabled
				return ex_hub_4dp_3dis_cfd;
			else
				return ex_hub_p_3dis_cfd;
		default:
			printk("INVALID number\n");
			return NULL;
	}
}

char ex_hub_p_0dis_rj45s2[] = "1.2.5.3";
char ex_hub_p_1dis_rj45s2[] = "1.2.5.2";
//IN HUB2.0 port4 disabled
char ex_hub_4dp_0dis_rj45s2[] = "1.2.4.3";
char ex_hub_4dp_1dis_rj45s2[] = "1.2.4.2";
char * exhub_get_rj45s2_port_number(void)
{
	u8 ctl_data = (u8)usb2517_external_hub_data.usb2517_port_ctl_data;
	u8 inctl_data = (u8)usb2517_internal_hub_data.usb2517_port_ctl_data;
	if(usb2517_external_hub_data.usb2517_chip_reset < 0)
	{
		printk("no ex hub in this proj\n");
		return NULL;
	}

	if((ctl_data&(0x04)) != 0)//port2 is dis
	{
		if((inctl_data&(0x10))!=0)//in hub2.0 port 4 is disabled
			return ex_hub_4dp_1dis_rj45s2;
		else
			return ex_hub_p_1dis_rj45s2;
	}
	else
	{
		if((inctl_data&(0x10))!=0)//in hub2.0 port 4 is disabled
			return ex_hub_4dp_0dis_rj45s2;
		else
			return ex_hub_p_0dis_rj45s2;
	}
}
/*2017-6-10 Jack W Lu: add interface for ttyUSB, get dynamic port number }*/

int usb2517_reset(int s32_usb2517_chip_reset, int hub_location)
{
	int ret = 0;
	if(s32_usb2517_chip_reset > 0)
	{
/*2017-04-17 Jack W Lu: enable reset function {*/
#if 1
		if(hub_location == 1)//external hub
		{
			gpio_set_value(s32_usb2517_chip_reset, 0);
			msleep(10);
			gpio_set_value(s32_usb2517_chip_reset, 1);
		}
		else
		{
			gpio_set_value(s32_usb2517_chip_reset, 1);
			msleep(10);
			gpio_set_value(s32_usb2517_chip_reset, 0);
		}
		msleep(10);
#endif
/*2017-04-17 Jack W Lu: enable reset function }*/
		printk("HUB location = %d, s32_usb2517_chip_reset = %d\n", hub_location, s32_usb2517_chip_reset);
		ret = 1;
	}
	else
		printk("FAIL to reset HUB2.0\n");
	return ret;
}

static int usb2517_write_byte(void *_data, u64 val)
{
	struct usb2517_data *clientdata = _data;
	int rc = 0;
	int loop = 0;
	u8 usb25x_i=0;
	u8 Usb25x_init_table[] =
	{
		0x24, 0x04, 0x17, 0x25, 0x00, 0x00, 0x9B, 0x28,
		0x00, 0x00, 0x20, 0x20, 0x01, 0x32, 0x01, 0x32,
		0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,//8*8

		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,//12*8
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,//16*8

		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,//20*8
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,//24*8

		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,//28*8
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,//32*8
	};

	rc = usb2517_reset(clientdata->usb2517_chip_reset, clientdata->features);
	if(rc == 0)
	{
		printk("NO RESET, NO PORT CONTROL!!! RC=%d\n", rc);
		return 0;
	}
	rc = gpio_get_value(clientdata->usb2517_chip_reset);
	printk(">>>> [usb2517_write_byte] usb2517-chip-reset-gpio (level=%d)\n", rc);

	Usb25x_init_table[10]=(u8)(val);
	Usb25x_init_table[11]=(u8)(val);
	/*2017-10-16 Jack W Lu: Add PWRUSB boost mode control for PP2.0 {*/
	if(clientdata->features == 1)
	{
		if(input_boost_mode == 0xFF)
		{
			Usb25x_init_table[0xF7] = ex_usb2517_pwrusb_boost_mode;//use saved data
		}
		else
		{
			Usb25x_init_table[0xF7] = input_boost_mode;
		}
		printk("boost data = %d\n", Usb25x_init_table[0xF7]);
	}
	/*2017-10-16 Jack W Lu: Add PWRUSB boost mode control for PP2.0 }*/

	/*2017-06-09 Jack W Lu: add retry mechanism for PORT control {*/
	while(usb25x_i++ < 3)
	{
		for(loop=0;loop < (sizeof(Usb25x_init_table))/I2C_SMBUS_BLOCK_MAX;loop++)
		{
			rc = i2c_smbus_write_block_data(clientdata->client, loop*I2C_SMBUS_BLOCK_MAX, I2C_SMBUS_BLOCK_MAX, &Usb25x_init_table[loop*I2C_SMBUS_BLOCK_MAX]);
			printk("i2c_smbus_write_block_data time = %d, rc = %d\n", loop, rc);
		}
		if(rc!=0)
			loop = -1;
		else
		{
			loop = 0;
			clientdata->usb2517_port_ctl_data = (int)(val);
			/*2017-10-16 Jack W Lu: Add PWRUSB boost mode control for PP2.0 {*/
			if((clientdata->features == 1)&&(input_boost_mode != 0xFF))
			{
				ex_usb2517_pwrusb_boost_mode = input_boost_mode;//success, save data
			}
			/*2017-10-16 Jack W Lu: Add PWRUSB boost mode control for PP2.0 }*/
			break;
		}
	}
	/*2017-06-09 Jack W Lu: add retry mechanism for PORT control }*/
	input_boost_mode = 0xFF;//clear input data

	usb25x_i = 1;
	rc = i2c_smbus_write_block_data(clientdata->client, 0xFF, 1, &usb25x_i);
	printk("i2c_smbus_write_block_data enable SMBUS addr [FF], rc = 0x%x\n",rc);
	msleep(10);
	return loop;
}

static int gggggg = 0;
static int usb2517_read_byte(void *_data, u64 * val)
{
	struct usb2517_data *clientdata = _data;
	int rc = 0;
	int loop = 0;
	u8 Usb25x_init_table[256];
	u8 usb25x_i = 0;
	//1 USB2517 not support byte read, QCT not support BLOCK read
	//1 SO, read function IS NOT SUPPORTED
	printk("usb2517_read_byte NOT SUPPORTED = 0x%x\n",rc);
	return 0;

	gggggg++;
	if(gggggg%2){
		usb25x_i = 0;
		rc = i2c_smbus_write_byte_data(clientdata->client, 0xFF, usb25x_i);
		printk("rrrrrrrrr i2c_smbus_write_byte_data enable SMBUS addr [FF] = 0x%x\n",rc);
		msleep(10);
	}

	if(1)
	{
		rc = i2c_smbus_read_i2c_block_data(clientdata->client, 0, I2C_SMBUS_BLOCK_MAX, Usb25x_init_table);
		printk("i2c_smbus_read_block_data block rc = %d\n", rc);
		for(loop=0;loop < I2C_SMBUS_BLOCK_MAX;loop++){
			printk("44444444  i2c_smbus_read_i2c_block_data [0x%x] = 0x%x\n", loop, Usb25x_init_table[loop]);
		}
	}

	if(1)
	{
		for(loop=0;loop < I2C_SMBUS_BLOCK_MAX;loop++){
			rc = i2c_smbus_read_byte_data(clientdata->client, loop);
			printk("33333333 i2c_smbus_read_byte_data addr [0x%x] = 0x%x\n", loop, rc);
			msleep(10);
		}

		rc = i2c_smbus_read_byte_data(clientdata->client, 0xFF);
		printk("usb2517_read_byte addr [FF] = 0x%x\n",rc);
		msleep(10);
	}

	usb25x_i = 1;
	rc = i2c_smbus_write_byte_data(clientdata->client, 0xFF, usb25x_i);
	printk("rrrrrrrrr i2c_smbus_write_byte_data enable SMBUS addr [FF] = 0x%x\n",rc);

	rc = i2c_smbus_read_byte_data(clientdata->client, 0xFF);
	printk("usb2517_read_byte addr [FF] = 0x%x\n",rc);
	msleep(10);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(usb2517_port_ctl_op, usb2517_read_byte,
				usb2517_write_byte, "%lld\n");

/*2017-04-17 Jack W Lu: enable reset function {*/
void usb2517_default_port(void)
{
	if (usb2517_internal_hub_data.usb2517_chip_reset < 0)
	{
		printk("in HUB2.0 reset failed\n");
	}
	else
	{
		/*2017-05-19 JackWLu: fix error, set HUB2.0 port control as saved*/
		usb2517_write_byte(&usb2517_internal_hub_data, usb2517_internal_hub_data.usb2517_port_ctl_data);
		printk("usb2517_internal_hub_data setting OK\n");
	}

	if (usb2517_external_hub_data.usb2517_chip_reset < 0)
	{
		printk("ex HUB2.0 reset failed\n");
	}
	else
	{
		/*2017-05-19 JackWLu: fix error, set HUB2.0 port control as saved*/
		usb2517_write_byte(&usb2517_external_hub_data, usb2517_external_hub_data.usb2517_port_ctl_data);
		printk("usb2517_external_hub_data setting OK\n");
	}
	return;
}
/*2017-04-17 Jack W Lu: enable reset function }*/

#ifdef CONFIG_OF
static const struct of_device_id usb2517_dt_match[] = {
	{ .compatible = "qcom,usb2517", .data = &usb2517_internal_hub_data},
	{ .compatible = "qcom,usb2517_ext", .data = &usb2517_external_hub_data},
	{ },
};
#else
#define usb2517_dt_match NULL
#endif

/*2017-05-09 Jack W Lu: Add hwmon interface for HUB port control {*/
static ssize_t usb2517_port_ctl_set(struct device *dev,
			      struct device_attribute *da,
			      const char *buf, size_t count)
{
	struct usb2517_data *data = dev_get_drvdata(dev);
	unsigned long val = 0;
	int ret;

	if (kstrtoul(buf, 10, &val))
		return -1;
	printk("usb2517_port_ctl_set enter = %ld\n", val);

	ret = usb2517_write_byte((void *)data, (u64)val);
	if (ret < 0)
		return ret;

	return count;
}
static ssize_t usb2517_port_ctl_get(struct device *dev,
		     struct device_attribute *attr,
		     char *buf)
{
	struct usb2517_data *data = dev_get_drvdata(dev);
	printk("usb2517_port_ctl_get = %d\n", data->usb2517_port_ctl_data);

	if(data->features == 1)
	{
		printk("usb2517_port_ctl_get cfd= %s\n", exhub_get_cfd_port_number());
		printk("usb2517_port_ctl_get rj45_2= %s\n", exhub_get_rj45s2_port_number());
	}

	return snprintf(buf, 8, "0x%x\n", data->usb2517_port_ctl_data);
}

static struct device_attribute usb_in_hub2_port_ctl_attr =
	__ATTR(in_hub2_ctl, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP, usb2517_port_ctl_get, usb2517_port_ctl_set);

static struct device_attribute usb_ex_hub2_port_ctl_attr =
	__ATTR(ex_hub2_ctl, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP, usb2517_port_ctl_get, usb2517_port_ctl_set);
#if 0
static DEVICE_ATTR(usb2517_port_ctl_op1, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP, NULL, usb2517_port_ctl_set);

/* Driver data */
static struct attribute *usb2517_attrs[] = {
	&dev_attr_usb2517_port_ctl_op1.attr,
	NULL
};
//ATTRIBUTE_GROUPS(usb2517);
#endif
/*2017-05-09 Jack W Lu: Add hwmon interface for HUB port control }*/

/*2017-10-16 Jack W Lu: Add PWRUSB boost mode control for PP2.0 {*/
static ssize_t pwrusb_boost_ctl_set(struct device *dev,
			      struct device_attribute *da,
			      const char *buf, size_t count)
{
	struct usb2517_data *data = dev_get_drvdata(dev);
	unsigned long val = 0;
	int ret;

	if (kstrtoul(buf, 10, &val))
		return -1;
	printk("pwrusb_boost_ctl_set enter = %ld\n", val);
	input_boost_mode = (unsigned char)val;
	if(input_boost_mode > 3)
	{
		printk("pwrusb_boost_ctl_set invalid parameter = %d\n", input_boost_mode);
		return -1;
	}
	ret = usb2517_write_byte((void *)data, (u64)data->usb2517_port_ctl_data);
	if (ret < 0)
		return ret;

	return count;
}
static ssize_t pwrusb_boost_ctl_get(struct device *dev,
		     struct device_attribute *attr,
		     char *buf)
{
	printk("pwrusb_boost_ctl_get = %d\n", ex_usb2517_pwrusb_boost_mode);

	return snprintf(buf, 8, "0x%x\n", ex_usb2517_pwrusb_boost_mode);
}

static struct device_attribute ex_pwrusb_boost_ctl_attr =
	__ATTR(pwrusb_boost_ctl, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP, pwrusb_boost_ctl_get, pwrusb_boost_ctl_set);
/*2017-10-16 Jack W Lu: Add PWRUSB boost mode control for PP2.0 }*/

static int usb2517_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int rc = 0;
	struct device_node *np = client->dev.of_node;
	struct usb2517_data *clientdata = NULL;
	struct dentry *temp;

	printk("2517-smbus enter, I2C addr = 0x%x\n",client->addr);
	if (np) {
		const struct of_device_id *of_id;

		of_id = of_match_device(of_match_ptr(usb2517_dt_match), &client->dev);
		if (of_id)
			clientdata = (struct usb2517_data *)of_id->data;
	}

	if (!clientdata) {
		printk("Unknown device type\n");
		return -EINVAL;
	}

	//for qcom,usb2517-chip-enable-gpio
	clientdata->usb2517_chip_reset = of_get_named_gpio(client->dev.of_node,
		"qcom,usb2517-reset-gpio", 0);
	printk("usb2517-chip is (internal = 0, external = 1) =%d\n", clientdata->features);
	printk("usb2517-chip-reset-gpio =%d\n", clientdata->usb2517_chip_reset);

	if (gpio_is_valid(clientdata->usb2517_chip_reset)) {
		rc = devm_gpio_request_one(&client->dev, clientdata->usb2517_chip_reset,
			GPIOF_DIR_OUT, "reset_hub");
		if (rc< 0) {
			printk("usb2517 could NOT acquire enable gpio (err=%d)\n", rc);
		}
		else
		{
			if(clientdata->features == 0)
				gpio_direction_output(clientdata->usb2517_chip_reset, 1);//internal hub2.0
			else
				gpio_direction_output(clientdata->usb2517_chip_reset, 0);//external hub2.0
			rc = gpio_get_value(clientdata->usb2517_chip_reset);
			printk("req OK usb2517-chip-reset-gpio (level=%d)\n", rc);
			gpio_export(clientdata->usb2517_chip_reset, false);
		}
	}

	clientdata->client = client;
	i2c_set_clientdata(client, clientdata);
	if(clientdata->features == 0)
	{
		device_create_file(&client->dev, &usb_in_hub2_port_ctl_attr);

		temp = debugfs_create_file("usb2517_port_ctl",
					  S_IFREG | S_IWUSR | S_IRUGO,
					  clientdata->debugfs, clientdata,
					  &usb2517_port_ctl_op);
		printk("usb2517-smbus internal HUB OK\n");
	}
	else
	{
		device_create_file(&client->dev, &usb_ex_hub2_port_ctl_attr);
		device_create_file(&client->dev, &ex_pwrusb_boost_ctl_attr);

		temp = debugfs_create_file("usb2517_port_ctl_ext",
					  S_IFREG | S_IWUSR | S_IRUGO,
					  clientdata->debugfs, clientdata,
					  &usb2517_port_ctl_op);
		printk("usb2517-smbus external HUB OK\n");
	}

	return 0;
}

static int usb2517_remove(struct i2c_client *client)
{

	return 0;
}

static struct i2c_driver usb2517_driver = {
	.driver = {
		.name = "qcom,usb2517",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(usb2517_dt_match),
	},
	.probe = usb2517_probe,
	.remove = usb2517_remove,
	.id_table = usb2517_id,
};

module_i2c_driver(usb2517_driver);

MODULE_AUTHOR("Jack");
MODULE_DESCRIPTION("HUB2.0 USB2517 I2C SMBUS Driver");
MODULE_LICENSE("GPL");
