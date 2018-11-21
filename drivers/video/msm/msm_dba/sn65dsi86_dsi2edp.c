/*
 * drivers/video/tegra/dc/sn65dsi86_dsi2edp.c
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
 #include "sn65dsi86_dsi2edp.h"
 #include <linux/of_device.h>
 #include <linux/gpio.h>
 #include <linux/interrupt.h>
 #include <linux/of_gpio.h>
 #include <linux/of_irq.h>
 #include <soc/qcom/socinfo.h>

 //#define DEBUG_COLOR_BAR   // uncomment this defined for colorbar testing
 #define COLOR_BAR_TYPE  (0)  // should 0 ~ 7
 //Leo Guo add for ftd to read ic id
 static char edp_device_id[6];
 static int edp_ic_version=0;
 module_param(edp_ic_version, int, 0444);

 static struct sn65dsi86_platform_data *sn65dsi86_dsi2edp_pdata = NULL;
 static struct i2c_client *sn65dsi86_i2c_client = NULL;
 static struct sn65dsi86_platform_data *dsi2edp=NULL;
 #if 0
 static int PANEL_SUPPORT_ASSR=0;
 static int PANEL_SUPPORT_ENHANCE_FRAMING=0;
 static int PANEL_SUPPORT_MAX_LINK_RATE=0;
 #endif

 enum i2c_transfer_type {
 	I2C_WRITE,
 	I2C_READ,
 };
 
 static inline int sn65dsi86_reg_write(struct sn65dsi86_platform_data *dsi2edp,
                                       unsigned int addr, unsigned int val)
 {
       int error;
       error = regmap_write(dsi2edp->regmap, addr, val);
       if(error != 0)
           printk("sn65dsi86_reg_write error=%d\n",error);
       return error;
 }

 static inline void sn65dsi86_reg_read(struct sn65dsi86_platform_data *dsi2edp,
                                       unsigned int addr, unsigned int *val)
 {
       int error;
       error = regmap_read(dsi2edp->regmap, addr, val);
       if(error != 0)
          printk("sn65dsi86_reg_read error =%d addr=%x val=%x\n",error,addr,*val);
 }

 static const struct regmap_config sn65dsi86_regmap_config = {
        .reg_bits = 8,
        .val_bits = 8,
 };


 static int sn65dsi86_dsi2edp_init(void)
 {
 	int err = 0;

	printk("sn65dsi86_dsi2edp_init\n");
 	if (!sn65dsi86_dsi2edp_pdata)
          return -ENOMEM;

 	mutex_init(&sn65dsi86_dsi2edp_pdata->lock);
 	sn65dsi86_dsi2edp_pdata->dsi2edp_enabled = false;
	sn65dsi86_dsi2edp_pdata->client_i2c = sn65dsi86_i2c_client;
	sn65dsi86_dsi2edp_pdata->regmap =  devm_regmap_init_i2c(sn65dsi86_i2c_client,
                                                         &sn65dsi86_regmap_config);

        if (IS_ERR(sn65dsi86_dsi2edp_pdata->regmap)) {
            err = PTR_ERR(sn65dsi86_dsi2edp_pdata->regmap);
            goto fail;
        }

        printk("sn65dsi86_dsi2edp_init end\n");

fail:
        return err;
}

static void sn65dsi86_dsi2edp_destroy(void)
{

       if (!dsi2edp)
            return;

       mutex_destroy(&dsi2edp->lock);
       regmap_exit(dsi2edp->regmap);
       sn65dsi86_dsi2edp_pdata = NULL;
}


static void sn65dsi86_dsi2edp_write_dpcd(unsigned short int* pbuff, unsigned long int v_addr, unsigned long int cnt)
{
	unsigned int tmp[4];
	int i;
	unsigned int val = 0;

	//AUX 20bit address
	tmp[0] = (v_addr>>16) & 0x0F;
	tmp[1] = (v_addr>>8) & 0xFF;
	tmp[2] = (v_addr & 0xFF);

	//Length
	tmp[3] = (cnt & 0x1F);

	//write AUX address and Length to sn65dsi86
      sn65dsi86_reg_write(dsi2edp, SN65DSI86_AUX_ADDR_19_16, tmp[0]);
      sn65dsi86_reg_write(dsi2edp, SN65DSI86_AUX_ADDR_15_8, tmp[1]);
      sn65dsi86_reg_write(dsi2edp, SN65DSI86_AUX_ADDR_7_0, tmp[2]);
      sn65dsi86_reg_write(dsi2edp, SN65DSI86_AUX_LENGTH, tmp[3]);

      //AUX write data buff
      for(i=0; i<cnt; i++){
      	  val = (unsigned int)pbuff[i];
         sn65dsi86_reg_write(dsi2edp, SN65DSI86_AUX_WDATA0+i,val);
      }

      //AUX_NATIVE_CMD(Native Aux write = 0x08)
      tmp[0] = (8<<4) | (1<<0); //AUX_NATIVE_READ , EN_SEND_AUX
      sn65dsi86_reg_write(dsi2edp,SN65DSI86_AUX_CMD_SEND,tmp[0]);

      //wait writting
      usleep_range(10000, 12000);
}

#if 0
static void sn65dsi86_dsi2edp_read_dpcd(unsigned short int* pbuff, unsigned long int v_addr, unsigned long int cnt)
{
	unsigned int tmp[4];
	int i;
	unsigned int val = 0;
	struct sn65dsi86_platform_data *dsi2edp = sn65dsi86_dsi2edp_pdata;

	//AUX 20bit address
	tmp[0] = (v_addr>>16) & 0x0F;
	tmp[1] = (v_addr>>8) & 0xFF;
	tmp[2] = (v_addr & 0xFF);

	//Length
	tmp[3] = (cnt & 0x1F);

      //write AUX address and Length to sn65dsi86
      sn65dsi86_reg_write(dsi2edp, SN65DSI86_AUX_ADDR_19_16, tmp[0]);
      sn65dsi86_reg_write(dsi2edp, SN65DSI86_AUX_ADDR_15_8, tmp[1]);
      sn65dsi86_reg_write(dsi2edp, SN65DSI86_AUX_ADDR_7_0, tmp[2]);
      sn65dsi86_reg_write(dsi2edp, SN65DSI86_AUX_LENGTH, tmp[3]);

      //AUX_NATIVE_CMD (Native Aux Read = 0x09)
      tmp[0] = (9<<4) | (1<<0); //AUX_NATIVE_READ , EN_SEND_AUX
      sn65dsi86_reg_write(dsi2edp, SN65DSI86_AUX_CMD_SEND, tmp[0]);

      //wait response
      usleep_range(10000, 12000);
      for(i=0; i<cnt; i++)
      {
      	val = 0;
      	sn65dsi86_reg_read(dsi2edp, SN65DSI86_AUX_RDATA0+i, &val);
      	pbuff[i] = (unsigned short int)val;
      }
}


static void sn65dsi86_dsi2edp_showDPCDInfo(void)
{
	unsigned short int buf[16];

	memset((void *)buf, 0 ,sizeof(buf));
	sn65dsi86_dsi2edp_read_dpcd(buf, 0, 16);

	printk("DPCD: REV:%d.%d, MAX_LINK_RATE:", (buf[0] >> 4), (buf[0]&0xF));

	if (buf[1] == 0x06) {
                printk("1.62Gbps\n");
                PANEL_SUPPORT_MAX_LINK_RATE = MAX_LINK_RATE_1P6Gbps;
       } else if (buf[1] == 0x0A) {
                printk("2.7Gbps\n");
                PANEL_SUPPORT_MAX_LINK_RATE = MAX_LINK_RATE_2P7Gbps;
       }

       printk(" MAX_LINK_LANE:%d\n", buf[2]);

       if (buf[0x0D] & ASSR_SUPPORT) {
                printk(" support ASSR\n");
                PANEL_SUPPORT_ASSR =1;
        } else {
                printk(" not support ASSR\n");
                PANEL_SUPPORT_ASSR =0;
        }
        if (buf[0x0D] & ENHANCE_FRAMING) {
                printk(" support Enhance framing\n");
                PANEL_SUPPORT_ENHANCE_FRAMING=1;
        } else {
                printk(" not support Enhance framing\n");
                PANEL_SUPPORT_ENHANCE_FRAMING=0;
        }
}
#endif
//Leo Guo add following for cont splash issue
//extern int  bl_en,pe_vcc;
static int sn65dsi86_en=-1;
static int sn65dsi86_rst=-1;
static int puck_redriver_en=-1;
static int edp_irq_gpio=-1;
int panel_en=-1;
int backlight_en=-1;
int led_en_edp=-1;
static int splash_flag=1;

void sn65dsi86_dsi2edp_enable_early(void)
{

	printk("sn65dsi86_dsi2edp_enable_early\n\n");
	if(dsi2edp==NULL)
		return;
	if (dsi2edp && dsi2edp->dsi2edp_enabled)
		return;
	if(splash_flag)
	{
		gpio_direction_output(panel_en,1);//Leo Guo modify for I2.0 15.6" panel
		gpio_direction_output(backlight_en,1);
		//gpio_set_value(panel_en, 0);
		//gpio_set_value(backlight_en, 0);
		//gpio_set_value(led_en_edp,0);
		//splash_flag=0;
	}
	gpio_set_value(panel_en, 1);
	msleep(130);
}

void sn65dsi86_dsi2edp_print_status(void)
{
	//struct sn65dsi86_platform_data *dsi2edp = sn65dsi86_dsi2edp_pdata;
	//unsigned int val = 0;

	//sn65dsi86_reg_read(dsi2edp, SN65DSI86_PLL_REFCLK_CFG, &val);
	//printk("Alex_Fang:sn65dsi86_dsi2edp_print_status SN65DSI86_PLL_REFCLK_CFG=%d\n",val);

	//sn65dsi86_reg_read(dsi2edp, SN65DSI86_COLOR_BAR_CFG, &val);
	//printk("Alex_Fang:sn65dsi86_dsi2edp_print_status SN65DSI86_COLOR_BAR_CFG=%d\n",val);

	//sn65dsi86_reg_read(dsi2edp, SN65DSI86_FRAMING_CFG, &val);
	//printk("Alex_Fang:sn65dsi86_dsi2edp_print_status SN65DSI86_FRAMING_CFG=%d\n",val);

	//sn65dsi86_reg_read(dsi2edp, SN65DSI86_ML_TX_MODE, &val);
	//printk("Alex_Fang:sn65dsi86_dsi2edp_print_status SN65DSI86_ML_TX_MODE=%d\n",val);

	//sn65dsi86_reg_read(dsi2edp, SN65DSI86_DSI_CHA_CLK_RANGE, &val);
	//printk("Alex_Fang:sn65dsi86_dsi2edp_print_status SN65DSI86_DSI_CHA_CLK_RANGE=%d\n",val);

	//printk("=============================================================\n");
	//sn65dsi86_reg_read(dsi2edp, SN65DSI86_IRQ_STATUS0, &val);
	//printk("Alex_Fang:sn65dsi86_dsi2edp_print_status IRQ0 STATUS=%d\n", val);
	//printk("Alex_Fang:sn65dsi86_dsi2edp_print_status IRQ0 STATUS=%d\n", val);
	//printk("Alex_Fang:sn65dsi86_dsi2edp_print_status IRQ0 STATUS=%d\n", val);
	//msleep(10);
	//sn65dsi86_reg_read(dsi2edp, SN65DSI86_IRQ_STATUS1, &val);
	//printk("Alex_Fang:sn65dsi86_dsi2edp_print_status IRQ1 STATUS=%d\n", val);
	//sn65dsi86_reg_read(dsi2edp, SN65DSI86_IRQ_STATUS2, &val);
	//printk("Alex_Fang:sn65dsi86_dsi2edp_print_status IRQ2 STATUS=%d\n", val);
	//sn65dsi86_reg_read(dsi2edp, SN65DSI86_IRQ_STATUS3, &val);
	//printk("Alex_Fang:sn65dsi86_dsi2edp_print_status IRQ3 STATUS=%d\n", val);
	//sn65dsi86_reg_read(dsi2edp, SN65DSI86_IRQ_STATUS4, &val);
	//printk("Alex_Fang:sn65dsi86_dsi2edp_print_status IRQ4 STATUS=%d\n", val);
	//sn65dsi86_reg_read(dsi2edp, SN65DSI86_IRQ_STATUS5, &val);
	//printk("Alex_Fang:sn65dsi86_dsi2edp_print_status IRQ5 STATUS=%d\n", val);
	//sn65dsi86_reg_read(dsi2edp, SN65DSI86_IRQ_STATUS6, &val);
	//printk("Alex_Fang:sn65dsi86_dsi2edp_print_status IRQ6 STATUS=%d\n", val);
	//sn65dsi86_reg_read(dsi2edp, SN65DSI86_IRQ_STATUS7, &val);
	//printk("Alex_Fang:sn65dsi86_dsi2edp_print_status IRQ7 STATUS=%d\n", val);
	//sn65dsi86_reg_read(dsi2edp, SN65DSI86_IRQ_STATUS8, &val);
	//printk("Alex_Fang:sn65dsi86_dsi2edp_print_status IRQ8 STATUS=%d\n", val);
	//printk("=============================================================\n");
}

//Leo Guo modify following for android animation issue

int edp_init_first=0;
int sn65dsi86_enabled = 0;
int sn65dsi86_1st_part_enabled=0;
int edp_irq;
int edp_hpd_enable=0;
int edp_is_pluggable=0;
int edp_is_plugged=0;
extern void reset_mipi_for_edp(void);

#if 0
static irqreturn_t edp_2mipi_irq(int irq, void *data)
{
	unsigned int val=0;

	printk("edp_2mipi_irq occured!!!\n");

	if (!data) {
		pr_err("%s: invalid input\n", __func__);
		return IRQ_HANDLED;
	}

	sn65dsi86_reg_read(dsi2edp, 0x5c, &val);
	printk("sn65dsi86_dsi2edp_print_status 0x5c STATUS=%d\n", val);

	val=val|0x01;

	sn65dsi86_reg_write(dsi2edp, SN65DSI86_DP_HPD_EN, val);//disable HPD
	msleep(10);




	sn65dsi86_reg_write(dsi2edp, SN65DSI86_IRQ_EN, 0x00);//disable irq
	msleep(10);


	sn65dsi86_reg_write(dsi2edp, SN65DSI86_IRQ_EN_6, 0x00);//disable plug and remove irq
	msleep(10);




	sn65dsi86_reg_read(dsi2edp, SN65DSI86_IRQ_STATUS0, &val);
	printk("sn65dsi86_dsi2edp_print_status IRQ0 STATUS=%d\n", val);

	msleep(10);
	sn65dsi86_reg_read(dsi2edp, SN65DSI86_IRQ_STATUS1, &val);
	printk("sn65dsi86_dsi2edp_print_status IRQ1 STATUS=%d\n", val);
    sn65dsi86_reg_read(dsi2edp, SN65DSI86_IRQ_STATUS2, &val);
    printk("sn65dsi86_dsi2edp_print_status IRQ2 STATUS=%d\n", val);
	sn65dsi86_reg_read(dsi2edp, SN65DSI86_IRQ_STATUS3, &val);
	printk("sn65dsi86_dsi2edp_print_status IRQ3 STATUS=%d\n", val);
	sn65dsi86_reg_read(dsi2edp, SN65DSI86_IRQ_STATUS4, &val);
	printk("sn65dsi86_dsi2edp_print_status IRQ4 STATUS=%d\n", val);
	sn65dsi86_reg_read(dsi2edp, SN65DSI86_IRQ_STATUS5, &val);
	printk("sn65dsi86_dsi2edp_print_status IRQ5 STATUS=%d\n", val);
	sn65dsi86_reg_read(dsi2edp, SN65DSI86_IRQ_STATUS6, &val);
	printk("sn65dsi86_dsi2edp_print_status IRQ6 STATUS=%d\n", val);
	sn65dsi86_reg_read(dsi2edp, SN65DSI86_IRQ_STATUS7, &val);
	printk("sn65dsi86_dsi2edp_print_status IRQ7 STATUS=%d\n", val);
	sn65dsi86_reg_read(dsi2edp, SN65DSI86_IRQ_STATUS8, &val);
	printk("sn65dsi86_dsi2edp_print_status IRQ8 STATUS=%d\n", val);

	sn65dsi86_reg_read(dsi2edp, 0x5c, &val);
	printk("sn65dsi86_dsi2edp_print_status 0x5c STATUS=%d\n", val);


	sn65dsi86_reg_write(dsi2edp, SN65DSI86_DP_HPD_EN, 0x00);//enable HPD
	msleep(10);

	//sn65dsi86_reg_write(dsi2edp, SN65DSI86_IRQ_EN, 0x01);//enable irq
	msleep(10);
	//sn65dsi86_reg_write(dsi2edp, SN65DSI86_IRQ_EN_5, 0x01);//enable send int
	msleep(10);

	//sn65dsi86_reg_write(dsi2edp, SN65DSI86_IRQ_EN_6, 0x01);//enable plug and remove irq
	msleep(10);

	return IRQ_HANDLED;
}
#endif
void sn65dsi86_dsi2edp_enable_lately_2nd_part(void)
{
	unsigned short int buff;
	unsigned int val = 0;
	int retry = 0;
	PANEL_ID_TYPE qisda_panel_id=socinfo_get_panel_id();

	if (dsi2edp && dsi2edp->dsi2edp_enabled)
		return;

    /* STEP3: Single channelA 4 DSI lanes */
    printk("sn65dsi86_dsi2edp_enable_lately_2nd_part start\n");
	//gpio_set_value(backlight_en, 1);


    /* STEP9,10,11: eDP Pannel DisplayPort configuration(DPCD) */
	sn65dsi86_reg_write(dsi2edp, SN65DSI86_DP_SSC_CFG, 0x24); /* Pre0dB 2 lanes no SSC */
	msleep(10);

	sn65dsi86_reg_write(dsi2edp, SN65DSI86_DP_CFG, 0x80);/* DP_DATARATE HBR 2.70Gbps per lane */
	msleep(10);

    /* PLL ENABLE */
	sn65dsi86_reg_write(dsi2edp, SN65DSI86_PLL_EN, 0x01);
	msleep(10);

	 /* POST2 0dB */
	sn65dsi86_reg_write(dsi2edp, SN65DSI86_TRAINING_CFG, 0x00);
	msleep(10);

	 /* STEP12: check DP_PLL_LOCK */
	do{
		val = 0;
		sn65dsi86_reg_read(dsi2edp, SN65DSI86_PLL_REFCLK_CFG, &val);
		if((val & 0x80) != 0)
		{
			printk("sn65dsi86_dsi2edp_enable DP_PLL_LOCK=%x(LOCK)\n",val);
		}
		else{
			printk("sn65dsi86_dsi2edp_enable DP_PLL_LOCK=%x(UNLOCK)\n",val);
		}
		retry++;
		msleep(10);
		if(retry>50) break;
	}while((val & 0x80) == 0);

	/* STEP13: use Native AUX interface to enable eDP panel ASSR */
	buff = EDP_EN_ASSR;
	sn65dsi86_dsi2edp_write_dpcd(&buff,EDP_CONFIGURATION_SET_ADD,1);
	msleep(75); //LG 15.6 eDP panel data sheet power sequence T3 period

    /* STEP14: DisplayPort Link Semi-Auto train */
	sn65dsi86_reg_write(dsi2edp, SN65DSI86_ML_TX_MODE, 0x0a);
	msleep(20);

	val =0;
	sn65dsi86_reg_read(dsi2edp, SN65DSI86_ML_TX_MODE, &val);
	if((val & 0x0F) == 0x1)
       	printk("DisplayPort Link Semi-Auto train Success!!\n");
	else
       	printk("DisplayPort Link Semi-Auto train Fail(%x)!!\n",val);

	msleep(10);

    /* CHA_ACTIVE_LINE_LENGTH */
	sn65dsi86_reg_write(dsi2edp, SN65DSI86_VIDEO_CHA_LINE_LOW, 0x80);
	msleep(10);
	sn65dsi86_reg_write(dsi2edp, SN65DSI86_VIDEO_CHA_LINE_HIGH, 0x07);
	msleep(10);


	/* CHA_VERTICAL_DISPLAY_SIZE */
	sn65dsi86_reg_write(dsi2edp, SN65DSI86_CHA_VERT_DISP_SIZE_LOW, 0x38);
	msleep(10);
	sn65dsi86_reg_write(dsi2edp, SN65DSI86_CHA_VERT_DISP_SIZE_HIGH, 0x04);
	msleep(10);

	/* CHA_HSYNC_PULSE_WIDTH */
	sn65dsi86_reg_write(dsi2edp, SN65DSI86_CHA_HSYNC_PULSE_WIDTH_LOW, 0x20);
	msleep(10);

	sn65dsi86_reg_write(dsi2edp, SN65DSI86_CHA_HSYNC_PULSE_WIDTH_HIGH,0x80);
	msleep(10);

	/* CHA_VSYNC_PULSE_WIDTH */
	sn65dsi86_reg_write(dsi2edp, SN65DSI86_CHA_VSYNC_PULSE_WIDTH_LOW, 0x0E);
	msleep(10);
	sn65dsi86_reg_write(dsi2edp, SN65DSI86_CHA_VSYNC_PULSE_WIDTH_HIGH,0x80);
	msleep(10);

	/* CHA_HORIZONTAL_BACK_PORCH */
	sn65dsi86_reg_write(dsi2edp, SN65DSI86_CHA_HORIZONTAL_BACK_PORCH, 0x98);
	msleep(10);

	/* CHA_VERTICAL_BACK_PORCH */
	sn65dsi86_reg_write(dsi2edp, SN65DSI86_CHA_VERTICAL_BACK_PORCH, 0x13);
	msleep(10);


	/* CHA_HORIZONTAL_FRONT_PORCH */
	sn65dsi86_reg_write(dsi2edp, SN65DSI86_CHA_HORIZONTAL_FRONT_PORCH, 0x10);
	msleep(10);

	/* CHA_VERTICAL_FRONT_PORCH */
	sn65dsi86_reg_write(dsi2edp, SN65DSI86_CHA_VERTICAL_FRONT_PORCH, 0x03);
	msleep(10);

	/* DP-18BPP Enable */
	sn65dsi86_reg_write(dsi2edp, SN65DSI86_DP_18BPP_EN, 0x01);
	msleep(10);

	 /* COLOR BAR */
	#ifdef DEBUG_COLOR_BAR
	sn65dsi86_reg_write(dsi2edp, SN65DSI86_COLOR_BAR_CFG, 0x18|COLOR_BAR_TYPE);
	#else
	sn65dsi86_reg_write(dsi2edp, SN65DSI86_COLOR_BAR_CFG, 0x00);
	#endif
	msleep(10);

	/* enhanced framing and Vstream enable */
	if(qisda_panel_id==AUO_B133HAN027)
	{
		sn65dsi86_reg_read(dsi2edp, SN65DSI86_FRAMING_CFG, &val);
		msleep(10);
	   	val |= (0x01 <<3); // Enable VSTREAM for AUTHEN_METHOD
		sn65dsi86_reg_write(dsi2edp, SN65DSI86_FRAMING_CFG, val);
	}
	else
		sn65dsi86_reg_write(dsi2edp, SN65DSI86_FRAMING_CFG, 0x08);
	msleep(10);
	dsi2edp->dsi2edp_enabled = true;
	sn65dsi86_enabled = 1;

	printk("sn65dsi86_dsi2edp_enable_lately 2nd part-------------------\n");
}

