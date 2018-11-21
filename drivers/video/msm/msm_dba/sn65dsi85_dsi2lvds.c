/*
 * drivers/video/tegra/dc/sn65dsi85_dsi2lvds.c
 *
 * Copyright (c) 2013, NVIDIA Corporation.
 *
 * Author:
 *	Bibek Basu <bbasu@nvidia.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */
 #include <linux/module.h>
 #include <linux/moduleparam.h>//Leo Guo add for ftd to read ic id
 #include <linux/i2c.h>
 #include <linux/device.h>
 #include <linux/slab.h>
 #include <linux/delay.h>
 #include <linux/regmap.h>
 #include <linux/swab.h>
 #include <linux/err.h>
 #include "sn65dsi85_dsi2lvds.h"
 #include <linux/of_device.h>
 #include <linux/gpio.h>
 #include <linux/interrupt.h>
 #include <linux/of_gpio.h>
 #include <linux/of_irq.h>
 #include <soc/qcom/socinfo.h>
 #include "../mdss/mdss_dsi.h"

 //#define DEBUG_COLOR_BAR   // uncomment this defined for colorbar testing
 #define COLOR_BAR_TYPE  (0)  // should 0 ~ 7

 /* IRQ STATUS */
 #define IRQ_STATUS_DISABLED			(1<<16)
 #define IRQ_STATUS_ENABLED				0x0


 //Leo Guo add for ftd to read ic id
 static char edp_device_id[6];
 static int edp_ic_version=0;
 module_param(edp_ic_version, int, 0444);

 static struct sn65dsi85_platform_data *sn65dsi85_dsi2lvds_pdata = NULL;
 static struct i2c_client *sn65dsi85_i2c_client = NULL;
 static struct sn65dsi85_platform_data *dsi2lvds=NULL;
 #if 0
 static int PANEL_SUPPORT_ASSR=0;
 static int PANEL_SUPPORT_ENHANCE_FRAMING=0;
 static int PANEL_SUPPORT_MAX_LINK_RATE=0;
 #endif

 enum i2c_transfer_type {
 	I2C_WRITE,
 	I2C_READ,
 };

 static inline int sn65dsi85_reg_write(struct sn65dsi85_platform_data *dsi2lvds,
                                       unsigned int addr, unsigned int val)
 {
       int error;
       error = regmap_write(dsi2lvds->regmap, addr, val);
       if(error != 0)
           printk("sn65dsi85_reg_write error=%d\n",error);
	   msleep(10);
       return error;
 }

 static inline void sn65dsi85_reg_read(struct sn65dsi85_platform_data *dsi2lvds,
                                       unsigned int addr, unsigned int *val)
 {
       int error;
       error = regmap_read(dsi2lvds->regmap, addr, val);
       if(error != 0)
          printk("sn65dsi85_reg_read error =%d addr=%x val=%x\n",error,addr,*val);
 }

 static const struct regmap_config sn65dsi85_regmap_config = {
        .reg_bits = 8,
        .val_bits = 8,
 };


 static int sn65dsi85_dsi2lvds_init(void)
 {
 	int err = 0;

	printk("sn65dsi85_dsi2lvds_init\n");
 	if (!sn65dsi85_dsi2lvds_pdata)
          return -ENOMEM;

 	mutex_init(&sn65dsi85_dsi2lvds_pdata->lock);
 	sn65dsi85_dsi2lvds_pdata->dsi2lvds_enabled = false;
	sn65dsi85_dsi2lvds_pdata->client_i2c = sn65dsi85_i2c_client;
	sn65dsi85_dsi2lvds_pdata->regmap =  devm_regmap_init_i2c(sn65dsi85_i2c_client,
                                                         &sn65dsi85_regmap_config);

        if (IS_ERR(sn65dsi85_dsi2lvds_pdata->regmap)) {
            err = PTR_ERR(sn65dsi85_dsi2lvds_pdata->regmap);
            goto fail;
        }

        printk("sn65dsi85_dsi2lvds_init end\n");

fail:
        return err;
}

static void sn65dsi85_dsi2lvds_destroy(void)
{
	if (!dsi2lvds)
		return;
	mutex_destroy(&dsi2lvds->lock);
	regmap_exit(dsi2lvds->regmap);
	sn65dsi85_dsi2lvds_pdata = NULL;
}

