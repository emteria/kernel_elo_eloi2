/*
 * LEDs driver for GPIOs
 *
 * Copyright (C) 2007 8D Technologies inc.
 * Raphael Assenat <raph@8d.com>
 * Copyright (C) 2008 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/uaccess.h>

#include <linux/qpnp/pwm.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>

#define TORCH_BRIGHT_TO_BL(out, v, bl_max, max_bright)	\
	do {						\
		out = (2 * (v) * (bl_max) + max_bright);\
		do_div(out, 2 * max_bright);		\
	} while (0)


#define TIMER_50MS_WQ		50

#define TORCH_DEBUG_ENABLE	1

#if TORCH_DEBUG_ENABLE
#define TORCH_DEBUG		pr_err
#else
#define TORCH_DEBUG
#endif

struct gpio_led_data {
	struct led_classdev cdev;
	struct gpio_desc *gpiod;
	u8 can_sleep;
	u8 blinking;
	struct pwm_device *pwm_bl;
	u32 pwm_period;
	u32 pwm_freq_min;
	u32 pwm_freq_max;
	u32 pwm_freq;
	u32 pwm_level;
	u32 pwm_level_dt;
	u32 brightness_max;
	u32 tl_max;
	u32 tl_min;
	u32 pwm_enabled;
	bool use_pwm;
	gpio_blink_set_t platform_gpio_blink_set;
	struct workqueue_struct *torch_work_queue;
	struct delayed_work torch_work;
};

static u32 torch_switch_freq_to_period(u32 freq)
{
	return (u32)(USEC_PER_SEC / freq);
}

static void torch_config_pwm(struct gpio_led_data *led, u32 level)
{
	int ret;
	u64 duty_ns;
	u64 period_ns;
	struct pwm_state pstate;
	int rc;

	if (led->pwm_bl == NULL) {
		pr_err("%s: no PWM\n", __func__);
		return;
	}

	if (level == 0) {
		if (led->pwm_enabled) {
			pr_err("%s: disable pwm\n", __func__);
			ret = pwm_config(led->pwm_bl, 0, led->pwm_period * NSEC_PER_USEC);
			if (ret)
				pr_err("%s: pwm_config() failed err=%d.\n", __func__, ret);
			pwm_disable(led->pwm_bl);
		}
		led->pwm_enabled = 0;
		return;
	}

	//duty:pwm duty cycle
	led->pwm_period = torch_switch_freq_to_period(led->pwm_freq);
	period_ns = led->pwm_period * NSEC_PER_USEC;
	duty_ns = level * period_ns;
	duty_ns /= led->tl_max;

	TORCH_DEBUG("[%s]:level=%d period_ns=%lld tl_max=%d duty=%lld\n",
		    __func__, level, period_ns, led->tl_max, duty_ns);

	pwm_get_state(led->pwm_bl, &pstate);
	pstate.period = period_ns;
	pstate.duty_cycle = duty_ns;
	pstate.output_type = PWM_OUTPUT_FIXED;

	pstate.output_pattern = NULL;
	rc = pwm_apply_state(led->pwm_bl, &pstate);
	if (rc < 0) {
		pr_err("%s: config pwm error!!\n", __func__);
		return;
	}

	if (!led->pwm_enabled) {
		ret = pwm_enable(led->pwm_bl);
		if (ret)
			pr_err("%s: pwm_enable() failed err=%d\n", __func__, ret);
		led->pwm_enabled = 1;
	}
}

static void torch_set_brightness(struct gpio_led_data *led,
				     u32 value)
{
	u64 bl_lvl;

	if (value > led->brightness_max)
		value = led->brightness_max;

	/* This maps torch light level 0 to 255 into
	 * driver backlight level 0 to bl_max with rounding
	 */
	TORCH_BRIGHT_TO_BL(bl_lvl, value, led->tl_max, led->brightness_max);

	if (!bl_lvl && value)
		bl_lvl = 1;
	torch_config_pwm(led, bl_lvl);
}

static void torch_work_handler(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct gpio_led_data *led_dat = container_of(dwork, struct gpio_led_data, torch_work);

	TORCH_DEBUG("[%s] led_dat->pwm_level:%d\n", __func__, led_dat->pwm_level);
	TORCH_DEBUG("[%s] led_dat->pwm_freq:%d\n", __func__, led_dat->pwm_freq);
	torch_set_brightness(led_dat, led_dat->pwm_level);
}