void sn65dsi86_dsi2edp_enable_lately(void)
{
	static int first_time=1;
	unsigned int val=0;
	PANEL_ID_TYPE qisda_panel_id=socinfo_get_panel_id();
	if(dsi2edp==NULL)
		return;
	if(first_time)
	{
		printk("sn65dsi86_dsi2edp_enable_lately first start\n");
		first_time=0;
		edp_init_first=1;
		return;
	}

	if (dsi2edp && dsi2edp->dsi2edp_enabled)
		return;
	printk("sn65dsi86_dsi2edp_enable_lately start\n");

    /* STEP3: Single channelA 4 DSI lanes */
	//gpio_set_value(backlight_en, 1);
	//gpio_set_value(sn65dsi86_en, 1);
	//msleep(100);

	//msleep(100);
	gpio_set_value(sn65dsi86_rst, 1);
	msleep(100);

	if(gpio_is_valid(puck_redriver_en))
		gpio_set_value(puck_redriver_en,1);
    /* STEP3: Single channelA 4 DSI lanes */
	sn65dsi86_reg_write(dsi2edp, SN65DSI86_DSI_CFG1, 0x26);
	msleep(10);

	/* STEP4: set DPLL REFCLK 416MHz */
	sn65dsi86_reg_write(dsi2edp, SN65DSI86_PLL_REFCLK_CFG, 0x05);
	msleep(10);

	/* DSI CLK FREQ 415MHz */
	sn65dsi86_reg_write(dsi2edp, SN65DSI86_DSI_CHA_CLK_RANGE, 0x53);
	msleep(10);

    //disable VSTREAM
	if(qisda_panel_id==AUO_B133HAN027)
	{
		sn65dsi86_reg_read(dsi2edp, SN65DSI86_FRAMING_CFG, &val);
		msleep(10);
	   	val |= (0x01 <<2); // Enable VSTREAM for AUTHEN_METHOD
		sn65dsi86_reg_write(dsi2edp, SN65DSI86_FRAMING_CFG, val);
	}
	else
		sn65dsi86_reg_write(dsi2edp, SN65DSI86_FRAMING_CFG, 0x00);
	msleep(10);

    /* STEP8: HPD ENABLE */
	if(edp_is_pluggable)
	{
		sn65dsi86_reg_write(dsi2edp, SN65DSI86_DP_HPD_EN, 0x00);//enable HPD
		edp_hpd_enable=1;
	}
	else
		sn65dsi86_reg_write(dsi2edp, SN65DSI86_DP_HPD_EN, 0x01);//enable HPD
	msleep(10);

	//sn65dsi86_reg_write(dsi2edp, SN65DSI86_IRQ_EN, 0x01);//enable irq
	//msleep(10);
	//sn65dsi86_reg_write(dsi2edp, SN65DSI86_IRQ_EN_5, 0x01);//enable send int
	//msleep(10);

	//sn65dsi86_reg_write(dsi2edp, SN65DSI86_IRQ_EN_6, 0x01);//enable plug and remove irq
	//msleep(10);
    if((!edp_is_pluggable)||edp_is_plugged)
		sn65dsi86_dsi2edp_enable_lately_2nd_part();

	sn65dsi86_1st_part_enabled=1;
	printk("sn65dsi86_dsi2edp_enable_lately end\n");
}


