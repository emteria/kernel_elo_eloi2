#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <soc/qcom/socinfo.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/qpnp/qpnp-adc.h>

#define ACCESSORY_MODNAME       "gpio-accessory"
#define PINCTRL_STATE_ACTIVE    "gpio_accessory_active"
#define NBCR_FEATURE_EN

/* 20170526 JackWLu: I2.0 add external BCR GPIOs (Use USB ID pin for trigger) {*/
#define I20_SUPPORT_EXTERNAL_BCR_TRIGGER
/* 20170526 JackWLu: I2.0 add external BCR GPIOs (Use USB ID pin for trigger) }*/

#ifdef NBCR_FEATURE_EN
enum
{
	CONFIG_DEF, //def_configuration,
	CONFIG_STA, //start_configuartion,
	CONFIG_SET, //set_configuartion,
	CONFIG_END, //end_configuration,
	CONFIG_UNK, //unknown_configuration
};

struct mtimestramp
{
	u64 ts_nsec_last;
	u64 ts_nsec_current;//we can use local_clock() to get current time from power up
};
#endif

typedef struct peripheral_driver_data
{
	struct device *dev;

	/* NUMA BCR*/
	int n_txd_gpio;
	int n_beeper_gpio;
	int n_trigger_gpio;
	int Nbcr_irq;
	unsigned char irq_count;
	struct work_struct Nbcr_irq_work;
	struct input_dev *input;
#ifdef NBCR_FEATURE_EN
	struct mtimestramp Nbcr_tt;
	unsigned char  last_status;
	unsigned char  cur_status;
#endif
	/*NUMA BCR*/

/* 20170526 JackWLu: I2.0 add external BCR GPIOs (Use USB ID pin for trigger) {*/
#ifdef I20_SUPPORT_EXTERNAL_BCR_TRIGGER
	int i20_ext_bcr_trigger_d1;
	int i20_ext_bcr_trigger_d2;
	int i20_ext_bcr_trigger_d3;
	int i20_ext_bcr_trigger_d4;
#endif
/* 20170526 JackWLu: I2.0 add external BCR GPIOs (Use USB ID pin for trigger) }*/

	/*Honeywell BCR */
	int h_beeper_gpio;
	int h_trigger_gpio;
	/*Honeywell BCR */

	/*Cash Drawer*/
	int cash_d_sense; //cash drawer sense driver IO
	int cash_d_detct; //cash drawer detect IO
	int cash_d_solen; //cash drawer solenoid IO
	atomic_t calibrating; //sensor calibration  status
	unsigned long cs_pulse_width; //cash drawer pulse width
	unsigned long cs_low_th; //cash drawer calibration close
	unsigned long cs_high_th; //cash drawer calibration open
	struct qpnp_vadc_chip *vadc_dev; //cash drawer detect ADC
	/*Cash Drawer*/

	/*Printer*/
	int printer_power;
	/*Printer*/
}p_data_driver ;

/*---------------------------Internal NUMA BCR interface-----------------------*/
static irqreturn_t Nbcr_irq_handler(int irq, void *handle)
{
	p_data_driver *data = handle;

	printk(KERN_INFO "%s\n", __func__);
	if (data == NULL)
		return IRQ_HANDLED;

	schedule_work(&data->Nbcr_irq_work);

	return IRQ_HANDLED;
}

static void Nbcr_irq_work_func(struct work_struct *work)
{
#ifdef NBCR_FEATURE_EN
	u64 time_diff = 0;
#endif
	p_data_driver *Nbcr_driver_data = container_of((struct work_struct *)work,
			struct peripheral_driver_data, Nbcr_irq_work);
#ifdef NBCR_FEATURE_EN
	if(!gpio_get_value(Nbcr_driver_data->n_txd_gpio))
	{
		Nbcr_driver_data->Nbcr_tt.ts_nsec_current = local_clock();
		Nbcr_driver_data->Nbcr_tt.ts_nsec_last = Nbcr_driver_data->Nbcr_tt.ts_nsec_current;
		Nbcr_driver_data->irq_count++;
	}
	else if(gpio_get_value(Nbcr_driver_data->n_txd_gpio))
	{
		Nbcr_driver_data->Nbcr_tt.ts_nsec_current = local_clock();
		printk(KERN_INFO "last%llu,Now %llu\n",Nbcr_driver_data->Nbcr_tt.ts_nsec_last,Nbcr_driver_data->Nbcr_tt.ts_nsec_current);
		time_diff = (Nbcr_driver_data->Nbcr_tt.ts_nsec_current - Nbcr_driver_data->Nbcr_tt.ts_nsec_last)/1000000;

		printk(KERN_INFO "%03llu\n",time_diff);

		Nbcr_driver_data->irq_count = 0;
		if(time_diff== 0 ||time_diff < 0)
		{
			printk(KERN_INFO "ignor this irq\n");
			Nbcr_driver_data->last_status = Nbcr_driver_data->cur_status;
			Nbcr_driver_data->cur_status = CONFIG_DEF;
		}
		else if(time_diff >= 300&& time_diff <= 500)//300ms~500ms for start
		{
			printk(KERN_INFO "%s :start configuration \n", __func__);
			Nbcr_driver_data->last_status = Nbcr_driver_data->cur_status;
			Nbcr_driver_data->cur_status = CONFIG_STA;
			//input report key event
			input_report_key(Nbcr_driver_data->input,KEY_F13,1);
			input_sync(Nbcr_driver_data->input);
			input_report_key(Nbcr_driver_data->input,KEY_F13,0);
			input_sync(Nbcr_driver_data->input);
		}
		else if(time_diff >0&& time_diff <= 200)//0ms~200ms for set cfg
		{
			printk(KERN_INFO "%s :set configuration \n",__func__);
			Nbcr_driver_data->last_status = Nbcr_driver_data->cur_status;
			Nbcr_driver_data->cur_status = CONFIG_SET;
			//input report key event
			input_report_key(Nbcr_driver_data->input,KEY_F14,1);
			input_sync(Nbcr_driver_data->input);
			input_report_key(Nbcr_driver_data->input,KEY_F14,0);
			input_sync(Nbcr_driver_data->input);
		}
		else if(time_diff >500&& time_diff <= 700)//500ms ~700ms for end
		{
			printk(KERN_INFO "%s :end configuration \n",__func__);
			Nbcr_driver_data->last_status = Nbcr_driver_data->cur_status;
			Nbcr_driver_data->cur_status = CONFIG_END;
			//input report key event
			input_report_key(Nbcr_driver_data->input,KEY_F15,1);
			input_sync(Nbcr_driver_data->input);
			input_report_key(Nbcr_driver_data->input,KEY_F15,0);
			input_sync(Nbcr_driver_data->input);
		}
		else
		{
			Nbcr_driver_data->last_status = Nbcr_driver_data->cur_status;
			Nbcr_driver_data->cur_status = CONFIG_UNK;
			printk(KERN_ERR "%s :unknown irq \n",__func__);
		}
		Nbcr_driver_data->Nbcr_tt.ts_nsec_last = Nbcr_driver_data->Nbcr_tt.ts_nsec_current;
	}
	else
		printk(KERN_ERR "%s :unexpect irq \n",__func__);
#else
	printk(KERN_INFO "%s :TXD level change \n",__func__);
	//input report key event
	input_report_key(Nbcr_driver_data->input,KEY_F13,1);
	input_sync(Nbcr_driver_data->input);
	input_report_key(Nbcr_driver_data->input,KEY_F13,0);
	input_sync(Nbcr_driver_data->input);
#endif
}

