/*
 * oem_ptdet.c - power type detect driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

//disable poe detect, if ac is absent and system is power on, poe is present
#define PTDET_POE_IO_IGNORE

struct ptdet_event_data {
	const char *of_name;
	const char *name;
	int gpio;
	int irq;
	int active_low;
	int status;
	struct delayed_work event_work;
};

static struct ptdet_event_data ptdet_io_map[] = {
	{ "ptdet-ac-detect", "power_ac",  -1, -1, -1},
	#ifndef PTDET_POE_IO_IGNORE
	{ "ptdet-poe-detect", "power_poe", -1, -1, -1},
	#endif
	{ NULL,           NULL,             -1, -1, -1},
};

struct ptdet_data {
	struct platform_device *pdev;
	struct device *dev;

	struct ptdet_event_data * pw_event;
	unsigned int debounce_time;
	struct mutex event_lock;
};

static struct ptdet_data* oem_ptdet = NULL;


/* sysfs interface */
static int ptdet_get_io_status(struct device *dev, char* io_name)
{
	struct ptdet_event_data *ptdet_ev = oem_ptdet->pw_event;
	int i;

	if( ptdet_ev == NULL)
	{
		pr_err("%s: ptdet_data is NULL\n", __func__);
		return -1;
	}

	for (i = 0; ptdet_ev[i].name; i++) {
		if(!strcmp(ptdet_ev[i].name, io_name)) {
			if( ptdet_ev[i].active_low )
				return !gpio_get_value_cansleep(ptdet_ev[i].gpio);
			else
				return !!gpio_get_value_cansleep(ptdet_ev[i].gpio);
		}
	}

	/*unsupport name*/
	pr_err("%s: un-support IO: %s\n", __func__, io_name);
	return -1;
}


static ssize_t
ptdet_get_ac_status(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	int status;

	status = ptdet_get_io_status(dev, "power_ac");

	pr_info("%s:  %d\n", __func__, status);

	if( status < 0 )
	{
		return snprintf(buf, PAGE_SIZE, "unknown\n");
	}
	else
	{
		return snprintf(buf, PAGE_SIZE, status? "present\n":"absent\n");
	}
}

static ssize_t
ptdet_get_poe_status(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	int status;

	#ifndef PTDET_POE_IO_IGNORE
	status = ptdet_get_io_status(dev, "power_poe");
	#else
	status = !ptdet_get_io_status(dev, "power_ac");
	#endif

	pr_info("%s:  %d\n", __func__, status);

	if( status < 0 )
	{
		return snprintf(buf, PAGE_SIZE, "unknown\n");
	}
	else
	{
		return snprintf(buf, PAGE_SIZE, status? "present\n":"absent\n");
	}
}

static struct device_attribute ac_status =
	__ATTR(ac_status, S_IRUGO, ptdet_get_ac_status, NULL);

static struct device_attribute poe_status =
	__ATTR(poe_status, S_IRUGO, ptdet_get_poe_status, NULL);


static void ptdet_notify_work_fn(struct work_struct *work)
{
	char *envp[2];
	char devpath_string[32]= {0} ;
	int value = 0;
	struct delayed_work *dwork = to_delayed_work(work);
	struct ptdet_event_data *ptdet_event = container_of(dwork, struct ptdet_event_data, event_work);

	if(ptdet_event == NULL || oem_ptdet == NULL)
	{
		pr_err("%s: ptdet event data or oem ptdet not valid\n", __func__);
		return;
	}

	mutex_lock(&oem_ptdet->event_lock);
	if ( ptdet_event->active_low )
		value = !gpio_get_value_cansleep(ptdet_event->gpio);
	else
		value = !!gpio_get_value_cansleep(ptdet_event->gpio);
	pr_debug("%s: %s value is :%d\n", __func__, ptdet_event->name, value);

	if( value != ptdet_event->status)
	{
		ptdet_event->status = value;

		if (value==0)
			sprintf(devpath_string, "%s_status=absent", ptdet_event->name);
		else
			sprintf(devpath_string, "%s_status=present", ptdet_event->name);
		envp[0]= devpath_string;
		envp[1]=NULL;

		pr_info("%s, %s , GPIO%d = %d\n", __func__,ptdet_event->name, ptdet_event->gpio, value);

		kobject_uevent_env(&oem_ptdet->dev->kobj, KOBJ_CHANGE, envp);
	}
	else
	{
		pr_err("%s: Pseudo interrupt for %s, current status is %d\n", __func__, ptdet_event->name, value);
	}
	mutex_unlock(&oem_ptdet->event_lock);

}