void sn65dsi86_dsi2edp_disable(void)
{

	printk("sn65dsi86_dsi2edp_disable\n");
	if(dsi2edp==NULL)
		return;
	/* enhanced framing and Vstream disable */
	sn65dsi86_reg_write(dsi2edp, SN65DSI86_FRAMING_CFG, 0x04);
	usleep_range(10000, 12000);
	sn65dsi86_reg_write(dsi2edp, SN65DSI86_ML_TX_MODE, 0x00);
	usleep_range(10000, 12000);
	sn65dsi86_reg_write(dsi2edp, SN65DSI86_DP_SSC_CFG, 0x04);
	usleep_range(10000, 12000);

	/* PLL ENABLE disable */
	sn65dsi86_reg_write(dsi2edp, SN65DSI86_PLL_EN, 0x00);
	usleep_range(10000, 12000);

	msleep(100);
	dsi2edp->dsi2edp_enabled = false;
	sn65dsi86_enabled = 0;
	sn65dsi86_1st_part_enabled=0;
	if(splash_flag)
	{
		splash_flag=0;
	}
	else
	{
		gpio_set_value(panel_en, 0);
		msleep(50);
	}
	gpio_set_value(sn65dsi86_en, 0);

	gpio_set_value(sn65dsi86_rst, 0);
	msleep(500);//for lcd timing
}