//Leo Guo add following for cont splash issue
int panel_en1=-1;
int panel_en2=-1;
int panel_pwr_det1=-1;
int panel_pwr_det2=-1;
int panel_pwr_det3=-1;
static int sn65dsi85_rst=-1;
int led_en_lvds=-1;
static int splash_flag=1;
//Leo Guo add for lvds IC error issue
static int mipi_int=-1;
struct mutex lock_for_lvds;
//Leo Guo add for lvds panel no display issue
int lvds_irq;
int irq_detected_flag=0;
int irq_times=0;




extern void set_pwm_for_lvds_panel(struct mdss_dsi_ctrl_pdata *ctrl,u32 level);

void sn65dsi85_dsi2lvds_enable_early(struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	//PANEL_ID_TYPE qisda_panel_id=socinfo_get_panel_id();
	printk("sn65dsi85_dsi2lvds_enable_early\n\n");

	if(dsi2lvds==NULL)
		return;
	if (dsi2lvds && dsi2lvds->dsi2lvds_enabled)
		return;
	if(splash_flag)
	{
		#if 0
		gpio_direction_output(panel_en1,1);
		gpio_direction_output(panel_en2,1);
		gpio_set_value(led_en_lvds, 1);
		//if(qisda_panel_id==HR215WU1_120)
		{
			gpio_set_value(led_en_lvds, 0);
			set_pwm_for_lvds_panel(ctrl_pdata,0);
			msleep(200);
			if(qisda_panel_id==TM101JDHP01_00)
				gpio_set_value(panel_en2, 0);
			msleep(50);
			gpio_set_value(panel_en1, 0);
			gpio_direction_output(panel_en1,0);
			if(qisda_panel_id==HR215WU1_120)
				msleep(130);
			else
				msleep(75);
			gpio_set_value(sn65dsi85_rst, 0);
			msleep(900);
		}
		#endif
		gpio_set_value(led_en_lvds, 0);
		set_pwm_for_lvds_panel(ctrl_pdata,0);
		splash_flag=0;
	}


	msleep(100);
	printk("sn65dsi85_dsi2lvds_enable_early out\n\n");
}

void sn65dsi85_dsi2lvds_print_status(void)
{
	struct sn65dsi85_platform_data *dsi2lvds = sn65dsi85_dsi2lvds_pdata;
	unsigned int val = 0;
	int indx = 0;
	for (indx = 0xE0; indx <= 0xE6; indx++) {
		sn65dsi85_reg_read(dsi2lvds, indx, &val);
		printk("%x:%x\n", indx,val);
		msleep(1);
	}
}

//Leo Guo modify following for android animation issue



void panel_power_detect(int gpio_index)
{

	int value=0;
	int times=0;

	if (gpio_is_valid(gpio_index))
	{
		while(1)
		{
			value=gpio_get_value(gpio_index);
			if(value)
				break;
			mdelay(5);
			times++;
			if(times==200)
			{
				pr_info("panel_power_detect timeout!!!\n");
				break;
			}
		}
	}
}
extern int edp_init_first;