static inline struct gpio_led_data *
			cdev_to_gpio_led_data(struct led_classdev *led_cdev)
{
	return container_of(led_cdev, struct gpio_led_data, cdev);
}

static void gpio_led_set(struct led_classdev *led_cdev,
	enum led_brightness value)
{
	struct gpio_led_data *led_dat = cdev_to_gpio_led_data(led_cdev);
	int level;

	if (led_dat->use_pwm) {
		led_dat->pwm_level = value;
		queue_delayed_work(led_dat->torch_work_queue, &led_dat->torch_work, msecs_to_jiffies(TIMER_50MS_WQ));
		return;
	}

	if (value == LED_OFF)
		level = 0;
	else
		level = 1;

	if (led_dat->blinking) {
		led_dat->platform_gpio_blink_set(led_dat->gpiod, level,
						 NULL, NULL);
		led_dat->blinking = 0;
	} else {
		if (led_dat->can_sleep)
			gpiod_set_value_cansleep(led_dat->gpiod, level);
		else
			gpiod_set_value(led_dat->gpiod, level);
	}
}

static ssize_t pwm_freq_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct gpio_led_data *led_dat = cdev_to_gpio_led_data(led_cdev);

	return snprintf(buf, PAGE_SIZE, "%u\n", led_dat->pwm_freq);
}

static ssize_t pwm_freq_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct gpio_led_data *led_dat = cdev_to_gpio_led_data(led_cdev);
	u32 freq;
	int ret;

	ret = kstrtou32(buf, 10, &freq);
	if (ret)
		return ret;

	led_dat->pwm_freq = freq;
	queue_delayed_work(led_dat->torch_work_queue, &led_dat->torch_work, msecs_to_jiffies(TIMER_50MS_WQ));
	return count;
}

static DEVICE_ATTR(pwm_freq, 0644, pwm_freq_show, pwm_freq_store);

static const struct attribute *pwm_attributes[] = {
	&dev_attr_pwm_freq.attr,
	NULL,
};

static int gpio_led_set_blocking(struct led_classdev *led_cdev,
	enum led_brightness value)
{
	gpio_led_set(led_cdev, value);
	return 0;
}

static int gpio_blink_set(struct led_classdev *led_cdev,
	unsigned long *delay_on, unsigned long *delay_off)
{
	struct gpio_led_data *led_dat = cdev_to_gpio_led_data(led_cdev);

	led_dat->blinking = 1;
	return led_dat->platform_gpio_blink_set(led_dat->gpiod, GPIO_LED_BLINK,
						delay_on, delay_off);
}

static int create_gpio_led(const struct gpio_led *template,
	struct gpio_led_data *led_dat, struct device *parent,
	gpio_blink_set_t blink_set)
{
	int ret, state;

	led_dat->gpiod = template->gpiod;
	if (!led_dat->gpiod) {
		/*
		 * This is the legacy code path for platform code that
		 * still uses GPIO numbers. Ultimately we would like to get
		 * rid of this block completely.
		 */
		unsigned long flags = GPIOF_OUT_INIT_LOW;

		/* skip leds that aren't available */
		if (!gpio_is_valid(template->gpio)) {
			dev_info(parent, "Skipping unavailable LED gpio %d (%s)\n",
					template->gpio, template->name);
			return 0;
		}

		if (template->active_low)
			flags |= GPIOF_ACTIVE_LOW;

		ret = devm_gpio_request_one(parent, template->gpio, flags,
					    template->name);
		if (ret < 0)
			return ret;

		led_dat->gpiod = gpio_to_desc(template->gpio);
		if (!led_dat->gpiod)
			return -EINVAL;
	}

	led_dat->cdev.name = template->name;
	led_dat->cdev.default_trigger = template->default_trigger;
	led_dat->can_sleep = gpiod_cansleep(led_dat->gpiod);
	if (!led_dat->can_sleep)
		led_dat->cdev.brightness_set = gpio_led_set;
	else
		led_dat->cdev.brightness_set_blocking = gpio_led_set_blocking;
	led_dat->blinking = 0;
	if (blink_set) {
		led_dat->platform_gpio_blink_set = blink_set;
		led_dat->cdev.blink_set = gpio_blink_set;
	}
	if (template->default_state == LEDS_GPIO_DEFSTATE_KEEP) {
		state = gpiod_get_value_cansleep(led_dat->gpiod);
		if (state < 0)
			return state;
	} else {
		state = (template->default_state == LEDS_GPIO_DEFSTATE_ON);
	}
	led_dat->cdev.brightness = state ? LED_FULL : LED_OFF;
	if (!template->retain_state_suspended)
		led_dat->cdev.flags |= LED_CORE_SUSPENDRESUME;
	if (template->panic_indicator)
		led_dat->cdev.flags |= LED_PANIC_INDICATOR;

	ret = gpiod_direction_output(led_dat->gpiod, state);
	if (ret < 0)
		return ret;

	return devm_led_classdev_register(parent, &led_dat->cdev);
}

