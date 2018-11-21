/*
 * oem_extio.c - external IO driver
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
#include "../gpio/gpiolib.h"

struct extio_ind_data {
	const char *of_name;
	const char *name;
	const char *dir_name;
	int gpio;
	int irq;
	int status;
	struct device_attribute io_state;
	struct device_attribute io_direction;
	struct delayed_work event_work;
};

static struct extio_ind_data extio_in_map[] = {
	{ "extio1-1v8-in", "extio1_value", "extio1_direction", -1, -1, -1},
	{ "extio2-1v8-in", "extio2_value", "extio2_direction", -1, -1, -1},
	{ NULL,           NULL,             NULL, -1, -1, -1},
};

struct extio_data {
	struct platform_device *pdev;
	struct device *dev;

	struct extio_ind_data * extio_in;
	int extio3_out_en;
	unsigned int debounce_time;
	struct mutex event_lock;
	struct mutex attribute_lock;
};


static struct extio_data* gp_extio = NULL;
static irqreturn_t extio_irq_handler(int irq, void *dev_data);

static ssize_t
extio_io_state_show(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	struct extio_data* oem_extio = dev->driver_data;
	int i;
	ssize_t			status = -EINVAL;;

	if( oem_extio == NULL)
	{
		pr_err("%s: oem extio data is NULL\n", __func__);
		return -EBUSY;
	}

	mutex_lock(&oem_extio->attribute_lock);
	for (i = 0; oem_extio->extio_in[i].name; i++) {
		if(!strcmp(oem_extio->extio_in[i].name, attr->attr.name)) {
			pr_info("%s: gpio number: %d\n", __func__, oem_extio->extio_in[i].gpio);
			status = snprintf(buf, PAGE_SIZE, "%d\n", !!gpio_get_value_cansleep(oem_extio->extio_in[i].gpio));
			break;
		}
	}

	mutex_unlock(&oem_extio->attribute_lock);
	return status;
}

static int extio_check_dir(const struct gpio_desc* desc)
{
	gpiod_get_direction(desc);
	if( test_bit(FLAG_IS_OUT, &desc->flags))
		return GPIOF_DIR_OUT;
	else
		return GPIOF_DIR_IN;
}
static ssize_t extio_io_state_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct extio_data* oem_extio = dev->driver_data;
	int t_gpio = -1, i;
	const struct gpio_desc	*desc = NULL;
	ssize_t status;
	char *end;
	unsigned int set = simple_strtoul(buf, &end, 0);

	if (end == buf || (set != 0 && set != 1)) {
		return -EINVAL;
	}

	if( oem_extio == NULL)
	{
		pr_err("%s: oem extio data is NULL\n", __func__);
		return -EBUSY;
	}

	mutex_lock(&oem_extio->attribute_lock);
	for (i = 0; oem_extio->extio_in[i].name; i++) {
		if(!strcmp(oem_extio->extio_in[i].name, attr->attr.name)) {
			pr_info("%s: gpio number: %d\n", __func__, oem_extio->extio_in[i].gpio);
			t_gpio = oem_extio->extio_in[i].gpio;
			desc = gpio_to_desc(oem_extio->extio_in[i].gpio);
			break;
		}
	}

	if ( t_gpio < 0 || desc == NULL )
		status = -EIO;
	else if (extio_check_dir(desc)!= GPIOF_DIR_OUT)
	{
		status = -EINVAL;
	}
	else
	{
		status = gpio_direction_output(t_gpio, set);
	}

	mutex_unlock(&oem_extio->attribute_lock);
	return status ? : size;
}

static ssize_t extio_direction_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct extio_data* oem_extio = dev->driver_data;
	const struct gpio_desc	*desc = NULL;
	ssize_t status;
	int i;

	if( oem_extio == NULL)
	{
		pr_err("%s: oem extio data is NULL\n", __func__);
		return -EBUSY;
	}

	mutex_lock(&oem_extio->attribute_lock);
	for (i = 0; oem_extio->extio_in[i].dir_name; i++) {
		if(!strcmp(oem_extio->extio_in[i].dir_name, attr->attr.name)) {
			pr_info("%s: gpio number: %d\n", __func__, oem_extio->extio_in[i].gpio);
			desc = gpio_to_desc(oem_extio->extio_in[i].gpio);
			break;
		}
	}

	if (desc == NULL ) {
		status = -EIO;
	} else {
		gpiod_get_direction(desc);
		status = sprintf(buf, "%s\n",  test_bit(FLAG_IS_OUT, &desc->flags) ? "out" : "in");
	}

	mutex_unlock(&oem_extio->attribute_lock);
	return status;
}

static ssize_t extio_direction_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct extio_data* oem_extio = dev->driver_data;
	int t_gpio = -1;
	ssize_t			status;
	int  i;

	if( oem_extio == NULL)
	{
		pr_err("%s: oem extio data is NULL\n", __func__);
		return -EBUSY;
	}

	mutex_lock(&oem_extio->attribute_lock);
	for (i = 0; oem_extio->extio_in[i].dir_name; i++) {
		if(!strcmp(oem_extio->extio_in[i].dir_name, attr->attr.name)) {
			pr_info("%s: gpio number: %d\n", __func__, oem_extio->extio_in[i].gpio);
			t_gpio = oem_extio->extio_in[i].gpio;
			break;
		}
	}

	if ( t_gpio < 0 )
		status = -EIO;
	else if (sysfs_streq(buf, "high"))
	{
		if( oem_extio->extio_in[i].irq > 0 )
		{
			disable_irq_nosync(oem_extio->extio_in[i].irq);
			free_irq(oem_extio->extio_in[i].irq, oem_extio);
			cancel_delayed_work_sync(&oem_extio->extio_in[i].event_work);
			oem_extio->extio_in[i].irq  = -1;
		}
		status = gpio_direction_output(t_gpio, 1);
	}
	else if (sysfs_streq(buf, "out") || sysfs_streq(buf, "low"))
	{
		if( oem_extio->extio_in[i].irq > 0 )
		{
			disable_irq_nosync(oem_extio->extio_in[i].irq);
			free_irq(oem_extio->extio_in[i].irq, oem_extio);
			cancel_delayed_work_sync(&oem_extio->extio_in[i].event_work);
			oem_extio->extio_in[i].irq  = -1;
		}
		status = gpio_direction_output(t_gpio, 0);
	}
	else if (sysfs_streq(buf, "in"))
	{
		status = gpio_direction_input(t_gpio);
		if( !status && oem_extio->extio_in[i].irq < 0 )
		{
			oem_extio->extio_in[i].irq = gpio_to_irq(oem_extio->extio_in[i].gpio);
			if( request_threaded_irq(oem_extio->extio_in[i].irq, NULL, extio_irq_handler, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				oem_extio->extio_in[i].name, oem_extio) < 0)
			{
				pr_err("%s: could not request irq for %s (gpio %d)\n", __func__, oem_extio->extio_in[i].name,  oem_extio->extio_in[i].gpio);
				oem_extio->extio_in[i].irq = -1;
					status =  -EBUSY;
			}
		}
	}
	else
		status = -EINVAL;

	mutex_unlock(&oem_extio->attribute_lock);
	return status ? : size;
}


static ssize_t
extio_get_out_io_status(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	struct extio_data* oem_extio = dev->driver_data;

	if( oem_extio == NULL)
	{
		pr_err("%s: oem extio data is NULL\n", __func__);
		return -EBUSY;
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", !!gpio_get_value_cansleep(oem_extio->extio3_out_en));
}

static ssize_t
extio_set_out_enable(struct device *dev,
			struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = 0;
	struct extio_data* oem_extio = dev->driver_data;
	char *end;
	unsigned long set = simple_strtoul(buf, &end, 0);

	if (end == buf || (set != 0 && set != 1)) {
		ret = -EINVAL;
		goto out;
	}

	pr_info("%s: set out enable to %ld\n", __func__, set);

	if(oem_extio == NULL)
	{
		pr_err("%s: oem_extio device still not init\n", __func__);
		return -EBUSY;
	}

	gpio_direction_output(oem_extio->extio3_out_en, set);
	ret = count;

out:
	return ret;
}

static struct device_attribute extio_out_en =
	__ATTR(extio_out_en, S_IWUSR|S_IWGRP|S_IRUGO,  extio_get_out_io_status, extio_set_out_enable);


static void extio_notify_work_fn(struct work_struct *work)
{
	char *envp[2];
	char devpath_string[32]= {0} ;
	int value = 0;
	struct delayed_work *dwork = to_delayed_work(work);
	struct extio_ind_data *extio_in_ptr = container_of(dwork, struct extio_ind_data, event_work);

	if(gp_extio == NULL || extio_in_ptr == NULL)
	{
		pr_err("%s: oem_extio device still not init\n", __func__);
		return;
	}

	mutex_lock(&gp_extio->event_lock);
	value = !!gpio_get_value_cansleep(extio_in_ptr->gpio);
	pr_info("%s: %s value is :%d\n", __func__, extio_in_ptr->name, value);

	if( extio_in_ptr->status != value)
	{
		extio_in_ptr->status = value;
		if (value==0)
			sprintf(devpath_string, "%s_STATUS=low", extio_in_ptr->name);
		else
			sprintf(devpath_string, "%s_STATUS=high", extio_in_ptr->name);
		envp[0]= devpath_string;
		envp[1]=NULL;

		pr_debug("%s, GPIO%d = %d\n", __func__, extio_in_ptr->gpio, value);

		kobject_uevent_env(&gp_extio->dev->kobj, KOBJ_CHANGE, envp);
	}
	else
	{
		pr_err("%s: Pseudo interrupt for %s, current status is %d\n", __func__, extio_in_ptr->name, extio_in_ptr->status);
	}
	mutex_unlock(&gp_extio->event_lock);

	return;
}

static irqreturn_t extio_irq_handler(int irq, void *dev_data)
{
	struct extio_data* extio_ptr = (struct extio_data*)dev_data;
	struct device *dev;
	int i;
	struct extio_ind_data *extio_in;

	if (!extio_ptr)
		return IRQ_HANDLED;

	dev = extio_ptr->dev;
	dev_dbg(dev, "%s: irq %d\n", __func__, irq);

	extio_in = extio_ptr->extio_in;

	for (i = 0; extio_in[i].of_name; i++) {
		if(irq == extio_in[i].irq) {
			schedule_delayed_work(&extio_in[i].event_work, msecs_to_jiffies(extio_ptr->debounce_time));
			break;
		}
	}
	dev_dbg(dev, "%s: irq %d handled\n", __func__, irq);

	return IRQ_HANDLED;
}

static void extio_input_deinit(struct extio_data* extio_data_ptr)
{
	int i;
	struct extio_ind_data* extio_in_ptr = NULL;

	extio_in_ptr = extio_data_ptr->extio_in;
	if( extio_in_ptr == NULL)
	{
		pr_err("%s: input io data pointer is NULL\n", __func__);
		return ;
	}

	for( i = 0; extio_in_ptr[i].of_name; i++)
	{
		if( extio_in_ptr[i].io_state.attr.name == extio_in_ptr[i].name)
		{
			device_remove_file(extio_data_ptr->dev, &extio_in_ptr[i].io_state);
		}
		if( extio_in_ptr[i].io_state.attr.name == extio_in_ptr[i].dir_name)
		{
			device_remove_file(extio_data_ptr->dev, &extio_in_ptr[i].io_direction);
		}

		if( extio_in_ptr[i].irq > 0 )
		{
			disable_irq_nosync(extio_in_ptr[i].irq);
			free_irq(extio_in_ptr[i].irq, extio_data_ptr);
			cancel_delayed_work_sync(&extio_in_ptr[i].event_work);
			extio_in_ptr[i].irq = -1;
		}

		if ( extio_in_ptr[i].gpio > 0)
		{
			gpio_unexport(extio_in_ptr[i].gpio);
			gpio_free(extio_in_ptr[i].gpio);
		}
	}
}

static int extio_input_init(struct extio_data* extio_data_ptr)
{
	int i, ret;
	struct device_node *node = extio_data_ptr->dev->of_node;
	struct extio_ind_data* extio_in_ptr = NULL;

	extio_in_ptr = extio_data_ptr->extio_in;

	if( extio_in_ptr == NULL)
	{
		pr_err("%s: input io data pointer is NULL\n", __func__);
		return -1;
	}

	for( i = 0; extio_in_ptr[i].of_name; i++)
	{
		extio_in_ptr[i].gpio = of_get_named_gpio(node, extio_in_ptr[i].of_name, 0);

		if ( extio_in_ptr[i].gpio <= 0 || !gpio_is_valid(extio_in_ptr[i].gpio) ) {
			pr_err("%s: extio%d-1v8-in property not found %d or not valid\n", __func__, i, extio_in_ptr[i].gpio);
			continue;
		}

		pr_info("%s: (%s)request %d\n", __func__, extio_in_ptr[i].name, extio_in_ptr[i].gpio);

		ret = gpio_request(extio_in_ptr[i].gpio, extio_in_ptr[i].name);
		if (ret) {
			pr_err("%s could not acquire enable gpio (err=%d)\n", extio_in_ptr[i].name, ret);
			extio_in_ptr[i].gpio = -1;
			goto fail;
		}
		else
		{
			gpio_direction_input(extio_in_ptr[i].gpio);
			//gpio_export(extio_in_ptr[i].gpio, true);
			extio_in_ptr[i].irq = gpio_to_irq(extio_in_ptr[i].gpio);
			if( extio_in_ptr[i].irq < 0)
			{
				pr_err("%s: could not map irq for %s (gpio %d)\n", __func__, extio_in_ptr[i].name,  extio_in_ptr[i].gpio);
				goto fail;
			}

			extio_in_ptr[i].status = !!gpio_get_value_cansleep(extio_in_ptr[i].gpio);

			/*init work*/
			INIT_DELAYED_WORK(&extio_in_ptr[i].event_work, extio_notify_work_fn);
			/*init sysfs*/
			extio_in_ptr[i].io_state.show = extio_io_state_show;
			extio_in_ptr[i].io_state.store = extio_io_state_store;
			sysfs_attr_init(&extio_in_ptr[i].io_state.attr);
			extio_in_ptr[i].io_state.attr.name = extio_in_ptr[i].name;
			extio_in_ptr[i].io_state.attr.mode = S_IWUSR|S_IWGRP|S_IRUGO;
			if( device_create_file(extio_data_ptr->dev, &extio_in_ptr[i].io_state))
			{
				pr_err("%s: could not create io state sysfs for %s\n", __func__, extio_in_ptr[i].name);
				goto fail;
			}

			extio_in_ptr[i].io_direction.show = extio_direction_show;
			extio_in_ptr[i].io_direction.store = extio_direction_store;
			sysfs_attr_init(&extio_in_ptr[i].io_direction.attr);
			extio_in_ptr[i].io_direction.attr.name = extio_in_ptr[i].dir_name;
			extio_in_ptr[i].io_direction.attr.mode = S_IWUSR|S_IWGRP|S_IRUGO;
			if( device_create_file(extio_data_ptr->dev, &extio_in_ptr[i].io_direction))
			{
				pr_err("%s: could not create io direction sysfs for %s\n", __func__, extio_in_ptr[i].name);
				goto fail;
			}

			/*request irq*/
			if( request_threaded_irq(extio_in_ptr[i].irq, NULL, extio_irq_handler, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
						extio_in_ptr[i].name, extio_data_ptr) < 0)
			{
				pr_err("%s: could not request irq for %s (gpio %d)\n", __func__, extio_in_ptr[i].name,  extio_in_ptr[i].gpio);
				extio_in_ptr[i].irq = -1;
				goto fail;
			}
		}
	}

	return 0;

