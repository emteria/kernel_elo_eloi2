/* Copyright (c) 2013-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/of_platform.h>
#include <linux/interrupt.h>
#include <linux/power_supply.h>
#include <linux/regulator/consumer.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/workqueue.h>
#include <soc/qcom/socinfo.h>
#include <linux/qpnp-smbcharger.h>

struct gpio_usbdetect {
    struct platform_device *pdev;
    struct power_supply    *usb_psy;
    int    vbus_det_irq;
    int    gpio_irq;
};

static struct gpio_usbdetect *usb = NULL;
static struct power_supply *usb_psy = NULL;
static struct work_struct my_work;

static irqreturn_t gpio_usbdetect_vbus_irq(int irq, void *data)
{
    pr_info("USB gpio_usbdetect_vbus_irq\n");

    if( is_usb_device_mode() )
        schedule_work(&my_work);
    return IRQ_HANDLED;
}

static void gpio_usbdetect_first_check(void)
{
    int vbus;

    if( usb == NULL)
    {
        pr_err("%s: gpio usb detect struct still not init\n", __func__);
        return;
    }

    if( is_usb_device_mode() )
    {
        vbus = gpio_get_value_cansleep(usb->gpio_irq);
        pr_info("%s: vbus = %d\n", __func__, vbus);

        #ifdef CONFIG_FACTORY_IMAGE
        /*work aroud, set USB default status to present for Factory Image*/
        vbus = 1;
        pr_info("%s: Factory image, set vbus status to high: %d\n", __func__, vbus);
        #endif

        if (vbus) //only set usb state when gpio is high: for early version board, gpio is always low.
        {
            power_supply_set_supply_type(usb->usb_psy,
                POWER_SUPPLY_TYPE_USB);
            power_supply_set_present(usb->usb_psy, vbus);
        }
    }
}

static void my_work_fn(struct work_struct *work)
{
    int vbus;

    if( usb == NULL)
    {
        pr_err("%s: gpio usb detect struct still not init\n", __func__);
        return;
    }
    vbus = gpio_get_value_cansleep(usb->gpio_irq);
    pr_info("%s: vbus = %d\n", __func__, vbus);

    #ifdef CONFIG_FACTORY_IMAGE
    /*work aroud, set USB default status to present for Factory Image*/
    vbus = 1;
    pr_info("%s: Factory image, set vbus status to high: %d\n", __func__, vbus);
    #endif

    if (vbus)
        power_supply_set_supply_type(usb->usb_psy,
                POWER_SUPPLY_TYPE_USB);
    else
        power_supply_set_supply_type(usb->usb_psy,
                POWER_SUPPLY_TYPE_UNKNOWN);

    power_supply_set_present(usb->usb_psy, vbus);
}

static int gpio_usbdetect_probe(struct platform_device *pdev)
{
    int rc;
    unsigned long flags;

    dev_info(&pdev->dev, "%s\n", __func__);

    usb_psy = power_supply_get_by_name("usb");
    if (!usb_psy) {
        dev_dbg(&pdev->dev, "USB power_supply not found, deferring probe\n");
        return -EPROBE_DEFER;
    }

    usb = devm_kzalloc(&pdev->dev, sizeof(*usb), GFP_KERNEL);
    if (!usb)
        return -ENOMEM;

    usb->pdev = pdev;
    usb->usb_psy = usb_psy;

    INIT_WORK(&my_work, my_work_fn);

    usb->gpio_irq = of_get_named_gpio_flags(pdev->dev.of_node,
                "qcom,irq-gpio", 0, NULL);

    /* STAT irq configuration */
    if (gpio_is_valid(usb->gpio_irq)) {
        rc = gpio_request(usb->gpio_irq, "vbus_det_irq");
        if (rc) {
            dev_err(&pdev->dev,
                    "vbus_det_irq irq gpio request failed, rc=%d", rc);
            goto fail_irq_gpio;
        }

        rc = gpio_direction_input(usb->gpio_irq);
        if (rc) {
            dev_err(&pdev->dev,
                    "set_direction for irq gpio failed\n");
            goto fail_irq_gpio;
        }

        dev_err(&pdev->dev, "detect gpio: %d, value: %d\n", usb->gpio_irq, gpio_get_value_cansleep(usb->gpio_irq));

        usb->vbus_det_irq = gpio_to_irq(usb->gpio_irq);
        if (usb->vbus_det_irq < 0) {
            dev_err(&pdev->dev,
                "Invalid irq_gpio irq = %d\n", usb->vbus_det_irq);
            goto fail_irq_gpio;
        }
        rc = request_irq(usb->vbus_det_irq, gpio_usbdetect_vbus_irq,IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING,
            "vbus_det_irq", &pdev->dev);

        if (rc) {
            dev_err(&pdev->dev, "request for  vbus_det_irq failed: %d\n",
                rc);
            return rc;
        }

        enable_irq_wake(usb->vbus_det_irq);
        dev_set_drvdata(&pdev->dev, usb);
        disable_irq(usb->vbus_det_irq);

        dev_info(&pdev->dev, "USB gpio detect irq enabled\n");
        /*enable detect irq*/
        enable_irq(usb->vbus_det_irq);
        /* Read and report initial VBUS state */
        local_irq_save(flags);
        gpio_usbdetect_first_check();
        local_irq_restore(flags);

        return 0;
    }else
        dev_err(&pdev->dev,
                 "vbus_det_irq gpio invalid failed");

fail_irq_gpio:
    if (gpio_is_valid(usb->gpio_irq))
        gpio_free(usb->gpio_irq);
    return -1;

}

static int gpio_usbdetect_remove(struct platform_device *pdev)
{
    struct gpio_usbdetect *usb = dev_get_drvdata(&pdev->dev);

    disable_irq_wake(usb->vbus_det_irq);
    disable_irq(usb->vbus_det_irq);

    return 0;
}

static struct of_device_id of_match_table[] = {
    { .compatible = "qcom,gpio-usbdetect", },
    {}
};

static struct platform_driver gpio_usbdetect_driver = {
    .driver        = {
        .name    = "qcom,gpio-usbdetect",
        .of_match_table = of_match_table,
    },
    .probe        = gpio_usbdetect_probe,
    .remove        = gpio_usbdetect_remove,
};

module_driver(gpio_usbdetect_driver, platform_driver_register,
        platform_driver_unregister);

MODULE_DESCRIPTION("GPIO USB VBUS Detection driver");
MODULE_LICENSE("GPL v2");