void sn65dsi85_dsi2lvds_enable_reset(void)
{
	struct sn65dsi85_platform_data *dsi2lvds = sn65dsi85_dsi2lvds_pdata;
	unsigned int val = 0;
	int timeout = 0;
	PANEL_ID_TYPE qisda_panel_id=socinfo_get_panel_id();

	gpio_set_value(sn65dsi85_rst, 0);
	msleep(50);
	gpio_set_value(sn65dsi85_rst, 1);
	msleep(5);
	//gpio_set_value(led_en_lvds, 1);

	sn65dsi85_reg_write(dsi2lvds, SN65DSI85_SOFT_RESET, 0x01);
	sn65dsi85_reg_write(dsi2lvds, SN65DSI85_PLL_EN, 0x00);
	/* STEP3: set lvds clock 71MHZ and clock source is from mipi D-PHY */
	sn65dsi85_reg_write(dsi2lvds, SN65DSI85_PLL_REFCLK_CFG, 0x05);//0x0A   62.5~87.5MHZ   MIPI D-PHY for P clock

	/* STEP3: Single channelA 4 DSI lanes */
	sn65dsi85_reg_write(dsi2lvds, SN65DSI85_DSI_CFG1, 0x20);//0x10

	/*lvds config*/
	if(qisda_panel_id==TM101JDHP01_00)
	{
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_LVDS_CLK_CFG, 0x10);//0X0B  DIVIDE 6
		/*set dsi clock 415MHZ*/
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_DSI_CHA_CLK_RANGE, 0x53);
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_LVDS_CFG1, 0x18);//00011000  7A  18  78   1A
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_LVDS_CFG2, 0x0F);
		//sn65dsi85_reg_write(dsi2lvds, SN65DSI85_LVDS_CFG3, 0x53);
		//sn65dsi85_reg_write(dsi2lvds, SN65DSI85_LVDS_CFG4, 0x53);
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_VIDEO_CHA_LINE_LOW, 0x00);
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_VIDEO_CHA_LINE_HIGH, 0x05);//1280
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_VERT_DISP_SIZE_LOW, 0x20);
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_VERT_DISP_SIZE_HIGH, 0x03);//800
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_SYNC_DELAY_LOW, 0x00);//
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_SYNC_DELAY_HIGH, 0x02);//
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_HSYNC_PULSE_WIDTH_LOW, 0x01);//HSYNC width
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_HSYNC_PULSE_WIDTH_HIGH,0x00);
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_HORIZONTAL_BACK_PORCH, 0x05);//H Back porch
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_HORIZONTAL_FRONT_PORCH, 0x40);//H Front porch
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_VSYNC_PULSE_WIDTH_LOW, 0x01);//V width
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_VSYNC_PULSE_WIDTH_HIGH,0x00);
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_VERTICAL_FRONT_PORCH, 0x28);//V Front porch
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_VERTICAL_BACK_PORCH, 0x02);//V Back porch
	}
	else
	{
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_LVDS_CLK_CFG, 0x20);//0X0B  DIVIDE 6
		/*set dsi clock 415MHZ*/
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_DSI_CHA_CLK_RANGE, 0x57);
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_LVDS_CFG1, 0x0C);//00001100
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_LVDS_CFG2, 0x00);
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_LVDS_CFG3, 0x03);
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_LVDS_CFG4, 0x00);
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_VIDEO_CHA_LINE_LOW, 0x80);
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_VIDEO_CHA_LINE_HIGH, 0x07);//1920
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_VERT_DISP_SIZE_LOW, 0x00);
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_VERT_DISP_SIZE_HIGH, 0x00);//1080
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_SYNC_DELAY_LOW, 0x00);//
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_SYNC_DELAY_HIGH, 0x02);//
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHB_SYNC_DELAY_LOW, 0x00);//
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHB_SYNC_DELAY_HIGH, 0x02);//
		//channel A
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_HSYNC_PULSE_WIDTH_LOW, 0x80);//HSYNC width
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_HSYNC_PULSE_WIDTH_HIGH,0x00);
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_HORIZONTAL_BACK_PORCH, 0x06);
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_HORIZONTAL_FRONT_PORCH, 0x06);//H Front porch
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_VSYNC_PULSE_WIDTH_LOW, 0x20);//V width
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_VSYNC_PULSE_WIDTH_HIGH,0x00);
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_VERTICAL_FRONT_PORCH, 0x05);//V Front porch
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_VERTICAL_BACK_PORCH, 0x08);//V Back porch
	}
	sn65dsi85_reg_write(dsi2lvds, 0xE5, 0xff);

		/* PLL ENABLE */
	sn65dsi85_reg_write(dsi2lvds, SN65DSI85_PLL_EN, 0x01);
	msleep(5);

	 /* STEP12: check DP_PLL_LOCK */
	do{
		val = 0;
		sn65dsi85_reg_read(dsi2lvds, SN65DSI85_PLL_REFCLK_CFG, &val);
		if((val & 0x80) != 0)
		{
			printk("sn65dsi85_dsi2lvds_enable DP_PLL_LOCK=%x(LOCK)\n",val);
		}
		else{
			printk("sn65dsi85_dsi2lvds_enable DP_PLL_LOCK=%x(UNLOCK)\n",val);
			msleep(10);
			timeout++;
		}
	} while ( ((val & 0x80) == 0) && (timeout < 5) );

	//sn65dsi85_reg_write(dsi2lvds, SN65DSI85_SOFT_RESET, 0x00);


	sn65dsi85_reg_write(dsi2lvds, SN65DSI85_SOFT_RESET, 0x00);

	sn65dsi85_reg_read(dsi2lvds, 0xE5, &val);
	printk("sn65dsi85_reg_read 0xE5=%x\n",val);
	msleep(200);
	//sn65dsi85_reg_write(dsi2lvds, 0xE5, val);
	//msleep(200);
	sn65dsi85_reg_write(dsi2lvds, SN65DSI85_SOFT_RESET, 0x00);
	sn65dsi85_dsi2lvds_print_status();


	printk("sn65dsi85_dsi2lvds_enable_reset end\n");
	return;
}