fail:
	extio_input_deinit(extio_data_ptr);

	return -1;
}

static int extio_probe(struct platform_device *pdev)
{
	int ret;

	dev_info(&pdev->dev, "%s start\n", __func__);

	gp_extio = devm_kzalloc(&pdev->dev, sizeof(struct extio_data), GFP_KERNEL);

	if(gp_extio == NULL) {
		dev_info(&pdev->dev, "%s: devm_kzalloc failed.\n", __func__);
		return -ENOMEM;
	}

	gp_extio->pdev = pdev;
	gp_extio->dev = &pdev->dev;
	gp_extio->extio_in = extio_in_map;
	pdev->dev.driver_data = gp_extio;

	/*init output io*/
	gp_extio->extio3_out_en = of_get_named_gpio(gp_extio->dev->of_node, "extio3-out-en", 0);

	if ( gp_extio->extio3_out_en <= 0 || !gpio_is_valid(gp_extio->extio3_out_en) ) {
		dev_err(&pdev->dev, "%s: %s property not found %d or not valid\n", __func__, "extio3_out_en", gp_extio->extio3_out_en);
	}

	dev_info(&pdev->dev, "%s: extio3_out_en request %d\n", __func__, gp_extio->extio3_out_en);

	ret = gpio_request(gp_extio->extio3_out_en, "extio3_out_en");
	if (ret) {
		dev_err(&pdev->dev, "extio3_out_en could NOT acquire enable gpio (err=%d)\n", ret);
		return -EBUSY;
	}
	else
	{
		gpio_direction_output(gp_extio->extio3_out_en, 1);
		//gpio_export(gp_extio->extio3_out_en, true);
	}
	/*create out enable sysfs*/
	device_create_file(gp_extio->dev, &extio_out_en);

	/*init input debounce time*/
	if( of_property_read_u32(gp_extio->dev->of_node, "extio-in-debounce", &gp_extio->debounce_time))
	{
		gp_extio->debounce_time = 50;
		dev_err(&pdev->dev, "%s: extio-in-debounce not found, default debounce time: %dms\n", __func__, gp_extio->debounce_time);
	}
	dev_info(&pdev->dev, "%s: extio input debounce time: %dms\n", __func__, gp_extio->debounce_time);

	/*init mutex and work*/
	mutex_init(&gp_extio->event_lock);
	mutex_init(&gp_extio->attribute_lock);

	/*init input io*/
	if( extio_input_init(gp_extio) < 0)
	{
		dev_err(&pdev->dev, "%s: init input io failed\n", __func__);
		goto fail_input;
	}

	dev_info(&pdev->dev, "%s success\n", __func__);
	return 0;

fail_input:
	gpio_unexport(gp_extio->extio3_out_en);
	gpio_free(gp_extio->extio3_out_en);

	return -EIO;
}

