/*
 * BQ27xxx battery monitor I2C driver
 *
 * Copyright (C) 2015 Texas Instruments Incorporated - http://www.ti.com/
 *	Andrew F. Davis <afd@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <asm/unaligned.h>
#include <linux/delay.h>
#include <linux/debugfs.h>

#include <linux/power/bq27xxx_battery.h>

static DEFINE_IDR(battery_id);
static DEFINE_MUTEX(battery_mutex);

static irqreturn_t bq27xxx_battery_irq_handler_thread(int irq, void *data)
{
	struct bq27xxx_device_info *di = data;

	bq27xxx_battery_update(di);

	return IRQ_HANDLED;
}

int bq27xxx_write_two_bytes(struct bq27xxx_device_info *di, u8 cmd, u8 data0, u8 data1)
{
	struct i2c_client *client = to_i2c_client(di->dev);
	struct i2c_msg msg[1];
	unsigned char data[3];
	int ret;

	if (!client->adapter)
		return -ENODEV;

	data[0] = cmd;
	data[1] = data0;
	data[2] = data1;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].buf = data;
	msg[0].len = sizeof(data);

	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	return ret;
}

static int bq27xxx_battery_i2c_read(struct bq27xxx_device_info *di, u8 reg,
				    bool single)
{
	struct i2c_client *client = to_i2c_client(di->dev);
	struct i2c_msg msg[2];
	unsigned char data[2];
	int ret;

	if (!client->adapter)
		return -ENODEV;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].buf = &reg;
	msg[0].len = sizeof(reg);
	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = data;
	if (single)
		msg[1].len = 1;
	else
		msg[1].len = 2;

	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (ret < 0)
		return ret;

	if (!single)
		ret = get_unaligned_le16(data);
	else
		ret = data[0];

	return ret;
}

static ssize_t bq27xxx_set_enter_rom(struct device *dev, struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct bq27xxx_device_info *di = dev_get_drvdata(dev);

	printk(KERN_ERR "%s: enter ROM\n", __func__);

	/* these four are copied from golden .bq.fs, donno what they do */
	bq27xxx_write_two_bytes(di, 0x00, 0x14, 0x04);
	bq27xxx_write_two_bytes(di, 0x00, 0x72, 0x36);
	bq27xxx_write_two_bytes(di, 0x00, 0xff, 0xff);
	bq27xxx_write_two_bytes(di, 0x00, 0xff, 0xff);
	msleep(1000);
	/* goto ROM mode */
	bq27xxx_write_two_bytes(di, 0x00, 0x00, 0x0f);
	msleep(1000);

	return count;
}

static DEVICE_ATTR(enter_rom, S_IWUSR, NULL, bq27xxx_set_enter_rom);

static ssize_t bq27xxx_set_exit_rom(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct bq27xxx_device_info *di = dev_get_drvdata(dev);

	printk(KERN_ERR "%s: exit ROM\n", __func__);

	/* reset */
	bq27xxx_write_two_bytes(di, 0x00, 0x41, 0x00);
	msleep(1000);
	/* IT enable */
	bq27xxx_write_two_bytes(di, 0x00, 0x21, 0x00);
	msleep(1000);

	return count;
}

static DEVICE_ATTR(exit_rom, S_IWUSR, NULL, bq27xxx_set_exit_rom);

static ssize_t bq27xxx_get_df_version(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct bq27xxx_device_info *di = dev_get_drvdata(dev);
	int ret;

	printk(KERN_ERR "%s: DF VERSION\n", __func__);

	bq27xxx_write_two_bytes(di, 0x00, 0x1f, 0x00);
	ret = bq27xxx_battery_i2c_read(di, 0x00, 0);
	if (ret < 0)
		ret = 0;
	return sprintf(buf, "%04x\n", ret);
}

static DEVICE_ATTR(df_version, S_IRUSR, bq27xxx_get_df_version, NULL);