//Leo Guo add for lvds IC error issue
extern void reset_mipi_for_edp(void);


struct timeval time_start, time_end;

void sn65dsi85_dsi2lvds_start_pll(void)
{
	struct sn65dsi85_platform_data *dsi2lvds = sn65dsi85_dsi2lvds_pdata;
	PANEL_ID_TYPE qisda_panel_id = socinfo_get_panel_id();
	unsigned int val = 0;
	int timeout = 0;

	if (dsi2lvds && dsi2lvds->dsi2lvds_enabled)
		return;
	if(sn65dsi85_dsi2lvds_pdata==NULL)
		return;

	pr_info("%s start\n", __func__);

	//sn65dsi85_reg_write(dsi2lvds, 0xE5, 0xff);

	/* PLL ENABLE */
	sn65dsi85_reg_write(dsi2lvds, SN65DSI85_PLL_EN, 0x01);
	msleep(5);

	 /* STEP12: check DP_PLL_LOCK */
	do{
		val = 0;
		sn65dsi85_reg_read(dsi2lvds, SN65DSI85_PLL_REFCLK_CFG, &val);
		if((val & 0x80) != 0)
		{
			printk("sn65dsi85_dsi2lvds_enable DP_PLL_LOCK=%x(LOCK)\n",val);
		}
		else{
			printk("sn65dsi85_dsi2lvds_enable DP_PLL_LOCK=%x(UNLOCK)\n",val);
			msleep(10);
			timeout++;
		}
	} while ( ((val & 0x80) == 0) && (timeout < 5) );

	sn65dsi85_reg_write(dsi2lvds, SN65DSI85_SOFT_RESET, 0x01);

	gpio_set_value(panel_en1, 1);
	if(qisda_panel_id == TM101JDHP01_00)
		panel_power_detect(panel_pwr_det1);
	else
		panel_power_detect(panel_pwr_det2);

	gpio_set_value(panel_en2, 1);

	msleep(10);

	pr_info("%s exit\n", __func__);
}

void sn65dsi85_dsi2lvds_enable_2nd_start_pll(void)
{
	struct sn65dsi85_platform_data *dsi2lvds = sn65dsi85_dsi2lvds_pdata;
	struct irq_desc *lvds_irq_desc = irq_to_desc(lvds_irq);
	//PANEL_ID_TYPE qisda_panel_id = socinfo_get_panel_id();
	unsigned int val = 0;
	static int first_time=1;
	static int times=0;//Leo Guo add for lvds IC error issue
	int count=0;

	if( first_time)
	{
		printk("sn65dsi85_dsi2lvds_enable_2nd_start_pll start\n");
		first_time=0;
		edp_init_first=1;
		return;
	}

	if (dsi2lvds && dsi2lvds->dsi2lvds_enabled)
		return;
	if(sn65dsi85_dsi2lvds_pdata==NULL)
		return;
	printk("sn65dsi85_dsi2lvds_enable_2nd_start_pll()\n");

	//Leo Guo add for lvds IC error issue: clear error status
	sn65dsi85_reg_write(dsi2lvds, 0xE5, 0xFF);

	msleep(5);
	sn65dsi85_reg_read(dsi2lvds, 0xE5, &val);

	val &=0xFE;
	if(val!=0)
	{
		printk("E5 ERROR!!!!! E5=%d\n",val);
		goto retry;
	}

	sn65dsi85_reg_write(dsi2lvds, 0xF9, 0x0d);
	msleep(198);
	sn65dsi85_dsi2lvds_print_status();

	//printk("current time is %d:%d:%d\n",hour,minute,second);
	irq_detected_flag = 0;
	irq_times = 0;
	do_gettimeofday(&time_start);
	//printk("start time is %ld,%ld\n",time_start.tv_sec,time_start.tv_usec);
	pr_info("enable lvds irq!!!\n");
	if((lvds_irq_desc->irq_data.state_use_accessors & IRQD_IRQ_DISABLED) == IRQ_STATUS_DISABLED )
		enable_irq(lvds_irq);

	while(1)
	{
		count++;
		if(irq_detected_flag)
		{
			printk("At least %d irq happened within 500ms,lvds output is OK!\n", irq_times);
			break;
		}
		msleep(10);
		if(count==50)
		{
			printk("500ms timeout,no enough irq happened(irq times: %d)!!!\n", irq_times);
			break;
		}
	}

	if((lvds_irq_desc->irq_data.state_use_accessors & IRQD_IRQ_DISABLED) == IRQ_STATUS_ENABLED )
		disable_irq_nosync(lvds_irq);

	sn65dsi85_reg_read(dsi2lvds, 0xE5, &val);
	printk("Read E5 again,value is %d\n",val);
	val &= 0xFE;//mark unlock bit

retry:

#if 1
//Leo Guo add for lvds IC error issue
	if((irq_detected_flag==0)||(val!=0))
	{
		times++;
		pr_err("lvds signal no output times = %d\n", times);
		if(times<4)
			queue_delayed_work(dsi2lvds->workq, &dsi2lvds->sn65dsi85__work_id, 20);
		else
		{
			pr_err("%s: retry %d times, init maybe failed\n", __func__, times);
			times = 0;
		}
	}
	else
		times=0;
#endif
	dsi2lvds->dsi2lvds_enabled=true;

	printk("sn65dsi85_dsi2lvds_enable_2nd_start_pll() end\n");
	return;
}