static int extio_remove(struct platform_device *pdev)
{
	struct extio_data *extio_data_ptr = dev_get_drvdata(&pdev->dev);

	dev_info(&pdev->dev, "%s\n", __func__);

	if( extio_data_ptr == NULL)
	{
		dev_info(&pdev->dev, "%s driver data is null\n", __func__);
		return 0;
	}
	//free input io
	extio_input_deinit(extio_data_ptr);

	//free output io
	device_remove_file(&pdev->dev, &extio_out_en);
	gpio_unexport(extio_data_ptr->extio3_out_en);
	gpio_free(extio_data_ptr->extio3_out_en);

	return 0;
}

static void extio_shutdown(struct platform_device *pdev)
{
	struct extio_data *extio_data_ptr = dev_get_drvdata(&pdev->dev);

	dev_info(&pdev->dev, "%s\n", __func__);

	if( extio_data_ptr == NULL)
	{
		dev_info(&pdev->dev, "%s driver data is null\n", __func__);
		return;
	}
	//free input io
	extio_input_deinit(extio_data_ptr);

	//free output io
	device_remove_file(&pdev->dev, &extio_out_en);
	gpio_unexport(extio_data_ptr->extio3_out_en);
	gpio_free(extio_data_ptr->extio3_out_en);
}

#ifdef CONFIG_OF
static const struct of_device_id extio_dt_match[] = {
	{ .compatible = "oem,extio" },
	{ },
};
#else
#define extio_dt_match NULL
#endif

static struct platform_driver extio_driver = {
	.driver = {
		.name = "oem_extio",
		.of_match_table = extio_dt_match,
	},
		.probe = extio_probe,
		.remove = extio_remove,
		.shutdown = extio_shutdown,
};

module_driver(extio_driver, platform_driver_register,
	platform_driver_unregister);

MODULE_DESCRIPTION("oem external IO driver");
MODULE_LICENSE("GPL v2");