static irqreturn_t ptdet_irq_handler(int irq, void *dev_data)
{
	struct ptdet_data* ptdet_ptr = (struct ptdet_data*)dev_data;
	struct device *dev;
	int i;
	struct ptdet_event_data *ptdet_ev;

	if (!ptdet_ptr)
		return IRQ_HANDLED;

	dev = ptdet_ptr->dev;
	dev_dbg(dev, "%s: irq %d\n", __func__, irq);

	ptdet_ev = ptdet_ptr->pw_event;

	for (i = 0; ptdet_ev[i].of_name; i++) {
		if(irq == ptdet_ev[i].irq) {
			schedule_delayed_work(&ptdet_ev[i].event_work, msecs_to_jiffies(ptdet_ptr->debounce_time));
			break;
		}
	}

	return IRQ_HANDLED;
}

static void ptdet_input_deinit(struct ptdet_data* ptdet_data_ptr)
{
	int i;
	struct ptdet_event_data* ptdet_ev_ptr = NULL;

	ptdet_ev_ptr = ptdet_data_ptr->pw_event;
	if( ptdet_ev_ptr == NULL)
	{
		pr_err("%s: input io data pointer is NULL\n", __func__);
		return ;
	}

	for( i = 0; ptdet_ev_ptr[i].of_name; i++)
	{
		if( ptdet_ev_ptr[i].irq > 0 )
		{
			disable_irq_nosync(ptdet_ev_ptr[i].irq);
			free_irq(ptdet_ev_ptr[i].irq, ptdet_data_ptr);
			cancel_delayed_work_sync(&ptdet_ev_ptr[i].event_work);
			ptdet_ev_ptr[i].irq = -1;
		}

		if ( ptdet_ev_ptr[i].gpio > 0)
		{
			gpio_free(ptdet_ev_ptr[i].gpio);
		}
	}
}

static int ptdet_input_init(struct ptdet_data* ptdet_data_ptr)
{
	int i, ret;
	struct device_node *node = ptdet_data_ptr->dev->of_node;
	struct ptdet_event_data* ptdet_ev_ptr = NULL;
	enum of_gpio_flags gpio_flags;

	ptdet_ev_ptr = ptdet_data_ptr->pw_event;

	if( ptdet_ev_ptr == NULL)
	{
		pr_err("%s: input io data pointer is NULL\n", __func__);
		return -1;
	}

	for( i = 0; ptdet_ev_ptr[i].of_name; i++)
	{
		ptdet_ev_ptr[i].gpio = of_get_named_gpio_flags(node, ptdet_ev_ptr[i].of_name, 0, &gpio_flags);

		if ( ptdet_ev_ptr[i].gpio <= 0 || !gpio_is_valid(ptdet_ev_ptr[i].gpio) ) {
			pr_err("%s: %s property not found %d or not valid\n", __func__, ptdet_ev_ptr[i].of_name, ptdet_ev_ptr[i].gpio);
			continue;
		}

		pr_info("%s: (%s)request %d\n", __func__, ptdet_ev_ptr[i].name, ptdet_ev_ptr[i].gpio);

		ret = gpio_request(ptdet_ev_ptr[i].gpio, ptdet_ev_ptr[i].name);
		if (ret) {
			pr_err("%s could not acquire enable gpio (err=%d)\n", ptdet_ev_ptr[i].name, ret);
			ptdet_ev_ptr[i].gpio = -1;
			goto fail;
		}
		else
		{
			gpio_direction_input(ptdet_ev_ptr[i].gpio);
			gpio_export(ptdet_ev_ptr[i].gpio, true);
			ptdet_ev_ptr[i].active_low = gpio_flags & OF_GPIO_ACTIVE_LOW;
			if( ptdet_ev_ptr[i].active_low )
				pr_err("%s: %s is active low\n", __func__, ptdet_ev_ptr[i].name);

			ptdet_ev_ptr[i].irq = gpio_to_irq(ptdet_ev_ptr[i].gpio);
			if( ptdet_ev_ptr[i].irq < 0)
			{
				pr_err("%s: could not map irq for %s (gpio %d)\n", __func__, ptdet_ev_ptr[i].name,  ptdet_ev_ptr[i].gpio);
				goto fail;
			}

			/*init status*/
			if ( ptdet_ev_ptr[i].active_low)
				ptdet_ev_ptr[i].status = !gpio_get_value_cansleep(ptdet_ev_ptr[i].gpio);
			else
				ptdet_ev_ptr[i].status = !!gpio_get_value_cansleep(ptdet_ev_ptr[i].gpio);

			/*init work*/
			INIT_DELAYED_WORK(&ptdet_ev_ptr[i].event_work, ptdet_notify_work_fn);

			/*request irq*/
			if( request_threaded_irq(ptdet_ev_ptr[i].irq, NULL, ptdet_irq_handler, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
						ptdet_ev_ptr[i].name, ptdet_data_ptr) < 0)
			{
				pr_err("%s: could not request irq for %s (gpio %d)\n", __func__, ptdet_ev_ptr[i].name,  ptdet_ev_ptr[i].gpio);
				ptdet_ev_ptr[i].irq = -1;
				goto fail;
			}
		}
	}

	return 0;

fail:
	ptdet_input_deinit(ptdet_data_ptr);

	return -1;
}