void enable_lvds(int enable)
{
	if (dsi2lvds && dsi2lvds->dsi2lvds_enabled)
		return;

	gpio_set_value(sn65dsi85_rst, enable);
	pr_info("%s LVDS IC\n", enable ? "enable":"disable");
	msleep(5);
}

void sn65dsi85_dsi2lvds_enable_1st_init_CSR(void)
{
	struct sn65dsi85_platform_data *dsi2lvds = sn65dsi85_dsi2lvds_pdata;
	//unsigned int val = 0;
	//int timeout = 0;
	PANEL_ID_TYPE qisda_panel_id=socinfo_get_panel_id();


	if (dsi2lvds && dsi2lvds->dsi2lvds_enabled)
		return;
	if(sn65dsi85_dsi2lvds_pdata==NULL)
		return;
	printk("sn65dsi85_dsi2lvds_enable_1st_init_CSR\n");

	sn65dsi85_reg_write(dsi2lvds, SN65DSI85_SOFT_RESET, 0x01);
	sn65dsi85_reg_write(dsi2lvds, SN65DSI85_PLL_EN, 0x00);
	/* STEP3: set lvds clock 71MHZ and clock source is from mipi D-PHY */
	sn65dsi85_reg_write(dsi2lvds, SN65DSI85_PLL_REFCLK_CFG, 0x05);//0x0A   62.5~87.5MHZ   MIPI D-PHY for P clock

	/* STEP3: Single channelA 4 DSI lanes */
	sn65dsi85_reg_write(dsi2lvds, SN65DSI85_DSI_CFG1, 0x20);//0x10

	/*lvds config*/
	if(qisda_panel_id==TM101JDHP01_00)
	{
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_LVDS_CLK_CFG, 0x10);//0X0B  DIVIDE 3
		/*set dsi clock 415MHZ*/
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_DSI_CHA_CLK_RANGE, 0x53);
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_LVDS_CFG1, 0x18);//00011000  7A  18  78   1A
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_LVDS_CFG2, 0x0F);
		//sn65dsi85_reg_write(dsi2lvds, SN65DSI85_LVDS_CFG3, 0x53);
		//sn65dsi85_reg_write(dsi2lvds, SN65DSI85_LVDS_CFG4, 0x53);
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_VIDEO_CHA_LINE_LOW, 0x00);
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_VIDEO_CHA_LINE_HIGH, 0x05);//1280
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_VERT_DISP_SIZE_LOW, 0x20);
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_VERT_DISP_SIZE_HIGH, 0x03);//800
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_SYNC_DELAY_LOW, 0x00);//
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_SYNC_DELAY_HIGH, 0x02);//
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_HSYNC_PULSE_WIDTH_LOW, 0x01);//HSYNC width
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_HSYNC_PULSE_WIDTH_HIGH,0x00);
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_HORIZONTAL_BACK_PORCH, 0x05);//H Back porch
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_HORIZONTAL_FRONT_PORCH, 0x40);//H Front porch
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_VSYNC_PULSE_WIDTH_LOW, 0x01);//V width
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_VSYNC_PULSE_WIDTH_HIGH,0x00);
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_VERTICAL_FRONT_PORCH, 0x28);//V Front porch
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_VERTICAL_BACK_PORCH, 0x02);//V Back porch
	}
	else
	{
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_LVDS_CLK_CFG, 0x20);//0X0B  DIVIDE 6
		/*set dsi clock 415MHZ*/
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_DSI_CHA_CLK_RANGE, 0x57);
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_LVDS_CFG1, 0x0C);//00001100
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_LVDS_CFG2, 0x00);
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_LVDS_CFG3, 0x03);
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_LVDS_CFG4, 0x00);
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_VIDEO_CHA_LINE_LOW, 0x80);
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_VIDEO_CHA_LINE_HIGH, 0x07);//1920
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_VERT_DISP_SIZE_LOW, 0x00);
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_VERT_DISP_SIZE_HIGH, 0x00);//1080
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_SYNC_DELAY_LOW, 0x00);//
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_SYNC_DELAY_HIGH, 0x02);//
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHB_SYNC_DELAY_LOW, 0x00);//
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHB_SYNC_DELAY_HIGH, 0x02);//
		//channel A
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_HSYNC_PULSE_WIDTH_LOW, 0x80);//HSYNC width
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_HSYNC_PULSE_WIDTH_HIGH,0x00);
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_HORIZONTAL_BACK_PORCH, 0x06);
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_HORIZONTAL_FRONT_PORCH, 0x06);//H Front porch
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_VSYNC_PULSE_WIDTH_LOW, 0x20);//V width
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_VSYNC_PULSE_WIDTH_HIGH,0x00);
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_VERTICAL_FRONT_PORCH, 0x05);//V Front porch
		sn65dsi85_reg_write(dsi2lvds, SN65DSI85_CHA_VERTICAL_BACK_PORCH, 0x08);//V Back porch
	}


	printk("sn65dsi85_dsi2lvds_enable_1st_init_CSR end\n");
	return;
}