static ssize_t bq27xxx_set_bat_insert(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct bq27xxx_device_info *di = dev_get_drvdata(dev);

	printk(KERN_ERR "%s: battery insert\n", __func__);

	cancel_delayed_work_sync(&di->work);

	/* battery insert */
	bq27xxx_write_two_bytes(di, 0x00, 0x0d, 0x00);

	/* restart polling */
	di->inserted = 1;
	schedule_delayed_work(&di->work, HZ * 2);

	return count;
}

static DEVICE_ATTR(bat_insert, S_IWUSR, NULL, bq27xxx_set_bat_insert);

static ssize_t bq27xxx_set_bat_remove(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct bq27xxx_device_info *di = dev_get_drvdata(dev);

	printk(KERN_ERR "%s: battery remove\n", __func__);

	cancel_delayed_work_sync(&di->work);

	/* battery remove */
	bq27xxx_write_two_bytes(di, 0x00, 0x0e, 0x00);

	/* restart polling */
	di->inserted = 0;
	schedule_delayed_work(&di->work, HZ * 2);


	return count;
}

static DEVICE_ATTR(bat_remove, S_IWUSR, NULL, bq27xxx_set_bat_remove);



static struct attribute *bq27xxx_sysfs_attributes[] = {
	&dev_attr_enter_rom.attr,
	&dev_attr_exit_rom.attr,
	&dev_attr_df_version.attr,
	&dev_attr_bat_insert.attr,
	&dev_attr_bat_remove.attr,
	NULL,
};

static const struct attribute_group bq27xxx_sysfs_attr_group = {
	.attrs = bq27xxx_sysfs_attributes,
};

static int bq_show_registers(struct seq_file *m, void *data)
{
	struct bq27xxx_device_info *di = m->private;
	int reg;
	u8 addr;

	bq27xxx_write_two_bytes(di, 0x00, 0x00, 0x00);
	reg = bq27xxx_battery_i2c_read(di, 0x00, 0);
	seq_printf(m, "CONTROL_STATUS = 0x%04x\n", reg);

	for (addr = 0x02; addr < 0x30; addr += 2) {
		reg = bq27xxx_battery_i2c_read(di, addr, false);
		if (reg >= 0)
			seq_printf(m, "0x%02x = 0x%04x\n", addr, reg);
	}

	return 0;
}

static int bq_debugfs_open(struct inode *inode, struct file *file)
{
	struct bq27xxx_device_info *di = inode->i_private;

        return single_open(file, bq_show_registers, di);
}

