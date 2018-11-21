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
#include <linux/delay.h>
#include <linux/debugfs.h>
/*2017-05-09 Jack W Lu: Add hwmon interface for HUB port control {*/
#ifdef GL3523_HWMON
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#endif
/*2017-05-09 Jack W Lu: Add hwmon interface for HUB port control }*/

/*2017-07-19 Jack W Lu: Add VBUS control for I2.0 {*/
#include <soc/qcom/socinfo.h>
/*2017-07-19 Jack W Lu: Add VBUS control for I2.0 }*/

#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

extern struct regulator *g_reg_p5v_usb;

static const struct i2c_device_id gl3523_id[] = {
	{ "gl3523", 1},
	{ }
};
MODULE_DEVICE_TABLE(i2c, gl3523_id);

static int s32_hub_power_ctl_gpio = 0;
/*2017-07-19 Jack W Lu: Add VBUS control for I2.0 {*/
struct gl3523_data *gl3523_clientdata = NULL;
static unsigned char port_ctl_data = 0x0;
static int gl3523_write_byte(void *_data, u64 val);
extern char smb_typea_otg_vbus_control(char enable);
static char prjType;
/*2017-07-19 Jack W Lu: Add VBUS control for I2.0 }*/

struct gl3523_data {
/*2017-05-09 Jack W Lu: Add hwmon interface for HUB port control {*/
#ifdef GL3523_HWMON
	struct device *hwmon_dev;
#endif
/*2017-05-09 Jack W Lu: Add hwmon interface for HUB port control }*/
	struct i2c_client *client;
	struct dentry *debugfs;
	u8 features;
	int gl3523_chip_reset;/*for reset: LOW enable, APQ GPIO1*/
	int p5v_usb_pwr_en_gpio;/*for P5V_USB: P5V_USB_PWR_EN GPIO99*/
};

void gl3523_poweron(void)
{
#if 1
	/*2017-05-12 Jack W Lu: set P5V_USB always on {*/
	int rc = 0;
	if(s32_hub_power_ctl_gpio != 0)
		rc = gpio_get_value(s32_hub_power_ctl_gpio);
	printk("gl3523_poweron power level==%d\n", rc);
	return;

	/*2017-05-12 Jack W Lu: set P5V_USB always on }*/
#else
	if(s32_hub_power_ctl_gpio != 0)
	{
		/*pp,tony.l.cai,20170425,bug fix for RTC I2C probe issue{*/
		gpio_set_value(s32_hub_power_ctl_gpio, 0);
		msleep(50);
		gpio_set_value(s32_hub_power_ctl_gpio, 1);
		msleep(100);
		/*}pp,tony.l.cai,20170425,bug fix for RTC I2C probe issue*/
		printk("gl3523_poweron PASS, s32_hub_power_ctl_gpio = %d\n", s32_hub_power_ctl_gpio);
	}
	else
		printk("FAIL to POWER ON HUB3.0\n");
#endif
}
void gl3523_reset(void)
{
/*2017-07-19 Jack W Lu: Add VBUS control for I2.0 {*/
	int ret;
	if ((prjType == PROJECT_AAIO2_STD_101)||
		(prjType == PROJECT_AAIO2_STD_156)||
		(prjType == PROJECT_AAIO2_STD_215)||
		(prjType == PROJECT_AAIO2_VALUE_101)||
		(prjType == PROJECT_AAIO2_VALUE_156)||
		(prjType == PROJECT_PUCK))
	{//for I2.0 and PUCK, need to remember the setting
		if((gl3523_clientdata != NULL)&&(gl3523_clientdata->gl3523_chip_reset != 0))
		{
			ret = gl3523_write_byte((void *)gl3523_clientdata, (u64)port_ctl_data);
			printk("gl3523_reset ret= %d, s32_gl3523_reset_gpio = %d\n", ret, gl3523_clientdata->gl3523_chip_reset);
		}
		else
			printk("FAIL to reset HUB3.0\n");
	}
	else
	{
		if((gl3523_clientdata != NULL)&&(gl3523_clientdata->gl3523_chip_reset != 0))
		{
			gpio_set_value(gl3523_clientdata->gl3523_chip_reset, 1);
			msleep(150);
			gpio_set_value(gl3523_clientdata->gl3523_chip_reset, 0);
			msleep(20);
			printk("PP PASS, s32_gl3523_reset_gpio = %d\n", gl3523_clientdata->gl3523_chip_reset);
		}
		else
			printk("PP FAIL to reset HUB3.0\n");
	}
/*2017-07-19 Jack W Lu: Add VBUS control for I2.0 }*/
	return;
}