static int ptdet_probe(struct platform_device *pdev)
{
	//int ret;

	dev_info(&pdev->dev, "%s start\n", __func__);

	oem_ptdet = devm_kzalloc(&pdev->dev, sizeof(struct ptdet_data), GFP_KERNEL);

	if(oem_ptdet == NULL) {
		dev_info(&pdev->dev, "%s: devm_kzalloc failed.\n", __func__);
		return -ENOMEM;
	}

	oem_ptdet->pdev = pdev;
	oem_ptdet->dev = &pdev->dev;
	oem_ptdet->pw_event = ptdet_io_map;
	pdev->dev.driver_data = oem_ptdet;

	/*init input debounce time*/
	if( of_property_read_u32(oem_ptdet->dev->of_node, "ptdet-in-debounce", &oem_ptdet->debounce_time))
	{
		oem_ptdet->debounce_time = 10;
		dev_err(&pdev->dev, "%s: ptdet-in-debounce not found, default debounce time: %dms\n", __func__, oem_ptdet->debounce_time);
	}
	dev_info(&pdev->dev, "%s: ptdet io debounce time: %dms\n", __func__, oem_ptdet->debounce_time);

	/*init mutex and work*/
	mutex_init(&oem_ptdet->event_lock);

	/*init input io*/
	if( ptdet_input_init(oem_ptdet) < 0)
	{
		dev_err(&pdev->dev, "%s: init input io failed\n", __func__);
		goto fail_input;
	}

	/*create sysfs*/
	device_create_file(&pdev->dev, &ac_status);
	device_create_file(&pdev->dev, &poe_status);

	dev_info(&pdev->dev, "%s success\n", __func__);
	return 0;

fail_input:

	return -EIO;
}

static int ptdet_remove(struct platform_device *pdev)
{
	struct ptdet_data *ptdet_data_ptr = dev_get_drvdata(&pdev->dev);

	dev_info(&pdev->dev, "%s\n", __func__);

	if( ptdet_data_ptr == NULL)
	{
		dev_info(&pdev->dev, "%s driver data is null\n", __func__);
		return 0;
	}
	//free input io
	ptdet_input_deinit(ptdet_data_ptr);

	return 0;
}

static void ptdet_shutdown(struct platform_device *pdev)
{
	struct ptdet_data *ptdet_data_ptr = dev_get_drvdata(&pdev->dev);

	dev_dbg(&pdev->dev, "%s\n", __func__);

	if( ptdet_data_ptr == NULL)
	{
		dev_err(&pdev->dev, "%s driver data is null\n", __func__);
		return;
	}
	ptdet_input_deinit(ptdet_data_ptr);
}

#ifdef CONFIG_OF
static const struct of_device_id ptdet_dt_match[] = {
	{ .compatible = "oem,ptdet" },
	{ },
};
#else
#define ptdet_dt_match NULL
#endif

static struct platform_driver ptdet_driver = {
	.driver = {
		.name = "oem_ptdet",
		.of_match_table = ptdet_dt_match,
	},
		.probe = ptdet_probe,
		.remove = ptdet_remove,
		.shutdown = ptdet_shutdown,
};

module_driver(ptdet_driver, platform_driver_register,
	platform_driver_unregister);

MODULE_DESCRIPTION("oem power type detect driver");
MODULE_LICENSE("GPL v2");