static struct i2c_device_id sn65dsi86_id[] = {
	{ "sn65dsi86_dsi2edp", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, sn65dsi86_id);

static ssize_t device_name_rda_attr(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	int val=0;
	int i=0;

	sn65dsi86_reg_read(dsi2edp, SN65DSI86_IRQ_STATUS0, &val);
	printk("sn65dsi86_dsi2edp_print_status IRQ0 STATUS=%d\n", val);

	msleep(10);
	sn65dsi86_reg_read(dsi2edp, SN65DSI86_IRQ_STATUS1, &val);
	printk("sn65dsi86_dsi2edp_print_status IRQ1 STATUS=%d\n", val);
	sn65dsi86_reg_read(dsi2edp, SN65DSI86_IRQ_STATUS2, &val);
	printk("sn65dsi86_dsi2edp_print_status IRQ2 STATUS=%d\n", val);
	sn65dsi86_reg_read(dsi2edp, SN65DSI86_IRQ_STATUS3, &val);
	printk("sn65dsi86_dsi2edp_print_status IRQ3 STATUS=%d\n", val);
	sn65dsi86_reg_read(dsi2edp, SN65DSI86_IRQ_STATUS4, &val);
	printk("sn65dsi86_dsi2edp_print_status IRQ4 STATUS=%d\n", val);
	sn65dsi86_reg_read(dsi2edp, SN65DSI86_IRQ_STATUS5, &val);
	printk("sn65dsi86_dsi2edp_print_status IRQ5 STATUS=%d\n", val);
	sn65dsi86_reg_read(dsi2edp, SN65DSI86_IRQ_STATUS6, &val);
	printk("sn65dsi86_dsi2edp_print_status IRQ6 STATUS=%d\n", val);
	sn65dsi86_reg_read(dsi2edp, SN65DSI86_IRQ_STATUS7, &val);
	printk("sn65dsi86_dsi2edp_print_status IRQ7 STATUS=%d\n", val);
	sn65dsi86_reg_read(dsi2edp, SN65DSI86_IRQ_STATUS8, &val);
	printk("sn65dsi86_dsi2edp_print_status IRQ8 STATUS=%d\n", val);

	sn65dsi86_reg_read(dsi2edp, 0x5c, &val);
	printk("sn65dsi86_dsi2edp_print_status 0x5c STATUS=%d\n", val);

	for(i=0x09;i<0x3f;i++)
	{
	  sn65dsi86_reg_read(dsi2edp, i, &val);
	  printk("0x%x = 0x%x\n",i,val);
	}

	return 64;
}

static DEVICE_ATTR(device_name, S_IRUGO, device_name_rda_attr, NULL);


static struct attribute *edp_sysfs_attrs[] = {
	&dev_attr_device_name.attr,
	NULL,
};

static struct attribute_group edp_sysfs_attr_grp = {
	.attrs = edp_sysfs_attrs,
};

int sndsi86_helper_sysfs_init(struct device *dev)
{
	int rc = 0;

	if (!dev) {
		pr_err("%s: Invalid params\n", __func__);
		return -EINVAL;
	}

	rc = sysfs_create_group(&dev->kobj, &edp_sysfs_attr_grp);
	if (rc)
		pr_err("%s: sysfs group creation failed %d\n", __func__, rc);

	return rc;
}



static void edp_intr_work(struct work_struct *work)
{
	unsigned int val = 0;
	struct delayed_work *dw = to_delayed_work(work);
	if(edp_hpd_enable&&sn65dsi86_1st_part_enabled)
	{
		sn65dsi86_reg_read(dsi2edp, 0x5c, &val);
		printk("edp_intr_work 0x5c STATUS=%d\n", val);

		if((val&0x10)&&(edp_is_plugged==0))
		{
			edp_is_plugged=1;
			reset_mipi_for_edp();
			printk("cable insert!!!\n");
		}
		else if((edp_is_plugged==1)&&(val>>4==0))
		{
			edp_is_plugged=0;
			printk("cable removed!!!\n");

		}
		sn65dsi86_reg_write(dsi2edp, SN65DSI86_DP_HPD_EN, 0x00);//enable HPD
	}
    schedule_delayed_work(dw ,msecs_to_jiffies(1000));

return;
}

static int sn65dsi86_i2c_probe(struct i2c_client *client,
                               const struct i2c_device_id *id)
{
	int err = 0;
	int ret = 0;
	struct sn65dsi86_platform_data *pdata;
	struct device_node *np = client->dev.of_node;

	unsigned int val = 0;
	unsigned int value[8] = {0};

	printk("sn65dsi86_i2c_probe\n");
	sndsi86_helper_sysfs_init(&client->dev);
	pdata = devm_kzalloc(&client->dev,
		sizeof(struct sn65dsi86_platform_data), GFP_KERNEL);

	if (!pdata)
		return -ENOMEM;
	else
		sn65dsi86_dsi2edp_pdata = pdata;

	dsi2edp=sn65dsi86_dsi2edp_pdata;

	sn65dsi86_en = of_get_named_gpio(np, "mipi_edp_en-gpio", 0);
	sn65dsi86_rst = of_get_named_gpio(np, "mipi_edp_rst-gpio", 0);
	puck_redriver_en = of_get_named_gpio(np, "redriver_3v3_en-gpio", 0);
	panel_en = of_get_named_gpio(np, "panel_en-gpio", 0);
	backlight_en = of_get_named_gpio(np, "backlight_en-gpio", 0);
	edp_irq_gpio = of_get_named_gpio(np, "irq-gpio", 0);
	led_en_edp=of_get_named_gpio(np, "led_en-gpio", 0);
	edp_is_pluggable = of_property_read_bool(np,"qcom,pluggable");
	printk("mipi_edp_en = %d\n", sn65dsi86_en);
	printk("sn65dsi86_rst = %d\n", sn65dsi86_rst);
	printk("panel_en = %d\n", panel_en);
	printk("back en = %d\n", backlight_en);
	printk("edp_irq_gpio = %d\n", edp_irq_gpio);
	printk("puck_redriver_en = %d\n", puck_redriver_en);
	printk("led_en_edp = %d\n", led_en_edp);
	printk("edp_is_pluggable = %d\n", edp_is_pluggable);

	if (gpio_is_valid(sn65dsi86_en)) {
		ret = devm_gpio_request_one(&client->dev, sn65dsi86_en,
					    GPIOF_DIR_OUT, "mipi_edp_en");
		gpio_export(sn65dsi86_en,true);
		if (ret < 0) {
			printk("could not acquire enable gpio (err=%d)\n", ret);
		}
	}
	if (gpio_is_valid(panel_en)) {
		ret = devm_gpio_request_one(&client->dev, panel_en,GPIOF_OUT_INIT_HIGH, "panel_en");
		gpio_export(panel_en,true);
		if (ret < 0) {
			printk("could not acquire enable gpio (err=%d)\n", ret);
		}
	}
	msleep(10);

	if (gpio_is_valid(sn65dsi86_rst)) {
		ret = devm_gpio_request_one(&client->dev, sn65dsi86_rst,
					    GPIOF_OUT_INIT_HIGH, "sn65dsi86_rst");
		gpio_export(sn65dsi86_rst,true);
		if (ret < 0) {
			printk("could not acquire sn65dsi86_rst gpio (err=%d)\n", ret);
		}
	}

	if (gpio_is_valid(backlight_en)) {
		ret = devm_gpio_request_one(&client->dev, backlight_en,GPIOF_OUT_INIT_HIGH, "backlight_en");
		gpio_export(backlight_en,true);
		if (ret < 0) {
			printk("could not acquire enable gpio (err=%d)\n", ret);
		}
	}
	msleep(10);
	if (gpio_is_valid(edp_irq_gpio)) {
		ret = devm_gpio_request_one(&client->dev, edp_irq_gpio,
									GPIOF_DIR_IN, "edp_irq");
		if (ret < 0) {
				printk("could not acquire edp_irq_gpio  (err=%d)\n", ret);
		}
		gpio_export(edp_irq_gpio,true);
	}

	if (gpio_is_valid(puck_redriver_en)) {
		ret = devm_gpio_request_one(&client->dev, puck_redriver_en,
					    GPIOF_DIR_OUT, "puck_redriver_en");
		/*2017-05-17 JackWLu: temp solution: enable VCC3V3 first, externel hub need this 3v3 {*/
		gpio_set_value(puck_redriver_en,1);
		/*2017-05-17 JackWLu: temp solution: enable VCC3V3 first, externel hub need this 3v3 }*/
		gpio_export(puck_redriver_en,true);
		if (ret < 0) {
			printk("could not acquire puck_redriver_en gpio (err=%d)\n", ret);
		}
	}
	if (gpio_is_valid(led_en_edp)) {
		ret = devm_gpio_request_one(&client->dev, led_en_edp,GPIOF_OUT_INIT_HIGH, "led_en_edp");
		gpio_export(led_en_edp,true);
		if (ret < 0) {
			printk("could not acquire led_en_edp gpio (err=%d)\n", ret);
		}
	}

	sn65dsi86_i2c_client = client;
	printk("sn65dsi86_i2c_client addr = %x\n", sn65dsi86_i2c_client->addr);
	err = sn65dsi86_dsi2edp_init();
	if(err != 0)
	{
		pr_err("sn65dsi86_dsi2edp_init error=%d\n",err);
		return err;
	}
#if 0
	edp_irq = gpio_to_irq(edp_irq_gpio);

	ret = request_threaded_irq(edp_irq, NULL, edp_2mipi_irq,
		IRQF_TRIGGER_RISING | IRQF_ONESHOT, "MIPI2EDP", dsi2edp);
	if (ret) {
		pr_err("%s: Failed to enable edp interrupt\n",
			__func__);
	}
    enable_irq(edp_irq);
	printk("irq requested and edp irq is %d\n",edp_irq);
#endif

	sn65dsi86_reg_read(sn65dsi86_dsi2edp_pdata, 0x00, &value[0]);
	sn65dsi86_reg_read(sn65dsi86_dsi2edp_pdata, 0x01, &value[1]);
	sn65dsi86_reg_read(sn65dsi86_dsi2edp_pdata, 0x02, &value[2]);
	sn65dsi86_reg_read(sn65dsi86_dsi2edp_pdata, 0x03, &value[3]);
	sn65dsi86_reg_read(sn65dsi86_dsi2edp_pdata, 0x04, &value[4]);
	sn65dsi86_reg_read(sn65dsi86_dsi2edp_pdata, 0x05, &value[5]);
	sn65dsi86_reg_read(sn65dsi86_dsi2edp_pdata, 0x06, &value[6]);
	sn65dsi86_reg_read(sn65dsi86_dsi2edp_pdata, 0x07, &value[7]);
	sn65dsi86_reg_read(sn65dsi86_dsi2edp_pdata, 0x08, &val);
//Leo Guo add for ftd to read ic id
	edp_device_id[0]=value[7];
	edp_device_id[1]=value[6];
	edp_device_id[2]=value[5];
	edp_device_id[3]=value[4];
	edp_device_id[4]=value[2];
	edp_device_id[5]=0;
	edp_ic_version=val;
	printk("edp device id is %s,version is %d\n",edp_device_id,edp_ic_version);

	if(edp_is_pluggable)
	{
	    if(gpio_is_valid(puck_redriver_en))
			gpio_set_value(puck_redriver_en,1);
		INIT_DELAYED_WORK(&dsi2edp->intr_work, edp_intr_work);
		schedule_delayed_work(&dsi2edp->intr_work,msecs_to_jiffies(10000));
	}

	return err;
}

static int sn65dsi86_i2c_remove(struct i2c_client *client)
{
	sn65dsi86_dsi2edp_disable();
	sn65dsi86_i2c_client = NULL;
	sn65dsi86_dsi2edp_destroy();
	return 0;
}

static void sn65dsi86_i2c_shutdown(struct i2c_client *client)
{
	if(gpio_is_valid(puck_redriver_en))
		gpio_set_value(puck_redriver_en,0);
	return;
}

static struct i2c_driver sn65dsi86_i2c_drv = {
	.driver = {
		.name = "sn65dsi86_dsi2edp",
		.owner = THIS_MODULE,
	},
		.probe = sn65dsi86_i2c_probe,
		.remove = sn65dsi86_i2c_remove,
		.shutdown = sn65dsi86_i2c_shutdown,
		.id_table = sn65dsi86_id,
};

static int __init sn65dsi86_i2c_client_init(void)
{
	int err = 0;

	err = i2c_add_driver(&sn65dsi86_i2c_drv);
	if (err)
		pr_err("sn65dsi86_dsi2edp: Failed to add i2c client driver\n");

	return err;
}

static void __exit sn65dsi86_i2c_client_exit(void)
{
	i2c_del_driver(&sn65dsi86_i2c_drv);
}

subsys_initcall(sn65dsi86_i2c_client_init);
module_exit(sn65dsi86_i2c_client_exit);

MODULE_AUTHOR("Bibek Basu <bbasu@nvidia.com>");
MODULE_DESCRIPTION(" TI SN65DSI86 dsi bridge to edp");
MODULE_LICENSE("GPL");



