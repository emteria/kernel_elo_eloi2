/* Qisda, Jerry.Zhai, 2017/09/12, USB Gigabit ethernet controller pwr control */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/switch.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#define DRV_NAME "usb_gigabit_eth_module"
#define PP_USB_ETHERNET_RESET_DELAY 5  // 2017/09/29, Jerry Zhai, PP update ethernet timing
static unsigned int eth_power_en = 0;
static unsigned int eth_wake_up = 0;
static unsigned int eth_reset_gpio = 0;

static const struct of_device_id usb_gigabit_eth_module_match[]  = {
	{ .compatible = "qcom,usb_gigabit_eth_module"},
	{},
};

void usb_gigabit_eth_gpio_control(void)
{
		int eth_pwr_level = 0;

		printk("Debug, in usb_gigabit_eth_gpio_control.\n");

		eth_pwr_level = gpio_get_value(eth_power_en);
		printk("Debug, before pull up eth_power_en = %d\n", eth_pwr_level);
		gpio_direction_output(eth_power_en, 1);
		mdelay(PP_USB_ETHERNET_RESET_DELAY);
		eth_pwr_level = gpio_get_value(eth_power_en);
		printk("Debug, after pull up eth_power_en = %d\n", eth_pwr_level);

		eth_pwr_level = gpio_get_value(eth_reset_gpio);
		printk("Debug, after pull down eth_reset_gpio = %d\n", eth_pwr_level);

		gpio_direction_output(eth_reset_gpio, 1);
		eth_pwr_level = gpio_get_value(eth_reset_gpio);
		printk("Debug, after pull up eth_reset_gpio = %d\n", eth_pwr_level);

		eth_pwr_level = gpio_get_value(eth_wake_up);
		printk("Debug, before reset eth_wake_up = %d\n", eth_pwr_level);
		msleep(PP_USB_ETHERNET_RESET_DELAY);
		gpio_direction_output(eth_wake_up, 1);
		eth_pwr_level = gpio_get_value(eth_wake_up);
		printk("Debug, after reset eth_wake_up = %d\n", eth_pwr_level);
}

static int usb_gigabit_eth_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	int ret;

	printk("Debug, usb_gigabit_eth enable enter.\n");
	if (!pdev->dev.of_node) {
		dev_err(&pdev->dev, "Debug, No platform supplied from device tree\n");
		return -EINVAL;
	}

	match = of_match_node(usb_gigabit_eth_module_match, pdev->dev.of_node);
	if (!match) {
		dev_err(&pdev->dev, "Debug, %s: no matched codec is found.\n",
			__func__);
		goto err;
	}

	eth_power_en = of_get_named_gpio(pdev->dev.of_node, "qcom,eth-power-en", 0);
	printk("Debug, eth_power_en == %d\n", eth_power_en);
	if (gpio_is_valid(eth_power_en)) {
		dev_dbg(&pdev->dev, "Debug, %s: eth_power_en request %d\n", __func__, eth_power_en);
		ret = gpio_request(eth_power_en, "eth_power_en");
		if (ret) {
			dev_err(&pdev->dev, "Debug, %s: eth_power_en request failed, ret:%d\n", __func__, ret);
			goto err;
		}
	}
	else{
		ret = -1;
		dev_err(&pdev->dev, "Debug, %s: eth_power_en gpio is unavailiable.\n",
			__func__);
		goto err;
	}

	eth_reset_gpio = of_get_named_gpio(pdev->dev.of_node, "qcom,eth-reset-gpio", 0);
	printk("Debug, eth_reset_gpio == %d\n", eth_reset_gpio);
	if (gpio_is_valid(eth_reset_gpio)) {
		dev_dbg(&pdev->dev, "Debug, %s: eth_reset_gpio request %d\n", __func__, eth_reset_gpio);
		ret = gpio_request(eth_reset_gpio, "eth_reset_gpio");
		if (ret) {
			dev_err(&pdev->dev, "Debug, %s: eth_reset_gpio request failed, ret:%d\n", __func__, ret);
			goto err;
		}
	}
	else{
		ret = -1;
		dev_err(&pdev->dev, "Debug, %s: eth_reset_gpio gpio is unavailiable.\n",
			__func__);
		goto err;
	}

	eth_wake_up = of_get_named_gpio(pdev->dev.of_node, "qcom,eth-wake-up", 0);
	printk("Debug, eth_wake_up == %d\n", eth_wake_up);
	if (gpio_is_valid(eth_wake_up)) {
		dev_dbg(&pdev->dev, "Debug, %s: eth_wake_up request %d\n", __func__, eth_wake_up);
		ret = gpio_request(eth_wake_up, "eth_wake_up");
		if (ret) {
			dev_err(&pdev->dev, "Debug, %s: eth_wake_up request failed, ret:%d\n", __func__, ret);
			goto err;
		}
	}
	else{
		ret = -1;
		dev_err(&pdev->dev, "Debug, %s: eth_wake_up gpio is unavailiable.\n",
			__func__);
		goto err;
	}
	usb_gigabit_eth_gpio_control();

err:
	if (ret < 0 && eth_power_en > 0) {
		dev_err(&pdev->dev, "Debug, %s free eth_power_en %d\n", __func__, eth_power_en);
		gpio_free(eth_power_en);
		eth_power_en = 0;
	}

	if (ret < 0 && eth_reset_gpio > 0) {
		dev_err(&pdev->dev, "Debug, %s free eth_reset_gpio %d\n", __func__, eth_reset_gpio);
		gpio_free(eth_reset_gpio);
		eth_reset_gpio = 0;
	}

	if (ret < 0 && eth_wake_up > 0) {
		dev_err(&pdev->dev, "Debug, %s free eth_wake_up %d\n", __func__, eth_wake_up);
		gpio_free(eth_wake_up);
		eth_wake_up = 0;
	}

	return ret;
}

static struct platform_driver usb_gigabit_eth_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = usb_gigabit_eth_module_match,
	},
	.probe = usb_gigabit_eth_probe,
};

static int __init usb_gigabit_eth_init(void)
{
	printk("Debug, usb_gigabit ethernet init begin\n");
	return platform_driver_register(&usb_gigabit_eth_driver);
}

static void __exit usb_gigabit_eth_exit(void)
{
	platform_driver_unregister(&usb_gigabit_eth_driver);
}

late_initcall(usb_gigabit_eth_init);
module_exit(usb_gigabit_eth_exit);

MODULE_DESCRIPTION("USB GIGABIT ETHERNET MODULE");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DEVICE_TABLE(of, usb_gigabit_eth_module_match);