static int gl3523_write_byte(void *_data, u64 val)
{
	struct gl3523_data *clientdata = _data;
	int rc1 = 0;
	int rc2 = 0;
	int rc3 = 0;
	int loop = 0;
	u8 setdata = 0;

	printk("gl3523_write_byte enter, val=%Ld \n", val);
    
	switch((u8)(val))
	{
		case 0://all port enable
			setdata = 0;
			break;
		case 4://port 2 disable:bit2
			setdata = 3;
			break;
		case 8://port 3 disable:bit3
			setdata = 2;
			break;
		case 12://port 2&3 disable:bit2&3
			setdata = 7;
			break;
		default:
			printk("HUB3: the setting is invalid : %d\n", (u8)(val));
			return -1;
	}

    /* Carl.S.Hua 20171207, restart hub for device detect issue in PayPoint_Software-000299 { */
    if ( (prjType == PROJECT_PUCK)&&(g_reg_p5v_usb != NULL) )
    {
        rc1 = regulator_disable(g_reg_p5v_usb);
        if(rc1)
            pr_err("---%s:-regulator_disable---error:%d--\n",__func__,rc1);
        printk("PUCK : regulator_disable !!\n");

        msleep(500);
        rc1 = regulator_enable(g_reg_p5v_usb);
        if(rc1)
            pr_err("---%s:-regulator_enable---error:%d--\n",__func__,rc1);
        printk("PUCK : regulator_enable !!\n");
        msleep(100);

        smb_typea_otg_vbus_control(0);//port 2&3 VBUS disable, OFF
        printk("PUCK : disable VBUS first !!\n");

    }
    /* Carl.S.Hua 20171207, restart hub for device detect issue in PayPoint_Software-000299 } */

    gpio_set_value(clientdata->gl3523_chip_reset, 1);
	rc1 = gpio_get_value(clientdata->gl3523_chip_reset);
	printk(">>>> [gl3523_write_byte] gl3523-chip-reset-gpio (level=%d)\n", rc1);

	for(loop=0;loop < 10;loop++){
		rc1 = i2c_smbus_write_byte_data(clientdata->client, 0x0, 0x55);
		if (rc1==0)
			break;
		else
			msleep(50);
		printk("failed rc1 = 0x%x\n", rc1);
	}

	for(loop=0;loop < 10;loop++){
		rc2 = i2c_smbus_write_byte_data(clientdata->client, 0x1, 0xAA);
		if (rc2==0)
			break;
		else
			msleep(50);
		printk("failed rc2 = 0x%x\n", rc2);
	}

	for(loop=0;loop < 10;loop++){
		rc3 = i2c_smbus_write_byte_data(clientdata->client, 2, setdata);
		if (rc3==0)
			break;
		else
			msleep(50);
		printk("failed rc3 = 0x%x\n", rc3);
	}
		printk("i2c_smbus_write_byte_data addr [0] rc1 = 0x%x\n", rc1);
		printk("i2c_smbus_write_byte_data addr [1] rc2 = 0x%x\n", rc2);
		printk("i2c_smbus_write_byte_data addr [2] = 0x%x, rc3 = %d\n", setdata, rc3);

	gpio_set_value(clientdata->gl3523_chip_reset, 0);
	if((rc1 != 0)||(rc2 != 0)||(rc3 != 0))
		rc3 = -1;
	msleep(10);
	rc1 = gpio_get_value(clientdata->gl3523_chip_reset);
	printk(">>>> [gl3523_write_byte] gl3523-chip-reset-gpio (level=%d)\n", rc1);
/*2017-07-19 Jack W Lu: Add VBUS control for I2.0 {*/
	printk("gl3523_write_byte prjType = 0x%x\n", prjType);//45678
	if ((prjType == PROJECT_AAIO2_STD_101)||
		(prjType == PROJECT_AAIO2_STD_156)||
		(prjType == PROJECT_AAIO2_STD_215)||
		(prjType == PROJECT_AAIO2_VALUE_101)||
		(prjType == PROJECT_AAIO2_VALUE_156))
	{
		//for I2.0, typeA VBUS control
		if((setdata == 2)||(setdata == 7))
			smb_typea_otg_vbus_control(0);//port 3 disable, OFF
		else
			smb_typea_otg_vbus_control(1);//port 3 enable, ON
	}
/*2017-11-02 Jack W Lu - to fix PayPoint_Software-000299 {*/
	else if(prjType == PROJECT_PUCK)
	{
		//for PUCK, typeA VBUS control
		if(setdata != 7)
		{
			smb_typea_otg_vbus_control(1);//one port enable, ON
			printk("PUCK : Enable VBUS !!\n");
		}
	}
/*2017-11-02 Jack W Lu - to fix PayPoint_Software-000299 }*/
	else
	{
		smb_typea_otg_vbus_control(1);//port 3 enable, ON
	}
/*2017-07-19 Jack W Lu: Add VBUS control for I2.0 }*/
	return rc3;
}