struct gpio_leds_priv {
	int num_leds;
	struct regulator *vdd_ldo_1, *vdd_ldo_2;
	struct gpio_led_data leds[];
};

static inline int sizeof_gpio_leds_priv(int num_leds)
{
	return sizeof(struct gpio_leds_priv) +
		(sizeof(struct gpio_led_data) * num_leds);
}

static struct gpio_leds_priv *gpio_leds_create(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fwnode_handle *child;
	struct gpio_leds_priv *priv;
	int count, ret, error;
	int rc = 0;
	u32 tmp;

	count = device_get_child_node_count(dev);
	if (!count)
		return ERR_PTR(-ENODEV);

	priv = devm_kzalloc(dev, sizeof_gpio_leds_priv(count), GFP_KERNEL);
	if (!priv)
		return ERR_PTR(-ENOMEM);

	device_for_each_child_node(dev, child) {
		struct gpio_led_data *led_dat = &priv->leds[priv->num_leds];
		struct gpio_led led = {};
		const char *state = NULL;
		const char *use_pwm = NULL;
		struct device_node *np = to_of_node(child);

		led.gpiod = devm_get_gpiod_from_child(dev, NULL, child);
		if (IS_ERR(led.gpiod)) {
			fwnode_handle_put(child);
			return ERR_CAST(led.gpiod);
		}

		ret = fwnode_property_read_string(child, "label", &led.name);
		if (ret && IS_ENABLED(CONFIG_OF) && np)
			led.name = np->name;
		if (!led.name) {
			fwnode_handle_put(child);
			return ERR_PTR(-EINVAL);
		}

		if (!fwnode_property_read_string(child, "use-pwm", &use_pwm)) {
			if (!strcmp(use_pwm, "true")) {
				led_dat->use_pwm = true;
				led_dat->pwm_bl = of_pwm_get(np, NULL);
				if (IS_ERR(led_dat->pwm_bl)) {
					pr_err("%s: Error, pwm device!!\n", __func__);
					led_dat->pwm_bl = NULL;
					led_dat->use_pwm = false;
					fwnode_handle_put(child);
					return ERR_PTR(-ENODEV);
				}
				TORCH_DEBUG("\n %s get pwm success!!\n", __func__);

				led_dat->pwm_enabled = 0;

				//get torch-pwm-frequency-min
				rc = of_property_read_u32(np, "torch-pwm-frequency-min", &tmp);
				if (rc) {
					pr_err("%s:%d, Error, torch-pwm-frequency-min\n", __func__, __LINE__);
					fwnode_handle_put(child);
					return ERR_PTR(-ENODEV);
				}
				led_dat->pwm_freq_min = tmp;
				TORCH_DEBUG("\n %s get torch-pwm-frequency-min(%d) success!!\n", __func__, tmp);

				rc = of_property_read_u32(np, "torch-pwm-frequency-max", &tmp);
				if (rc) {
					pr_err("%s: %d, Error, torch-pwm-frequency-max\n", __func__, __LINE__);
					fwnode_handle_put(child);
					return ERR_PTR(-ENODEV);
				}
				led_dat->pwm_freq_max = tmp;
				TORCH_DEBUG("\n %s get torch-pwm-frequency-max(%d) success!!\n", __func__, tmp);

				//get period
				rc = of_property_read_u32(np, "torch-pwm-frequency-default", &tmp);
				if (rc) {
					pr_err("%s: %d, Error, torch-pwm-frequency-default\n", __func__, __LINE__);
					fwnode_handle_put(child);
					return ERR_PTR(-ENODEV);
				}
				if (tmp < led_dat->pwm_freq_min)
					led_dat->pwm_freq = led_dat->pwm_freq_min;
				else if (tmp > led_dat->pwm_freq_max)
					led_dat->pwm_freq = led_dat->pwm_freq_max;
				else
					led_dat->pwm_freq = tmp;
				led_dat->pwm_period = torch_switch_freq_to_period(led_dat->pwm_freq);
				TORCH_DEBUG("\n %s get torch-pwm-period(%d) success!!\n", __func__, led_dat->pwm_period);

				//get min level
				rc = of_property_read_u32(np, "torch-min-level", &tmp);
				if (rc) {
					pr_err("%s: %d, Error, torch-min-level\n", __func__, __LINE__);
					fwnode_handle_put(child);
					return ERR_PTR(-ENODEV);
				}
				TORCH_DEBUG("\n %s get torch-min-level(%d) success!!\n", __func__, tmp);
				led_dat->tl_min = tmp;

				//get max level
				rc = of_property_read_u32(np, "torch-max-level", &tmp);
				if (rc) {
					pr_err("%s: %d, Error, torch-max-level\n", __func__, __LINE__);
					fwnode_handle_put(child);
					return ERR_PTR(-ENODEV);
				}
				TORCH_DEBUG("\n %s get torch-max-level(%d) success!!\n", __func__, tmp);
				led_dat->tl_max = tmp;

				//get bright_max
				rc = of_property_read_u32(np, "torch-brightness-max-level", &tmp);
				if (rc) {
					pr_err("%s: %d, Error, torch-brightness-max-level\n", __func__, __LINE__);
					fwnode_handle_put(child);
					return ERR_PTR(-ENODEV);
				}
				TORCH_DEBUG("\n %s get torch-brightness-max-level(%d) success!!\n", __func__, tmp);
				led_dat->brightness_max = tmp;

				//get default pwm level
				rc = of_property_read_u32(np, "torch-default-level", &tmp);
				if (rc) {
					pr_err("%s: %d, Error, torch-default-level\n", __func__, __LINE__);
					fwnode_handle_put(child);
					return ERR_PTR(-ENODEV);
				}
				TORCH_DEBUG("\n %s get torch-default-level(%d) success!!\n", __func__, tmp);
				led_dat->pwm_level_dt = tmp;

				if (!strcmp(led.name, "sdm450:rear:torch")) {
					led_dat->torch_work_queue = create_singlethread_workqueue("torch_work");
					if (led_dat->torch_work_queue == NULL) {
						pr_err("%s: could not create workqueue\n", __func__);
						fwnode_handle_put(child);
						return ERR_PTR(-ENODEV);
					}

					INIT_DELAYED_WORK(&led_dat->torch_work, torch_work_handler);
				} else if (!strcmp(led.name, "sdm450:rear:irtorch")) {
					led_dat->torch_work_queue = create_singlethread_workqueue("irtorch_work");
					if (led_dat->torch_work_queue == NULL) {
						pr_err("%s: could not create workqueue\n", __func__);
						fwnode_handle_put(child);
						return ERR_PTR(-ENODEV);
					}

					INIT_DELAYED_WORK(&led_dat->torch_work, torch_work_handler);
				} else {
					pr_err("%s: Unknown torch led: %s\n", __func__, led.name);
					fwnode_handle_put(child);
					return ERR_PTR(-ENODEV);
				}
			} else if (!strcmp(use_pwm, "false")) {
				TORCH_DEBUG("%s: no not use pwm!!\n", __func__);
			}
		}

		fwnode_property_read_string(child, "linux,default-trigger",
					    &led.default_trigger);

		if (!fwnode_property_read_string(child, "default-state",
						 &state)) {
			if (!strcmp(state, "keep"))
				led.default_state = LEDS_GPIO_DEFSTATE_KEEP;
			else if (!strcmp(state, "on"))
				led.default_state = LEDS_GPIO_DEFSTATE_ON;
			else
				led.default_state = LEDS_GPIO_DEFSTATE_OFF;
		}

		if (fwnode_property_present(child, "retain-state-suspended"))
			led.retain_state_suspended = 1;
		if (fwnode_property_present(child, "panic-indicator"))
			led.panic_indicator = 1;

		ret = create_gpio_led(&led, led_dat, dev, NULL);
		if (ret < 0) {
			fwnode_handle_put(child);
			return ERR_PTR(ret);
		}
		if (led_dat->use_pwm) {
			ret = sysfs_create_files(&led_dat->cdev.dev->kobj, pwm_attributes);
			if (ret) {
				pr_err("%s: sysfs_create_files error rc=%d\n", __func__, ret);
				return ERR_PTR(ret);
			}
		}

		led_dat->cdev.dev->of_node = np;
		priv->num_leds++;
	}
	priv->vdd_ldo_1 = regulator_get(&pdev->dev, "vdd_ldo_1");
	if (IS_ERR(priv->vdd_ldo_1)) {
		error = PTR_ERR(priv->vdd_ldo_1);
		pr_err("%s: regulator get failed vdd_ldo_1 rc=%d\n",
			__func__, error);
	}
	ret = regulator_enable(priv->vdd_ldo_1);
	if (ret) {
		pr_err("%s: Regulator vdd_ldo_1 enable failed rc=%d\n",
			__func__, ret);
	}
	priv->vdd_ldo_2 = regulator_get(&pdev->dev, "vdd_ldo_2");
	if (IS_ERR(priv->vdd_ldo_2)) {
		error = PTR_ERR(priv->vdd_ldo_2);
		pr_err("%s: regulator get failed vdd_ldo_2 rc=%d\n",
			__func__, error);
	}
	ret = regulator_enable(priv->vdd_ldo_2);
	if (ret) {
		pr_err("%s: Regulator vdd_ldo_2 enable failed rc=%d\n",
			__func__, ret);
	}
	return priv;
}