static ssize_t
gpio_accessory_ni_bcr_trigger_status(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	p_data_driver *driver_data = dev->driver_data;

	if( driver_data == NULL || driver_data->n_trigger_gpio <=0 )
	{
		pr_err("%s: Internal NUMA BCR trigger IO is not valid\n", __func__);
		return -EBUSY;
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", !!gpio_get_value_cansleep(driver_data->n_trigger_gpio));
}

static ssize_t
gpio_accessory_ni_bcr_trigger(struct device *dev,
			struct device_attribute *attr, const char *buf, size_t count)
{
	p_data_driver *driver_data = dev->driver_data;
	char *end;
	unsigned long set = simple_strtoul(buf, &end, 0);

	if (end == buf || (set != 0 && set != 1)) {
		return -EINVAL;
	}

	if( driver_data == NULL || driver_data->n_trigger_gpio <= 0)
	{
		pr_err("%s: Internal NUMA BCR trigger IO is not valid\n", __func__);
		return -EBUSY;
	}

	pr_info("%s: set internal NUMA BCR trigger IO %d to %ld\n", __func__, driver_data->n_trigger_gpio, set);
	gpio_direction_output(driver_data->n_trigger_gpio, set);

	return count;
}

static struct device_attribute n_i_bcr_trigger =
	__ATTR(n_i_bcr_trigger, S_IWUSR|S_IWGRP |S_IRUGO, gpio_accessory_ni_bcr_trigger_status, gpio_accessory_ni_bcr_trigger);


static int gpio_accessory_numa_bcr_init(p_data_driver * p_data)
{
	int ret = 0;
	struct device_node *node = p_data->dev->of_node;

	/* NUMA BCR TXD GPIO */
	p_data->n_txd_gpio = of_get_named_gpio(node, "nbcr-txd-gpio", 0);
	pr_info("%s: nbcr-txd-gpio = %d\n", __func__, p_data->n_txd_gpio);
	if (gpio_is_valid(p_data->n_txd_gpio)) {
		ret =gpio_request_one( p_data->n_txd_gpio, GPIOF_DIR_IN, "n_txd");
		if (ret < 0)
		{
			pr_err("%s: could not acquire nbcr-txd-gpio %d (err=%d)\n",__func__, p_data->n_txd_gpio, ret);
			p_data->n_txd_gpio = 0;
			return ret;
		}
		else
			p_data->Nbcr_irq = gpio_to_irq(p_data->n_txd_gpio);
	}
	else
	{
		pr_err("%s: nbcr-txd-gpio %d is not valid\n",__func__, p_data->n_txd_gpio);
		return -EINVAL;
	}


	/* NUMA BCR beeper GPIO */
	p_data->n_beeper_gpio = of_get_named_gpio(node, "nbcr-beeper-gpio", 0);
	pr_info("%s: n_beeper_gpio = %d\n",  __func__, p_data->n_beeper_gpio);
	if (gpio_is_valid(p_data->n_beeper_gpio)) {
		ret = gpio_request_one( p_data->n_beeper_gpio, GPIOF_DIR_IN, "n_beeper");
		if (ret < 0) {
			pr_err("%s: could not acquire n_beeper_gpio %d (err=%d)\n", __func__, p_data->n_beeper_gpio, ret);
			p_data->n_beeper_gpio = 0;
			goto txd_io;
		}
	}
	else
	{
		pr_err("%s: n_beeper_gpio %d is not valid\n",__func__, p_data->n_beeper_gpio);
		ret = EINVAL;
		goto txd_io;
	}

	/* NUMA BCR trigger GPIO */
	p_data->n_trigger_gpio = of_get_named_gpio(node, "nbcr-trigger-gpio", 0);
	pr_info("%s: n_trigger_gpio = %d\n", __func__, p_data->n_trigger_gpio);
	if (gpio_is_valid(p_data->n_trigger_gpio)) {
		ret = gpio_request_one(p_data->n_trigger_gpio, GPIOF_OUT_INIT_HIGH, "n_trigger");
		if (ret < 0) {
			pr_err("%s: could not acquire n_trigger_gpio %d (err=%d)\n", __func__, p_data->n_trigger_gpio, ret);
			p_data->n_trigger_gpio = 0;
			goto bep_io;
		}
		else
		{
			/*create NUMA BCR trigger sysfs*/
			device_create_file(p_data->dev, &n_i_bcr_trigger);
		}
	}
	else
	{
		pr_err("%s: n_trigger_gpio %d is not valid\n",__func__, p_data->n_trigger_gpio);
		ret = EINVAL;
		goto bep_io;
	}

	p_data->input = input_allocate_device();
	if (!p_data->input)
	{
		pr_err("%s: could not allocate input device\n",__func__);
		ret = -ENOMEM;
		goto tri_io;
	}

	input_set_drvdata(p_data->input, p_data);
	p_data->input->name = "NbcrTXD";
#ifdef NBCR_FEATURE_EN
	input_set_capability(p_data->input,EV_KEY,KEY_F13);
	input_set_capability(p_data->input,EV_KEY,KEY_F14);
	input_set_capability(p_data->input,EV_KEY,KEY_F15);
#else
	input_set_capability(p_data->input,EV_KEY,KEY_F13);
#endif
	ret = input_register_device(p_data->input);
	if (ret < 0){
		pr_err("can't register input devices (err=%d)\n", ret);
		input_free_device(p_data->input);
		goto tri_io;
	}

	/*init irq work queue*/
	INIT_WORK(&p_data->Nbcr_irq_work, Nbcr_irq_work_func);
#ifdef NBCR_FEATURE_EN
	ret = request_irq(p_data->Nbcr_irq, Nbcr_irq_handler, IRQF_TRIGGER_FALLING|IRQF_TRIGGER_RISING|IRQF_DISABLED,
				"Nbcr_txd", p_data);
#else
	ret = request_irq(p_data->Nbcr_irq, Nbcr_irq_handler, IRQF_TRIGGER_FALLING|IRQF_ONESHOT,
					"Nbcr_txd", p_data);
#endif
	if( ret)
	{
		pr_err("%s: could not map irq for gpio %d\n",__func__, p_data->n_txd_gpio);
		p_data->Nbcr_irq = 0;
		goto init_input;
	}

	return 0; //success

//destroy_work?
init_input:
	input_free_device(p_data->input);

tri_io:
	if( p_data->n_trigger_gpio > 0)
	{
		device_remove_file(p_data->dev, &n_i_bcr_trigger);
		gpio_free(p_data->n_trigger_gpio);
	}

bep_io:
	if(p_data->n_beeper_gpio > 0)
		gpio_free(p_data->n_beeper_gpio);

txd_io:
	if(p_data->n_txd_gpio)
		gpio_free(p_data->n_txd_gpio);

	return ret;
}
static void gpio_accessory_numa_bcr_deinit(p_data_driver * p_data)
{
	if( p_data == NULL)
		return;

	input_free_device(p_data->input);

	if(p_data->Nbcr_irq > 0)
		free_irq(p_data->Nbcr_irq, p_data);

	if( p_data->n_trigger_gpio > 0)
	{
		device_remove_file(p_data->dev, &n_i_bcr_trigger);
		gpio_free(p_data->n_trigger_gpio);
	}

	if(p_data->n_txd_gpio > 0)
		gpio_free(p_data->n_txd_gpio);

	if(p_data->n_beeper_gpio > 0)
		gpio_free(p_data->n_beeper_gpio);

	if( p_data->input != NULL)
		input_free_device(p_data->input);

	return;
}
/*-----------------------Internal NUMA BCR interface----------------------- */

/*-------------------------Honeywell BCR interface------------------------- */
static ssize_t
gpio_accessory_hi_bcr_trigger_status(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	p_data_driver *driver_data = dev->driver_data;

	if( driver_data == NULL || driver_data->h_trigger_gpio <=0 )
	{
		pr_err("%s: Internal honeywell BCR trigger IO is not valid\n", __func__);
		return -EBUSY;
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", !!gpio_get_value_cansleep(driver_data->h_trigger_gpio));
}

static ssize_t
gpio_accessory_hi_bcr_trigger(struct device *dev,
			struct device_attribute *attr, const char *buf, size_t count)
{
	p_data_driver *driver_data = dev->driver_data;
	char *end;
	unsigned long set = simple_strtoul(buf, &end, 0);

	if (end == buf || (set != 0 && set != 1)) {
		return -EINVAL;
	}

	if( driver_data == NULL || driver_data->h_trigger_gpio <= 0)
	{
		pr_err("%s: Internal honeywell BCR trigger IO is not valid\n", __func__);
		return -EBUSY;
	}

	pr_info("%s: set internal honeywell BCR trigger IO %d to %ld\n", __func__, driver_data->h_trigger_gpio, set);
	gpio_direction_output(driver_data->h_trigger_gpio, set);

	return count;
}

static struct device_attribute h_i_bcr_trigger =
	__ATTR(h_i_bcr_trigger, S_IWUSR|S_IWGRP |S_IRUGO, gpio_accessory_hi_bcr_trigger_status, gpio_accessory_hi_bcr_trigger);


static int gpio_accessory_honeywell_bcr_init(p_data_driver * p_data)
{
	int ret = 0;
	struct device_node *node = p_data->dev->of_node;

	/* Honeywell BCR beeper GPIO */
	p_data->h_beeper_gpio = of_get_named_gpio(node, "hbcr-beeper-gpio", 0);
	pr_info("%s: h_beeper_gpio = %d\n", __func__, p_data->h_beeper_gpio);
	if (gpio_is_valid(p_data->h_beeper_gpio)) {
		ret = gpio_request_one( p_data->h_beeper_gpio, GPIOF_DIR_IN, "h_beeper");
		if (ret < 0) {
			pr_err("%s: could not acquire h_beeper_gpio gpio %d (err=%d)\n",__func__, p_data->h_beeper_gpio, ret);
			p_data->h_beeper_gpio = 0;
			return ret;
		}
	}
	else
	{
		pr_err("%s: h_beeper_gpio gpio %d is not valid\n",__func__, p_data->h_beeper_gpio);
		return -EINVAL;
	}

	/* Honeywell BCR trigger GPIO */
	p_data->h_trigger_gpio = of_get_named_gpio(node, "hbcr-trigger-gpio", 0);
	pr_info("%s: h_trigger_gpio = %d\n",__func__, p_data->h_trigger_gpio);
	if (gpio_is_valid(p_data->h_trigger_gpio)) {
		ret =gpio_request_one( p_data->h_trigger_gpio, GPIOF_OUT_INIT_HIGH, "h_trigger");
		if (ret < 0) {
			pr_err("%s: could not acquire h_trigger_gpio %d (err=%d)\n",__func__, p_data->h_trigger_gpio, ret);
			p_data->h_trigger_gpio = 0;
			goto init_beep_io;
		}
		/*create sysfs*/
		device_create_file(p_data->dev, &h_i_bcr_trigger);
	}
	else
	{
		pr_err("%s: h_trigger_gpio gpio %d is not valid\n",__func__, p_data->h_trigger_gpio);
		goto init_beep_io;
	}

	return 0; //success

init_beep_io:
	if( p_data->h_beeper_gpio > 0)
		gpio_free(p_data->h_trigger_gpio);

	return ret;
}
static void gpio_accessory_honeywell_bcr_deinit(p_data_driver * p_data)
{
	if( p_data == NULL)
		return;

	if( p_data->h_trigger_gpio > 0)
	{
		device_remove_file(p_data->dev, &h_i_bcr_trigger);
		gpio_free(p_data->h_trigger_gpio);
	}

	if( p_data->h_beeper_gpio > 0)
		gpio_free(p_data->h_trigger_gpio);

	return;
}
/*-------------------------Honeywell BCR interface------------------------- */

/*----------------------Cash drawer interface---------------------*/
static ssize_t
gpio_accessory_cash_drawer_enable(struct device *dev,
			struct device_attribute *attr, const char *buf, size_t count)
{
	p_data_driver *driver_data = dev->driver_data;
	char *end;
	unsigned long set = simple_strtoul(buf, &end, 0);

	if (end == buf || (set != 0 && set != 1)) {
		return -EINVAL;
	}

	if( driver_data == NULL || driver_data->cash_d_solen <= 0)
	{
		pr_err("%s: cash drawer solen IO is not valid\n", __func__);
		return -EBUSY;
	}

	pr_info("%s: set cash drawer solen IO %d to %ld\n", __func__, driver_data->cash_d_solen, set);
	gpio_direction_output(driver_data->cash_d_solen, set);

	/*2017-06-05 Jack W Lu: fix solenoid broken issue {*/
	if(set == 1)
	{
		msleep(200);//sleep 200ms
		gpio_direction_output(driver_data->cash_d_solen, 0);
	}
	/*2017-06-05 Jack W Lu: fix solenoid broken issue }*/

	return count;
}

#define MAX_PULSE_WIDTH               (10000)           //10ms
#define MIN_PULSE_WIDTH               (5)               //us
#define OPEN_DEFAULT_ST               (1000)            //us
#define OPEN_ST_LOW_TH                (30)              //us
#define CLOSE_DEFAULT_ST              (MIN_PULSE_WIDTH) //us
#define CLOSE_ST_HIGH_TH              (MAX_PULSE_WIDTH) //us
#define CAL_STEP_TWO_TH               (50)              //us
#define CAL_STEP_FIVE_TH              (150)             //us
#define CAL_STEP_FIFTY_TH             (500)             //us
#define CAL_LOOP_COUNT                (25)              //times
#define CAL_STEP_INTERVAL             (15)              //15ms
#define DEFAULT_PULSE_WIDTH           (500)             //default 500us
#define DEFAULT_LOW_HIGH_GAP          (100)             //100us


static ssize_t
gpio_accessory_cash_drawer_pulse(struct device *dev,
			struct device_attribute *attr, const char *buf, size_t count)
{
	char *end;
	p_data_driver *driver_data = dev->driver_data;
	unsigned long set = simple_strtoul(buf, &end, 0);

	if (end == buf || ( set == 0 || set > MAX_PULSE_WIDTH)) {
		return -EINVAL;
	}

	driver_data->cs_pulse_width = set;
	pr_info("%s: set cash drawer pulse width to %ld\n", __func__, driver_data->cs_pulse_width);

	return count;
}

static ssize_t
gpio_accessory_cash_drawer_status(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	int ret = 0;
	p_data_driver *driver_data = dev->driver_data;
	unsigned long pulse_width;

	if( driver_data->cs_pulse_width != DEFAULT_PULSE_WIDTH ) //user space setting
	{
		pulse_width = driver_data->cs_pulse_width;
	}
	else if( driver_data->cs_low_th != 0 && driver_data->cs_high_th != 0 )
	{
		pulse_width = ( driver_data->cs_low_th + driver_data->cs_high_th ) / 2 ;
	}
	else
		pulse_width = DEFAULT_PULSE_WIDTH;

	if( driver_data == NULL || driver_data->cash_d_sense <= 0 || driver_data->cash_d_detct <= 0 )
	{
		pr_err("%s: cash drawer sense driver or detect IO is not valid\n", __func__);
		return -EBUSY;
	}

	/* enable sense driver */
	gpio_direction_output(driver_data->cash_d_sense, 1);
	udelay(pulse_width);//pulse_width?
	/* read cash detect IO status*/
	ret = !!gpio_get_value_cansleep(driver_data->cash_d_detct);
	/*disable sense driver */
	gpio_direction_output(driver_data->cash_d_sense, 0);
	pr_err("%s: cd status: %d (pulse width: %ldus)\n", __func__, ret, pulse_width);

	return snprintf(buf, PAGE_SIZE, "%d\n", ret);
}


static ssize_t
gpio_accessory_cash_drawer_calibration_close(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	p_data_driver *driver_data = dev->driver_data;
	int ret = 0;
	int cd_error = 10;
	int delay_count = CLOSE_DEFAULT_ST;
	int cal_threshold = -1;
	int i;

	if( driver_data == NULL || driver_data->cash_d_sense <=0 || driver_data->cash_d_detct <= 0 )
	{
		pr_err("%s: cash drawer sense driver or detect IO is not valid\n", __func__);
		cd_error = 10; //calibration error?
		goto calibration_done;
	}

	if( atomic_read(&driver_data->calibrating) )
	{
		pr_err("%s: another calibration is ongoing\n", __func__);
		cd_error = 12;
		goto calibration_done;
	}

	atomic_set(&driver_data->calibrating, 1);

	while( delay_count < ( CLOSE_ST_HIGH_TH - DEFAULT_LOW_HIGH_GAP) )
	{
		pr_info("%s: %dus step\n", __func__, delay_count);
		for( i = 0; i < CAL_LOOP_COUNT; i++ )
		{
			/* enable sense driver */
			gpio_direction_output(driver_data->cash_d_sense, 1);
			udelay(delay_count);//pulse_width?
			/* read cash detect IO status*/
			ret = !!gpio_get_value_cansleep(driver_data->cash_d_detct);
			/*disable sense driver */
			gpio_direction_output(driver_data->cash_d_sense, 0);
			//pr_info("%s: %dus step: %d try: %d\n", __func__, delay_count, i, ret);
			mdelay(CAL_STEP_INTERVAL);
			if( ret == 1 )
				break;
		}

		if( i == CAL_LOOP_COUNT )
		{
			pr_err("%s: find the threshold (%dus) for close status calibration\n", __func__, delay_count);
			cal_threshold = delay_count;
			break;
		}

		if( delay_count <= CAL_STEP_TWO_TH )
			delay_count += 2;
		else if ( delay_count <= CAL_STEP_FIVE_TH )
			delay_count += 5;
		else if ( delay_count <= CAL_STEP_FIFTY_TH )
			delay_count += 50;
		else
			delay_count = delay_count + 100;
	}
	atomic_set(&driver_data->calibrating, 0);

calibration_done:
	if( cal_threshold > 0 )
	{
		driver_data->cs_low_th = cal_threshold;
		return snprintf(buf, PAGE_SIZE, "%d\n", cal_threshold);
	}
	else
		return snprintf(buf, PAGE_SIZE, "-%d\n", cd_error); //request by framework
}

static ssize_t
gpio_accessory_cash_drawer_calibration_open(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	int ret = 0;
	p_data_driver *driver_data = dev->driver_data;
	int delay_count = OPEN_DEFAULT_ST, i;
	int cal_threshold = -1;
	int cd_error = 10; //calibration error
	int end_loop_count = OPEN_ST_LOW_TH;

	if( driver_data == NULL || driver_data->cash_d_sense <= 0 || driver_data->cash_d_detct <= 0 )
	{
		pr_err("%s: cash drawer sense driver or detect IO is not valid\n", __func__);
		cd_error = 10; //calibration error
		goto calibration_done;
	}

	if( driver_data->cs_low_th == 0 )
	{
		pr_err("%s: close calibration should be done first\n", __func__);
		cd_error = 11;
		goto calibration_done;
	}

	end_loop_count = driver_data->cs_low_th + DEFAULT_LOW_HIGH_GAP;

	//if( end_loop_count < OPEN_ST_LOW_TH ) //DEFAULT_LOW_HIGH_GAP is greater than OPEN_ST_LOW_TH
	//	end_loop_count = OPEN_ST_LOW_TH;

	if( end_loop_count > OPEN_DEFAULT_ST )
		delay_count = end_loop_count * 2;

	if( delay_count > MAX_PULSE_WIDTH )
		delay_count = MAX_PULSE_WIDTH;

	if( atomic_read(&driver_data->calibrating) )
	{
		pr_err("%s: another calibration is ongoing\n", __func__);
		cd_error = 12;
		goto calibration_done;
	}

	atomic_set(&driver_data->calibrating, 1);

	while( delay_count > end_loop_count )
	{
		pr_info("%s: %dus step\n", __func__, delay_count);
		for( i = 0; i < CAL_LOOP_COUNT; i++ )
		{
			/* enable sense driver */
			gpio_direction_output(driver_data->cash_d_sense, 1);
			udelay(delay_count);//pulse_width?
			/* read cash detect IO status*/
			ret = !!gpio_get_value_cansleep(driver_data->cash_d_detct);
			/*disable sense driver */
			gpio_direction_output(driver_data->cash_d_sense, 0);
			mdelay(CAL_STEP_INTERVAL);
			if( ret == 0 )
				break;
		}

		if( i == CAL_LOOP_COUNT )
		{
			pr_err("%s: find the threshold (%dus) for open status calibration\n", __func__, delay_count);
			cal_threshold = delay_count;
			break;
		}

		if ( delay_count >= ( CAL_STEP_FIFTY_TH + end_loop_count ) )
			delay_count -= 100;
		else if ( delay_count >= ( CAL_STEP_FIVE_TH + end_loop_count ) )
			delay_count -= 50;
		else if ( delay_count >= ( CAL_STEP_TWO_TH + end_loop_count) )
			delay_count -= 5;
		else
			delay_count -= 2;
	}
	atomic_set(&driver_data->calibrating, 0);

calibration_done:
	if( cal_threshold > 0 )
	{
		driver_data->cs_high_th = cal_threshold;
		return snprintf(buf, PAGE_SIZE, "%d\n", cal_threshold);
	}
	else
		return snprintf(buf, PAGE_SIZE, "-%d\n", cd_error); //request by framework
}

static ssize_t
gpio_accessory_cash_drawer_adc_status(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	int rc = -1;
	struct qpnp_vadc_result adc_result;
	p_data_driver *driver_data = dev->driver_data;

	if (!driver_data->vadc_dev) {
		driver_data->vadc_dev = qpnp_get_vadc(dev, "cd_detect");
		if (IS_ERR(driver_data->vadc_dev)) {
			pr_err("%s:  vadc get failed\n", __func__);
			driver_data->vadc_dev = NULL;
			return -EIO;
		}
	}

	gpio_direction_output(driver_data->cash_d_sense, 1);
	mdelay(10);
	rc = qpnp_vadc_read(driver_data->vadc_dev, P_MUX2_1_3, &adc_result);
	if (rc) {
		gpio_direction_output(driver_data->cash_d_sense, 0);
		pr_err("error reading adc channel = %d, rc = %d\n", P_MUX2_1_3, rc);
		return -EIO;
	}
	gpio_direction_output(driver_data->cash_d_sense, 0);

	pr_info("%s: cash drawer adc mvolts phy=%lld meas=0x%llx\n",__func__, adc_result.physical, adc_result.measurement);

	return snprintf(buf, PAGE_SIZE, "%dmv\n", (int)adc_result.physical / 1000);
}


static struct device_attribute cash_drawer_en =
	__ATTR(cash_drawer_en, S_IWUSR|S_IWGRP, NULL, gpio_accessory_cash_drawer_enable);

static struct device_attribute cash_drawer_status =
	__ATTR(cash_drawer_status, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP, gpio_accessory_cash_drawer_status, gpio_accessory_cash_drawer_pulse);


static struct device_attribute cd_calibration_open =
	__ATTR(cd_calibration_open, S_IRUSR|S_IRGRP, gpio_accessory_cash_drawer_calibration_open, NULL);

static struct device_attribute cd_calibration_close =
	__ATTR(cd_calibration_close, S_IRUSR|S_IRGRP, gpio_accessory_cash_drawer_calibration_close, NULL);

static struct device_attribute cd_adc_detect =
	__ATTR(cd_adc_detect, S_IRUSR|S_IRGRP, gpio_accessory_cash_drawer_adc_status, NULL);

static int gpio_accessory_cash_drawer_init(p_data_driver * p_data)
{
	int ret = 0;
	struct device_node *node = p_data->dev->of_node;

	/*cash drawer init*/
	p_data->cash_d_solen = of_get_named_gpio(node, "cd-solenoid-en", 0);
	pr_info("%s: cd-solenoid = %d\n", __func__, p_data->cash_d_solen);
	if (gpio_is_valid(p_data->cash_d_solen)) {
		ret = gpio_request_one(p_data->cash_d_solen, GPIOF_OUT_INIT_LOW, "CD SOLENOID"); //init as low
		if (ret < 0)
		{
			pr_err(">>>> [GPIO init] could NOT acquire cd-solenoid gpio (err=%d)\n", ret);
			p_data->cash_d_solen = 0;
			return ret;
		}

		/*init solenoid_enable sysfs*/
		device_create_file(p_data->dev, &cash_drawer_en);
	}

	p_data->cash_d_sense = of_get_named_gpio(node, "cd-sense-drv", 0);
	pr_info("%s: cd-sense-drv = %d\n", __func__, p_data->cash_d_sense);
	if (gpio_is_valid(p_data->cash_d_sense)) {
		ret = gpio_request_one( p_data->cash_d_sense, GPIOF_OUT_INIT_LOW, "CD SENSE CPU");
		if (ret < 0) {
				pr_err(">>>> [GPIO init] could NOT acquire cd-sense-drv gpio (err=%d)\n", ret);
				p_data->cash_d_sense = 0;
				goto init_d_solen;
		}
	}
	else
	{
		pr_err("%s: cash drawer sense driver io not valid\n", __func__);
		ret = -EINVAL;
		goto init_d_solen;
	}

	p_data->cash_d_detct = of_get_named_gpio(node, "cd-detect", 0);
	pr_info("%s: cd-detect = %d\n", __func__, p_data->cash_d_detct);
	if (gpio_is_valid(p_data->cash_d_detct)) {
		ret = gpio_request_one(p_data->cash_d_detct, GPIOF_DIR_IN, "CD DETECT");
		if (ret < 0) {
				pr_err(">>>> [GPIO init] could NOT acquire cd-detect gpio (err=%d)\n", ret);
				p_data->cash_d_detct = 0;
				goto init_d_sense;
		}
	}
	else
	{
		pr_err("%s: cash drawer detect io not valid\n", __func__);
		ret = -EINVAL;
		goto init_d_sense;
	}

	/*init flag and count*/
	atomic_set(&p_data->calibrating, 0);
	p_data->cs_pulse_width = DEFAULT_PULSE_WIDTH;
	p_data->cs_low_th = 0;
	p_data->cs_high_th = 0;
	p_data->vadc_dev = NULL;
	/*init cash drawer check sysfs*/
	device_create_file(p_data->dev, &cash_drawer_status);
	device_create_file(p_data->dev, &cd_calibration_open);
	device_create_file(p_data->dev, &cd_calibration_close);
	device_create_file(p_data->dev, &cd_adc_detect);

	return 0;

init_d_sense:
	if(p_data->cash_d_sense > 0)
		gpio_free(p_data->cash_d_sense);

init_d_solen:
	if(p_data->cash_d_solen > 0)
	{
		device_remove_file(p_data->dev, &cash_drawer_en);
		gpio_free(p_data->cash_d_solen);
	}

	return ret;
}

static void gpio_accessory_cash_drawer_deinit(p_data_driver * p_data)
{
	if( p_data == NULL)
		return;

	if( p_data->cash_d_solen > 0)
	{
		device_remove_file(p_data->dev, &cash_drawer_en);
		gpio_free(p_data->cash_d_solen);
	}

	if( p_data->cash_d_sense > 0 && p_data->cash_d_detct > 0 )
	{
		device_remove_file(p_data->dev, &cash_drawer_status);
		device_remove_file(p_data->dev, &cd_calibration_open);
		device_remove_file(p_data->dev, &cd_calibration_close);
		device_remove_file(p_data->dev, &cd_adc_detect);
		gpio_free(p_data->cash_d_sense);
		gpio_free(p_data->cash_d_detct);
	}

	return;
}
/*----------------------Cash drawer interface---------------------*/

/*------------------------Printer interface-----------------------*/
static ssize_t
gpio_accessory_printer_power_status(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	p_data_driver *driver_data = dev->driver_data;

	if( driver_data == NULL || driver_data->printer_power <=0 )
	{
		pr_err("%s: cash drawer sense driver or detect IO is not valid\n", __func__);
		return -EBUSY;
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", !!gpio_get_value_cansleep(driver_data->printer_power));
}

static ssize_t
gpio_accessory_printer_power_enable(struct device *dev,
			struct device_attribute *attr, const char *buf, size_t count)
{
	p_data_driver *driver_data = dev->driver_data;
	char *end;
	unsigned long set = simple_strtoul(buf, &end, 0);

	if (end == buf || (set != 0 && set != 1)) {
		return -EINVAL;
	}

	if( driver_data == NULL || driver_data->printer_power <= 0)
	{
		pr_err("%s: printer power enable IO is not valid\n", __func__);
		return -EBUSY;
	}

	pr_info("%s: set printer power enable IO %d to %ld\n", __func__, driver_data->printer_power, set);
	gpio_direction_output(driver_data->printer_power, set);

	return count;
}

static struct device_attribute printer_power_en =
	__ATTR(printer_power_en, S_IWUSR|S_IWGRP |S_IRUGO, gpio_accessory_printer_power_status, gpio_accessory_printer_power_enable);


static int gpio_accessory_printer_init(p_data_driver * p_data)
{
	int ret = 0;
	struct device_node *node = p_data->dev->of_node;

	p_data->printer_power = of_get_named_gpio(node, "pp-power-en", 0);
	pr_info("%s: pp-power-en = %d\n", __func__, p_data->printer_power);
	if (gpio_is_valid(p_data->printer_power)) {
		ret =gpio_request_one( p_data->printer_power, GPIOF_OUT_INIT_HIGH, "printer_power_en");
		if (ret < 0) {
			pr_err("%s: could not acquire pp-power-en gpio %d (err=%d)\n", __func__, p_data->printer_power, ret);
			p_data->printer_power = 0;
			return ret;
		}

		/*init printer power enable sysfs */
		device_create_file(p_data->dev, &printer_power_en);
	}
	else
	{
		pr_err("%s: pp-power-en io not valid\n", __func__);
		return -EINVAL;
	}

	return 0;//success
}

static void gpio_accessory_printer_deinit(p_data_driver * p_data)
{
	if( p_data == NULL)
		return;

	if( p_data->printer_power > 0)
	{
		device_remove_file(p_data->dev, &printer_power_en);
		gpio_free(p_data->printer_power);
	}

	return;
}
/*------------------------Printer interface-----------------------*/

/* 20170526 JackWLu: I2.0 add external BCR GPIOs (Use USB ID pin for trigger) {*/
#ifdef I20_SUPPORT_EXTERNAL_BCR_TRIGGER
static ssize_t i20_ex_bcr_trigger_control(struct device *dev,
			struct device_attribute *attr, const char *buf, size_t count)
{
	p_data_driver *driver_data = dev->driver_data;
	char *end;
	unsigned long set = simple_strtoul(buf, &end, 0);

	if (end == buf || (set != 0 && set != 1)) {
		return -EINVAL;
	}

	if( driver_data == NULL || driver_data->i20_ext_bcr_trigger_d1 <= 0
			|| driver_data->i20_ext_bcr_trigger_d2 <= 0
			|| driver_data->i20_ext_bcr_trigger_d3 <= 0
			|| driver_data->i20_ext_bcr_trigger_d4 <= 0)
	{
		pr_err("%s: IO is not valid\n", __func__);
		return -EBUSY;
	}

	pr_info("%s: set trigger IO to %ld\n", __func__, set);
	gpio_direction_output(driver_data->i20_ext_bcr_trigger_d1, set);
	gpio_direction_output(driver_data->i20_ext_bcr_trigger_d2, set);
	gpio_direction_output(driver_data->i20_ext_bcr_trigger_d3, set);
	gpio_direction_output(driver_data->i20_ext_bcr_trigger_d4, set);

	return count;
}

static struct device_attribute i20_ex_bcr_trigger_ctl =
	__ATTR(ex_bcr_trig_ctl, S_IWUSR|S_IWGRP, NULL, i20_ex_bcr_trigger_control);

static int i20_external_bcr_trigger_init(p_data_driver * p_data)
{
	int ret = 0;
	struct device_node *node = p_data->dev->of_node;

	p_data->i20_ext_bcr_trigger_d1 = of_get_named_gpio(node, "ex-bcr-tg-d1-gpio", 0);
	p_data->i20_ext_bcr_trigger_d2 = of_get_named_gpio(node, "ex-bcr-tg-d2-gpio", 0);
	p_data->i20_ext_bcr_trigger_d3 = of_get_named_gpio(node, "ex-bcr-tg-d3-gpio", 0);
	p_data->i20_ext_bcr_trigger_d4 = of_get_named_gpio(node, "ex-bcr-tg-d4-gpio", 0);
	pr_info("ex-bcr-tg-d1-gpio = %d\n", p_data->i20_ext_bcr_trigger_d1);
	pr_info("ex-bcr-tg-d2-gpio = %d\n", p_data->i20_ext_bcr_trigger_d2);
	pr_info("ex-bcr-tg-d3-gpio = %d\n", p_data->i20_ext_bcr_trigger_d3);
	pr_info("ex-bcr-tg-d4-gpio = %d\n", p_data->i20_ext_bcr_trigger_d4);

	if( (gpio_is_valid(p_data->i20_ext_bcr_trigger_d1) == 0)
		||(gpio_is_valid(p_data->i20_ext_bcr_trigger_d2) == 0)
		||(gpio_is_valid(p_data->i20_ext_bcr_trigger_d3) == 0)
		||(gpio_is_valid(p_data->i20_ext_bcr_trigger_d4) == 0) )
	{
		pr_err("%s: I2.0 io not valid\n", __func__);
		return -EINVAL;
	}

	ret = gpio_request_one(p_data->i20_ext_bcr_trigger_d1, GPIOF_OUT_INIT_HIGH, "ex-bcr-tg-d1");
	if (ret < 0)
	{
		pr_err("could not acquire gpio ret = %d\n", ret);
		p_data->i20_ext_bcr_trigger_d1 = 0;
		return ret;
	}
	ret = gpio_request_one(p_data->i20_ext_bcr_trigger_d2, GPIOF_OUT_INIT_HIGH, "ex-bcr-tg-d2");
	if (ret < 0)
	{
		pr_err("could not acquire gpio ret = %d\n", ret);
		p_data->i20_ext_bcr_trigger_d2 = 0;
		return ret;
	}
	ret = gpio_request_one(p_data->i20_ext_bcr_trigger_d3, GPIOF_OUT_INIT_HIGH, "ex-bcr-tg-d3");
	if (ret < 0)
	{
		pr_err("could not acquire gpio ret = %d\n", ret);
		p_data->i20_ext_bcr_trigger_d3 = 0;
		return ret;
	}
	ret = gpio_request_one(p_data->i20_ext_bcr_trigger_d4, GPIOF_OUT_INIT_HIGH, "ex-bcr-tg-d4");
	if (ret < 0)
	{
		pr_err("could not acquire gpio ret = %d\n", ret);
		p_data->i20_ext_bcr_trigger_d4 = 0;
		return ret;
	}

	device_create_file(p_data->dev, &i20_ex_bcr_trigger_ctl);

	return 0;//success
}

static void i20_external_bcr_trigger_deinit(p_data_driver * p_data)
{
	if( p_data == NULL)
		return;
	if( (p_data->i20_ext_bcr_trigger_d1 > 0)
		&&(p_data->i20_ext_bcr_trigger_d2 > 0)
		&&(p_data->i20_ext_bcr_trigger_d3 > 0)
		&&(p_data->i20_ext_bcr_trigger_d4 > 0) )
	{
		device_remove_file(p_data->dev, &i20_ex_bcr_trigger_ctl);
	}

	if (p_data->i20_ext_bcr_trigger_d1 > 0)
	{
		gpio_free(p_data->i20_ext_bcr_trigger_d1);
	}
	if (p_data->i20_ext_bcr_trigger_d2 > 0)
	{
		gpio_free(p_data->i20_ext_bcr_trigger_d2);
	}
	if (p_data->i20_ext_bcr_trigger_d3 > 0)
	{
		gpio_free(p_data->i20_ext_bcr_trigger_d3);
	}
	if (p_data->i20_ext_bcr_trigger_d4 > 0)
	{
		gpio_free(p_data->i20_ext_bcr_trigger_d4);
	}

	return;
}

#endif
/* 20170526 JackWLu: I2.0 add external BCR GPIOs (Use USB ID pin for trigger) }*/

static int gpio_accessory_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct pinctrl *ts_pinctrl;
	struct pinctrl_state *pinctrl_state_active;
	p_data_driver *driver_data = NULL;

	/* Get pinctrl if target uses pinctrl */
	ts_pinctrl = devm_pinctrl_get(&pdev->dev);
	driver_data = kzalloc(sizeof(p_data_driver), GFP_KERNEL);
	if (!driver_data)
		pr_err("alloc driver_data fail%X\n",-ENOMEM);

	/*init global data*/
	driver_data->dev = &pdev->dev;
	pdev->dev.driver_data = driver_data;

	if (IS_ERR_OR_NULL(ts_pinctrl)) {
		ret = PTR_ERR(ts_pinctrl);
		pr_err("%s: Pincontrol DT property returned %X\n",
				__func__, ret);
	}

	pinctrl_state_active = pinctrl_lookup_state(ts_pinctrl, "gpio_accessory_active");
	if (IS_ERR_OR_NULL(pinctrl_state_active)) {
		ret = PTR_ERR(pinctrl_state_active);
		pr_err("Can not lookup %s pinstate %d\n", PINCTRL_STATE_ACTIVE, ret);
	}
	ret = pinctrl_select_state(ts_pinctrl, pinctrl_state_active);
	if (ret < 0)
		pr_err("%s: Failed to select %s pinstate %d\n", __func__, PINCTRL_STATE_ACTIVE, ret);

/* 20170526 JackWLu: I2.0 add external BCR GPIOs (Use USB ID pin for trigger) {*/
#ifdef I20_SUPPORT_EXTERNAL_BCR_TRIGGER
	ret = i20_external_bcr_trigger_init(driver_data);
	if(ret == 0)
	{
		pr_err("i20_external_bcr_trigger_init init OK\n");
		return ret;
	}
#endif
/* 20170526 JackWLu: I2.0 add external BCR GPIOs (Use USB ID pin for trigger) }*/

	/* Cash-Drawer init */
	ret = gpio_accessory_cash_drawer_init(driver_data);
	if(ret)
	{
		pr_err("%s: cash drawer init failed: %d\n", __func__, ret);
		return ret;
	}

	/* Printer init */
	ret = gpio_accessory_printer_init(driver_data);
	if(ret)
	{
		pr_err("%s: Printer power IO init failed: %d\n", __func__, ret);
		goto init_cash;
	}

	/* Internal Honeywell Barcode scanner*/
	ret = gpio_accessory_honeywell_bcr_init(driver_data);
	if(ret)
	{
		pr_err("%s: Honeywell Barcode scanner IO init failed: %d\n", __func__, ret);
		goto init_prin;
	}

	/* Internal NUMA Barcode scanner */
	ret = gpio_accessory_numa_bcr_init(driver_data);
	if(ret)
	{
		pr_err("%s: NUMA Barcode scanner IO init failed: %d\n", __func__, ret);
		goto init_hony;
	}

	return 0;

init_hony:
	gpio_accessory_honeywell_bcr_deinit(driver_data);

init_prin:
	gpio_accessory_printer_deinit(driver_data);

init_cash:
	gpio_accessory_cash_drawer_deinit(driver_data);

	return ret;
}

static int gpio_accessory_remove(struct platform_device *pdev)
{
	p_data_driver *driver_data = dev_get_drvdata(&pdev->dev);

	if (!driver_data)
		return -EFAULT;
/* 20170526 JackWLu: I2.0 add external BCR GPIOs (Use USB ID pin for trigger) {*/
#ifdef I20_SUPPORT_EXTERNAL_BCR_TRIGGER
	i20_external_bcr_trigger_deinit(driver_data);
#endif
/* 20170526 JackWLu: I2.0 add external BCR GPIOs (Use USB ID pin for trigger) }*/
	gpio_accessory_cash_drawer_deinit(driver_data);
	gpio_accessory_printer_deinit(driver_data);
	gpio_accessory_honeywell_bcr_deinit(driver_data);
	gpio_accessory_numa_bcr_deinit(driver_data);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id gpio_accessory_of_match[] = {
		{ .compatible = ACCESSORY_MODNAME, },
		{ }
};
MODULE_DEVICE_TABLE(of, gpio_accessory_of_match);
#endif

static struct platform_driver gpio_accessory_platform_driver = {
	.driver = {
		.name           = ACCESSORY_MODNAME,
		.owner          = THIS_MODULE,
		.of_match_table = of_match_ptr(gpio_accessory_of_match),
	},
	.probe  = gpio_accessory_probe,
	.remove = gpio_accessory_remove,
};

module_platform_driver(gpio_accessory_platform_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Generic GPIO accessory driver");