static const struct file_operations bq_debugfs_ops = {
	.owner		= THIS_MODULE,
	.open		= bq_debugfs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int bq27xxx_battery_i2c_probe(struct i2c_client *client,
				     const struct i2c_device_id *id)
{
	struct bq27xxx_device_info *di;
	int ret = -ENOMEM;
	char *name;
	int num;
	struct dentry *ent;

	/* Get new ID for the new battery device */
	mutex_lock(&battery_mutex);
	num = idr_alloc(&battery_id, client, 0, 0, GFP_KERNEL);
	mutex_unlock(&battery_mutex);
	if (num < 0)
		return num;

	name = devm_kasprintf(&client->dev, GFP_KERNEL, "%s-battery", id->name);
	if (!name)
		goto err;

	di = devm_kzalloc(&client->dev, sizeof(*di), GFP_KERNEL);
	if (!di)
		goto err;

	di->id = num;
	di->dev = &client->dev;
	di->chip = id->driver_data;
	di->name = name;
	di->bus.read = bq27xxx_battery_i2c_read;

	ret = bq27xxx_battery_setup(di);
	if (ret)
		goto err;

	/* Schedule a polling after about 1 min */
	schedule_delayed_work(&di->work, 60 * HZ);

	i2c_set_clientdata(client, di);

	if (client->irq) {
		ret = devm_request_threaded_irq(&client->dev, client->irq,
				NULL, bq27xxx_battery_irq_handler_thread,
				IRQF_ONESHOT,
				di->name, di);
		if (ret) {
			dev_err(&client->dev,
				"Unable to register IRQ %d error %d\n",
				client->irq, ret);
			goto err;
		}
	}

	ret = sysfs_create_group(&di->dev->kobj, &bq27xxx_sysfs_attr_group);
	if (ret)
		goto err;

	di->debug_root = debugfs_create_dir(di->name, NULL);
	if (!di->debug_root) {
		dev_err(&client->dev, "Failed to create debugfs directory\n");
		goto err_sysfs;
	}

	ent = debugfs_create_file("registers", S_IFREG | S_IRUGO, di->debug_root, di, &bq_debugfs_ops);
	if (!ent) {
		dev_err(&client->dev, "Couldn't create registers debug file\n");
		goto err_debug;
	}

	return 0;

err_debug:
	debugfs_remove_recursive(di->debug_root);
err_sysfs:
	sysfs_remove_group(&di->dev->kobj, &bq27xxx_sysfs_attr_group);
err:
	mutex_lock(&battery_mutex);
	idr_remove(&battery_id, num);
	mutex_unlock(&battery_mutex);

	return ret;
}

static int bq27xxx_battery_i2c_remove(struct i2c_client *client)
{
	struct bq27xxx_device_info *di = i2c_get_clientdata(client);

	bq27xxx_battery_teardown(di);

	mutex_lock(&battery_mutex);
	idr_remove(&battery_id, di->id);
	mutex_unlock(&battery_mutex);
	sysfs_remove_group(&di->dev->kobj, &bq27xxx_sysfs_attr_group);
	debugfs_remove_recursive(di->debug_root);

	return 0;
}

static const struct i2c_device_id bq27xxx_i2c_id_table[] = {
	{ "bq27200", BQ27000 },
	{ "bq27210", BQ27010 },
	{ "bq27500", BQ27500 },
	{ "bq27510", BQ27510 },
	{ "bq27520", BQ27510 },
	{ "bq27530", BQ27530 },
	{ "bq27531", BQ27530 },
	{ "bq27541", BQ27541 },
	{ "bq27542", BQ27541 },
	{ "bq27546", BQ27541 },
	{ "bq27742", BQ27541 },
	{ "bq27545", BQ27545 },
	{ "bq27421", BQ27421 },
	{ "bq27425", BQ27421 },
	{ "bq27441", BQ27421 },
	{ "bq27621", BQ27421 },
	{},
};
MODULE_DEVICE_TABLE(i2c, bq27xxx_i2c_id_table);

#ifdef CONFIG_OF
static const struct of_device_id bq27xxx_battery_i2c_of_match_table[] = {
	{ .compatible = "ti,bq27200" },
	{ .compatible = "ti,bq27210" },
	{ .compatible = "ti,bq27500" },
	{ .compatible = "ti,bq27510" },
	{ .compatible = "ti,bq27520" },
	{ .compatible = "ti,bq27530" },
	{ .compatible = "ti,bq27531" },
	{ .compatible = "ti,bq27541" },
	{ .compatible = "ti,bq27542" },
	{ .compatible = "ti,bq27546" },
	{ .compatible = "ti,bq27742" },
	{ .compatible = "ti,bq27545" },
	{ .compatible = "ti,bq27421" },
	{ .compatible = "ti,bq27425" },
	{ .compatible = "ti,bq27441" },
	{ .compatible = "ti,bq27621" },
	{},
};
MODULE_DEVICE_TABLE(of, bq27xxx_battery_i2c_of_match_table);
#endif

static struct i2c_driver bq27xxx_battery_i2c_driver = {
	.driver = {
		.name = "bq27xxx-battery",
		.of_match_table = of_match_ptr(bq27xxx_battery_i2c_of_match_table),
	},
	.probe = bq27xxx_battery_i2c_probe,
	.remove = bq27xxx_battery_i2c_remove,
	.id_table = bq27xxx_i2c_id_table,
};
module_i2c_driver(bq27xxx_battery_i2c_driver);

MODULE_AUTHOR("Andrew F. Davis <afd@ti.com>");
MODULE_DESCRIPTION("BQ27xxx battery monitor i2c driver");
MODULE_LICENSE("GPL");