static const struct of_device_id of_gpio_leds_match[] = {
	{ .compatible = "gpio-leds", },
	{},
};

MODULE_DEVICE_TABLE(of, of_gpio_leds_match);

static int gpio_led_probe(struct platform_device *pdev)
{
	struct gpio_led_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct gpio_leds_priv *priv;
	int i, ret = 0;

	if (pdata && pdata->num_leds) {
		priv = devm_kzalloc(&pdev->dev,
				sizeof_gpio_leds_priv(pdata->num_leds),
					GFP_KERNEL);
		if (!priv)
			return -ENOMEM;

		priv->num_leds = pdata->num_leds;
		for (i = 0; i < priv->num_leds; i++) {
			ret = create_gpio_led(&pdata->leds[i],
					      &priv->leds[i],
					      &pdev->dev, pdata->gpio_blink_set);
			if (ret < 0)
				return ret;
		}
	} else {
		priv = gpio_leds_create(pdev);
		if (IS_ERR(priv))
			return PTR_ERR(priv);
	}

	platform_set_drvdata(pdev, priv);

	return 0;
}

static void gpio_led_shutdown(struct platform_device *pdev)
{
	struct gpio_leds_priv *priv = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < priv->num_leds; i++) {
		struct gpio_led_data *led = &priv->leds[i];

		gpio_led_set(&led->cdev, LED_OFF);
	}
}