void sn65dsi85_dsi2lvds_disable(void)
{

	printk("sn65dsi85_dsi2lvds_disable\n");

	/* PLL ENABLE disable */
	if(sn65dsi85_dsi2lvds_pdata==NULL)
		return;

	sn65dsi85_reg_write(dsi2lvds, SN65DSI85_PLL_EN, 0x00);

	dsi2lvds->dsi2lvds_enabled = false;

	gpio_set_value(sn65dsi85_rst, 0);
	msleep(1000);
	printk("sn65dsi85_dsi2lvds_disable  out\n");
}



static struct i2c_device_id sn65dsi85_id[] = {
	{ "sn65dsi85_dsi2lvds", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, sn65dsi85_id);

static ssize_t device_name_rda_attr(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct sn65dsi85_platform_data *dsi2lvds = sn65dsi85_dsi2lvds_pdata;
	unsigned int val = 0;
	int indx = 0;
	for (indx = 0x09; indx <= 0xE6; indx++) {
		sn65dsi85_reg_read(dsi2lvds, indx, &val);
		printk("%x:%x\n", indx,val);
		msleep(1);
	}
	//gpio_set_value(sn65dsi85_rst, 0);

	//msleep(100);

	//sn65dsi85_dsi2lvds_enable_lately_for_lvds_reset();
	sn65dsi85_dsi2lvds_enable_reset();

	return 64;
}

static DEVICE_ATTR(device_name, S_IRUGO, device_name_rda_attr, NULL);


static struct attribute *lvds_sysfs_attrs[] = {
	&dev_attr_device_name.attr,
	NULL,
};

static struct attribute_group lvds_sysfs_attr_grp = {
	.attrs = lvds_sysfs_attrs,
};

int sndsi85_helper_sysfs_init(struct device *dev)
{
	int rc = 0;

	if (!dev) {
		pr_err("%s: Invalid params\n", __func__);
		return -EINVAL;
	}

	rc = sysfs_create_group(&dev->kobj, &lvds_sysfs_attr_grp);
	if (rc)
		pr_err("%s: sysfs group creation failed %d\n", __func__, rc);

	return rc;
}



MODULE_DEVICE_TABLE(i2c, sn65dsi85_id);

//Leo Guo add for lvds IC error issue

static void sn65dsi85_work(struct work_struct *work)
{
	pr_info("sn65dsi85_work!!!!\n");
	reset_mipi_for_edp();

}

static irqreturn_t lvds_2mipi_irq(int irq, void *data)
{
	//unsigned int val=0;
	int time_diff=0;
	struct irq_desc *desc = irq_to_desc(irq);

	//printk("lvds_2mipi_irq occured!!!\n");
	irq_times++;
	#if 0
	do_gettimeofday(&time_end);
	//printk("irq time is %ld,%ld\n",time_end.tv_sec,time_end.tv_usec);

	time_diff=(time_end.tv_sec-time_start.tv_sec)*1000000+time_end.tv_usec-time_start.tv_usec;
	//printk("time_diff =%d\n",time_diff);

	if(time_diff >= 500000)
	{
		printk("500ms time out,total irq time is %d\n",irq_times-1);
		time_start.tv_sec=time_end.tv_sec;
		time_start.tv_usec=time_end.tv_usec;
		if(irq_times > 25)
		{
			irq_detected_flag=1;
		}
		irq_times=0;
	}
	#else

	if(irq_times >= 25)
	{
		if((desc->irq_data.state_use_accessors & IRQD_IRQ_DISABLED) == IRQ_STATUS_ENABLED )
			disable_irq_nosync(irq);

		do_gettimeofday(&time_end);
		//printk("irq time is %ld,%ld\n",time_end.tv_sec,time_end.tv_usec);

		time_diff=(time_end.tv_sec-time_start.tv_sec)*1000000+time_end.tv_usec-time_start.tv_usec;
		//printk("total %d irq times at %dms\n",irq_times-1, time_diff/1000);
		printk("detected %d irq at %dms\n",irq_times, time_diff/1000);
		if( time_diff <= 500000 )
		{
			irq_detected_flag = 1;
		}
	}
	#endif
	return IRQ_HANDLED;
}

static int sn65dsi85_i2c_probe(struct i2c_client *client,
                               const struct i2c_device_id *id)
{
	int err = 0;
	int ret = 0;
	struct sn65dsi85_platform_data *pdata;
	struct device_node *np = client->dev.of_node;

	unsigned int val = 0;
	unsigned int value[8] = {0};

	printk("sn65dsi85_i2c_probe\n");
	pdata = devm_kzalloc(&client->dev,
		sizeof(struct sn65dsi85_platform_data), GFP_KERNEL);

	if (!pdata)
		return -ENOMEM;
	else
		sn65dsi85_dsi2lvds_pdata = pdata;
	sndsi85_helper_sysfs_init(&client->dev);
	dsi2lvds=sn65dsi85_dsi2lvds_pdata;

	led_en_lvds = of_get_named_gpio(np, "led_en-gpio", 0);
	panel_en1 = of_get_named_gpio(np, "panel_en-gpio1", 0);
	panel_en2 = of_get_named_gpio(np, "panel_en-gpio2", 0);
	sn65dsi85_rst = of_get_named_gpio(np, "mipi_edp_rst-gpio", 0);
	panel_pwr_det1 = of_get_named_gpio(np, "panel_pwr_det1", 0);
	panel_pwr_det2 = of_get_named_gpio(np, "panel_pwr_det2", 0);
	panel_pwr_det3 = of_get_named_gpio(np, "panel_pwr_det3", 0);
	mipi_int = of_get_named_gpio(np, "mipi_int_gpio", 0);//Leo Guo add for lvds IC error issue
	printk("led_en_lvds = %d\n", led_en_lvds);
	printk("panel_en1 = %d\n", panel_en1);
	printk("panel_en2 = %d\n", panel_en2);
	printk("sn65dsi85_rst = %d\n", sn65dsi85_rst);
	printk("panel power detect 1 2 3 = %d,%d,%d\n", panel_pwr_det1,panel_pwr_det2,panel_pwr_det3);
	printk("mipi_int = %d\n", mipi_int);
//Leo Guo add for lvds IC error issue
	if (gpio_is_valid(mipi_int)) {
		ret = devm_gpio_request_one(&client->dev, mipi_int,
					    GPIOF_DIR_IN, "mipi_int");
		gpio_export(mipi_int,true);
		if (ret < 0) {
			printk("could not acquire mipi_int gpio (err=%d)\n", ret);
		}
	}


	if (gpio_is_valid(panel_en1)) {
		ret = devm_gpio_request_one(&client->dev, panel_en1,
					    GPIOF_OUT_INIT_HIGH, "panel_en1");
		gpio_export(panel_en1,true);
		if (ret < 0) {
			printk("could not acquire enable gpio (err=%d)\n", ret);
		}
	}
	msleep(10);

	if (gpio_is_valid(panel_en2)) {
		ret = devm_gpio_request_one(&client->dev, panel_en2,
									GPIOF_OUT_INIT_HIGH, "panel_en2");
		gpio_export(panel_en2,true);
		if (ret < 0) {
			printk("could not acquire enable gpio (err=%d)\n", ret);
		}
	}
	msleep(10);

	if (gpio_is_valid(sn65dsi85_rst)) {
		ret = devm_gpio_request_one(&client->dev, sn65dsi85_rst,
					    GPIOF_OUT_INIT_HIGH, "sn65dsi85_rst");
		gpio_export(sn65dsi85_rst,true);
		if (ret < 0) {
			printk("could not acquire sn65dsi85_rst gpio (err=%d)\n", ret);
		}
	}
	if (gpio_is_valid(led_en_lvds)) {
		ret = devm_gpio_request_one(&client->dev, led_en_lvds,
									GPIOF_OUT_INIT_HIGH, "led_en_lvds");
		gpio_export(led_en_lvds,true);
		if (ret < 0) {
			printk("could not acquire enable gpio (err=%d)\n", ret);
		}
	}
	msleep(10);

	sn65dsi85_i2c_client = client;
	printk("sn65dsi85_i2c_client addr = %x\n", sn65dsi85_i2c_client->addr);
	err = sn65dsi85_dsi2lvds_init();
	if(err != 0)
	{
		pr_err("sn65dsi85_dsi2lvds_init error=%d\n",err);
		return err;
	}


	sn65dsi85_reg_read(sn65dsi85_dsi2lvds_pdata, 0x00, &value[0]);
	sn65dsi85_reg_read(sn65dsi85_dsi2lvds_pdata, 0x01, &value[1]);
	sn65dsi85_reg_read(sn65dsi85_dsi2lvds_pdata, 0x02, &value[2]);
	sn65dsi85_reg_read(sn65dsi85_dsi2lvds_pdata, 0x03, &value[3]);
	sn65dsi85_reg_read(sn65dsi85_dsi2lvds_pdata, 0x04, &value[4]);
	//sn65dsi85_reg_read(sn65dsi85_dsi2lvds_pdata, 0x05, &value[5]);
	//sn65dsi85_reg_read(sn65dsi85_dsi2lvds_pdata, 0x06, &value[6]);
	//sn65dsi85_reg_read(sn65dsi85_dsi2lvds_pdata, 0x07, &value[7]);
	sn65dsi85_reg_read(sn65dsi85_dsi2lvds_pdata, 0x08, &val);
//Leo Guo add for ftd to read ic id
	edp_device_id[0]=value[4];
	edp_device_id[1]=value[3];
	edp_device_id[2]=value[2];
	edp_device_id[3]=value[1];
	edp_device_id[4]=value[0];
	edp_device_id[5]=0;
	edp_ic_version=val;
	printk("sn65dsi85 device id is %s,version is %d\n",edp_device_id,edp_ic_version);
//Leo Guo add for lvds IC error issue
	pdata->workq = create_workqueue("sn65dsi85_workq");
	if (!pdata->workq) {
		pr_err("%s: workqueue creation failed.\n", __func__);
		return -EPERM;
	}
	INIT_DELAYED_WORK(&pdata->sn65dsi85__work_id, sn65dsi85_work);
	mutex_init(&lock_for_lvds);
	lvds_irq = gpio_to_irq(mipi_int);

	ret = request_threaded_irq(lvds_irq, NULL, lvds_2mipi_irq,
		IRQF_TRIGGER_RISING | IRQF_ONESHOT, "MIPI2LVDS", dsi2lvds);
	if (ret) {
		pr_err("%s: Failed to enable edp interrupt\n",
				__func__);
	}
	disable_irq(lvds_irq);



	return err;
}

static int sn65dsi85_i2c_remove(struct i2c_client *client)
{
	sn65dsi85_dsi2lvds_disable();
	sn65dsi85_i2c_client = NULL;
	sn65dsi85_dsi2lvds_destroy();
	return 0;
}

static struct i2c_driver sn65dsi85_i2c_drv = {
	.driver = {
		.name = "sn65dsi85_dsi2lvds",
		.owner = THIS_MODULE,
	},
		.probe = sn65dsi85_i2c_probe,
		.remove = sn65dsi85_i2c_remove,
		.id_table = sn65dsi85_id,
};

static int __init sn65dsi85_i2c_client_init(void)
{
	int err = 0;

	err = i2c_add_driver(&sn65dsi85_i2c_drv);
	if (err)
		pr_err("sn65dsi85_dsi2lvds: Failed to add i2c client driver\n");

	return err;
}

static void __exit sn65dsi85_i2c_client_exit(void)
{
	i2c_del_driver(&sn65dsi85_i2c_drv);
}

subsys_initcall(sn65dsi85_i2c_client_init);
module_exit(sn65dsi85_i2c_client_exit);

MODULE_AUTHOR("Bibek Basu <bbasu@nvidia.com>");
MODULE_DESCRIPTION(" TI SN65DSI85 dsi bridge to lvds");
MODULE_LICENSE("GPL");