static int gl3523_read_byte(void *_data, u64 * val)
{
	struct gl3523_data *clientdata = _data;
	int rc = 0;
	int loop = 0;
	printk("gl3523_read_byte NOT SUPPORTED !!\n");
	return 0;
	gpio_set_value(clientdata->gl3523_chip_reset, 0);
	msleep(100);
	gpio_set_value(clientdata->gl3523_chip_reset, 1);
	msleep(100);
	rc = gpio_get_value(clientdata->gl3523_chip_reset);
	printk(">>>> [gl3523_read_byte] gl3523-chip-reset-gpio (level=%d)\n", rc);

	for(loop=9;loop < 10;loop++){
		rc = i2c_smbus_read_byte_data(clientdata->client, 0);
		printk("i2c_smbus_read_byte_data addr [0] = 0x%x\n", rc);
		msleep(10);
		if (rc==0)
			break;
	}
	for(loop=9;loop < 10;loop++){
		rc = i2c_smbus_read_byte_data(clientdata->client, 1);
		printk("i2c_smbus_read_byte_data addr [1] = 0x%x\n", rc);
		msleep(10);
		if (rc==0)
			break;
	}
	for(loop=9;loop < 10;loop++){
		rc = i2c_smbus_read_byte_data(clientdata->client, 2);
		printk("i2c_smbus_read_byte_data addr [2] = 0x%x\n", rc);
		msleep(10);
		if (rc==0)
			break;
	}
	*val = rc;
	msleep(100);

	gpio_set_value(clientdata->gl3523_chip_reset, 0);
	msleep(100);
	rc = gpio_get_value(clientdata->gl3523_chip_reset);
	printk(">>>> [gl3523_read_byte] gl3523-chip-reset-gpio (level=%d)\n", rc);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(gl3523_port_ctl_op, gl3523_read_byte,
				gl3523_write_byte, "%lld\n");

/*2017-05-09 Jack W Lu: Add hwmon interface for HUB port control {*/
static ssize_t gl3523_port_ctl_set(struct device *dev,
			      struct device_attribute *da,
			      const char *buf, size_t count)
{
	struct gl3523_data *data = dev_get_drvdata(dev);
	unsigned long val = 0;
	int ret;

	if (kstrtoul(buf, 10, &val))
		return -1;
	printk("gl3523_port_ctl_set enter = %ld\n", val);

	ret = gl3523_write_byte((void *)data, (u64)val);
	if (ret < 0)
		return ret;
	else
		port_ctl_data = (unsigned char)val;
	return count;
}

static ssize_t gl3523_port_ctl_get(struct device *dev,
		     struct device_attribute *attr,
		     char *buf)
{
#if 0
	struct gl3523_data *data = dev_get_drvdata(dev);
	unsigned long val = 0;
	int ret = 0;
	ret = gl3523_read_byte((void *)data, (u64 *)&val);
#endif
	printk("gl3523_port_ctl_get = %d\n", port_ctl_data);
	return snprintf(buf, 8, "0x%x\n", port_ctl_data);
}

static struct device_attribute hub3_port_ctl_attr =
	__ATTR(hub3_ctl, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP, gl3523_port_ctl_get, gl3523_port_ctl_set);

#ifdef GL3523_HWMON
static DEVICE_ATTR(gl3523_port_ctl_op1, S_IWUSR | S_IWGRP, NULL, gl3523_port_ctl_set);

/* Driver data */
static struct attribute *gl3523_attrs[] = {
	&dev_attr_gl3523_port_ctl_op1.attr,
	NULL
};

ATTRIBUTE_GROUPS(gl3523);
#endif
/*2017-05-09 Jack W Lu: Add hwmon interface for HUB port control }*/

static int gl3523_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int rc = 0;
	struct gl3523_data *clientdata = NULL;
	struct dentry *temp;
/*2017-05-09 Jack W Lu: Add hwmon interface for HUB port control {*/
#ifdef GL3523_HWMON
	const char gl3523_name[] = "gl3523";
#endif
/*2017-05-09 Jack W Lu: Add hwmon interface for HUB port control }*/

	printk("gl3523-smbus enter\n");

	clientdata = devm_kzalloc(&client->dev, sizeof(struct gl3523_data), GFP_KERNEL);
	if (!clientdata)
		return -ENOMEM;
#if 1
	/*pp, tony.l.cai, 20170605, gpio regulator enable for p5v_usb{*/
#ifdef GPIO_P5V_USB_ENABLE
	//for p5v_usb_pwr_en_gpio
	clientdata->p5v_usb_pwr_en_gpio = of_get_named_gpio(client->dev.of_node,
		"qcom,p5v_usb_pwr_en_gpio", 0);
	if (clientdata->p5v_usb_pwr_en_gpio < 0) {
		pr_err("%s: %s property not found %d\n", __func__, "p5v_usb_pwr_en_gpio", clientdata->p5v_usb_pwr_en_gpio);
	}
	if (gpio_is_valid(clientdata->p5v_usb_pwr_en_gpio)) {
		pr_debug("%s: p5v_usb_pwr_en_gpio request %d\n", __func__,
			clientdata->p5v_usb_pwr_en_gpio);
		rc = gpio_request(clientdata->p5v_usb_pwr_en_gpio, "p5v_usb_pwr_en_gpio");
		if (rc) {
			pr_err("p5v_usb_pwr_en_gpio could NOT acquire enable gpio (err=%d)\n", rc);
			return 0;
		}
		else
		{
			//P5V_USB is alway on!!
			/*pp,tony.l.cai,20170425,bug fix for RTC I2C probe issue{*/
			gpio_direction_output(clientdata->p5v_usb_pwr_en_gpio, 1);
			/*}pp,tony.l.cai,20170425,bug fix for RTC I2C probe issue*/
			rc = gpio_get_value(clientdata->p5v_usb_pwr_en_gpio);
			printk("HUB power level==%d\n", rc);
			gpio_export(clientdata->p5v_usb_pwr_en_gpio, false);
			s32_hub_power_ctl_gpio = clientdata->p5v_usb_pwr_en_gpio;
		}
	}
#endif
	/*}pp, tony.l.cai, 20170605, gpio regulator enable for p5v_usb*/
	//for qcom,gl3523-chip-enable-gpio
	clientdata->gl3523_chip_reset = of_get_named_gpio(client->dev.of_node,
		"qcom,gl3523-chip-reset-gpio", 0);
	pr_debug(">>>> [GPIO init] gl3523-chip-reset-gpio =%d\n", clientdata->gl3523_chip_reset);

	if (gpio_is_valid(clientdata->gl3523_chip_reset)) {
		rc = devm_gpio_request_one(&client->dev, clientdata->gl3523_chip_reset,
			GPIOF_DIR_OUT, "reset_hub");
		if (rc< 0) {
			pr_err("gl3523-chip-reset-gpio could NOT acquire enable gpio (err=%d)\n", rc);
		}
		else
		{
#if 0
			/*GL3523 power on seq*/
			// 1. set the reset PIN LOW
			gpio_set_value(clientdata->gl3523_chip_reset, 1);
			msleep(10);
			// 2. set power ON
			gpio_direction_output(clientdata->p5v_usb_pwr_en_gpio, 1);
			// 3. wait 10ms
			msleep(10);
			// 4. set reset PIN HIGH
#endif
			gpio_set_value(clientdata->gl3523_chip_reset, 1);//probe is in reset state
			// 5. finish
			msleep(10);
			rc = gpio_get_value(clientdata->gl3523_chip_reset);
			printk("gl3523_probe gl3523_chip_reset PASS level=%d\n", rc);
			gpio_export(clientdata->gl3523_chip_reset, false);
		}
	}
#endif
/*2017-07-19 Jack W Lu: Add VBUS control for I2.0 {*/
	gl3523_clientdata = clientdata;
	clientdata->client = client;
	prjType = socinfo_get_project_id();
/*2017-07-19 Jack W Lu: Add VBUS control for I2.0 }*/

	i2c_set_clientdata(client, clientdata);
	device_create_file(&client->dev, &hub3_port_ctl_attr);

/*2017-05-09 Jack W Lu: Add hwmon interface for HUB port control {*/
#ifdef GL3523_HWMON
	clientdata->hwmon_dev = hwmon_device_register_with_groups(&client->dev, gl3523_name,
			clientdata, gl3523_groups);
	if (IS_ERR(clientdata->hwmon_dev)) {
		printk("~~~~~hwmon_dev fail~~~~~\n");
	}
#endif
/*2017-05-09 Jack W Lu: Add hwmon interface for HUB port control }*/

	temp = debugfs_create_file("gl3523_port_ctl",
				  S_IFREG | S_IWUSR | S_IRUGO,
				  clientdata->debugfs, clientdata,
				  &gl3523_port_ctl_op);

	printk("gl3523-smbus OK\n");
	return 0;
}

static int gl3523_remove(struct i2c_client *client)
{
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id gl3523_dt_match[] = {
	{ .compatible = "qcom,gl3523-smbus" },
	{ },
};
#else
#define gl3523_dt_match NULL
#endif

static struct i2c_driver gl3523_driver = {
	.driver = {
		.name = "qcom,gl3523-smbus",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(gl3523_dt_match),
	},
	.probe = gl3523_probe,
	.remove = gl3523_remove,
	.id_table = gl3523_id,
};

module_i2c_driver(gl3523_driver);

MODULE_AUTHOR("Jack");
MODULE_DESCRIPTION("HUB3.0 GL3523 I2C SMBUS Driver");
MODULE_LICENSE("GPL");