static int gpio_led_suspend(struct platform_device *pdev, pm_message_t message)
{
	int ret;
	struct gpio_leds_priv *priv = platform_get_drvdata(pdev);

	if (priv->vdd_ldo_1) {
		ret = regulator_disable(priv->vdd_ldo_1);
		if (ret) {
			pr_err("%s: Regulator vdd_ldo_1 disable failed rc=%d\n",
				__func__, ret);
			return ret;
		}
	}
	if (priv->vdd_ldo_2) {
		ret = regulator_disable(priv->vdd_ldo_2);
		if (ret) {
			pr_err("%s: Regulator vdd_ldo_2 disable failed rc=%d\n",
				__func__, ret);
			return ret;
		}
	}
	return 0;
}

static int gpio_led_resume(struct platform_device *pdev)
{
	int ret;
	struct gpio_leds_priv *priv = platform_get_drvdata(pdev);

	ret = regulator_enable(priv->vdd_ldo_1);
	if (ret) {
		pr_err("%s: Regulator vdd_ldo_1 enable failed rc=%d\n",
			__func__, ret);
		return ret;
	}
	ret = regulator_enable(priv->vdd_ldo_2);
	if (ret) {
		pr_err("%s: Regulator vdd_ldo_2 enable failed rc=%d\n",
			__func__, ret);
		return ret;
	}
	return 0;
}

static struct platform_driver gpio_led_driver = {
	.probe		= gpio_led_probe,
	.shutdown	= gpio_led_shutdown,
	.driver		= {
		.name	= "leds-gpio",
		.of_match_table = of_gpio_leds_match,
	},
	.suspend = gpio_led_suspend,
	.resume = gpio_led_resume,
};

module_platform_driver(gpio_led_driver);

MODULE_AUTHOR("Raphael Assenat <raph@8d.com>, Trent Piepho <tpiepho@freescale.com>");
MODULE_DESCRIPTION("GPIO LED driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:leds-gpio");
