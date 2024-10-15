#ifndef __DRIVERS_VIDEO_SN65DSI85_DSI2LVDS_H
#define __DRIVERS_VIDEO_SN65DSI85_DSI2LVDS_H

struct sn65dsi85_platform_data {
 	struct i2c_client *client_i2c;
 	struct regmap *regmap;
 	bool dsi2lvds_enabled;
//Leo Guo add for lvds IC error issue
	struct workqueue_struct *workq;
	struct delayed_work sn65dsi85__work_id;
 	struct mutex lock;
};

#define  SN65DSI85_DEVICE_ID			0x00
#define  SN65DSI85_DEVICE_REV			0x08
#define  SN65DSI85_SOFT_RESET			0X09//
#define  SN65DSI85_PLL_REFCLK_CFG		0x0A//
#define  SN65DSI85_LVDS_CLK_CFG		0x0B//
#define  SN65DSI85_PLL_EN			0x0D//
#define  SN65DSI85_DSI_CFG1			0x10//
#define  SN65DSI85_DSI_CFG2			0x11//
#define  SN65DSI85_DSI_CHA_CLK_RANGE		0x12//
#define  SN65DSI85_DSI_CHB_CLK_RANGE		0x13//
#define  SN65DSI85_LVDS_CFG1          0x18
#define  SN65DSI85_LVDS_CFG2          0x19
#define  SN65DSI85_LVDS_CFG3          0x1A
#define  SN65DSI85_LVDS_CFG4          0x1B
#define  SN65DSI85_VIDEO_CHA_LINE_LOW		0x20//
#define  SN65DSI85_VIDEO_CHA_LINE_HIGH		0x21//
#define  SN65DSI85_VIDEO_CHB_LINE_LOW		0x22//
#define  SN65DSI85_VIDEO_CHB_LINE_HIGH		0x23//
#define  SN65DSI85_CHA_VERT_DISP_SIZE_LOW	0x24//
#define  SN65DSI85_CHA_VERT_DISP_SIZE_HIGH	0x25//
#define  SN65DSI85_CHB_VERT_DISP_SIZE_LOW	0x26
#define  SN65DSI85_CHB_VERT_DISP_SIZE_HIGH	0x27
#define  SN65DSI85_CHA_SYNC_DELAY_LOW	0x28
#define  SN65DSI85_CHA_SYNC_DELAY_HIGH	0x29
#define  SN65DSI85_CHB_SYNC_DELAY_LOW	0x2A
#define  SN65DSI85_CHB_SYNC_DELAY_HIGH	0x2B
#define  SN65DSI85_CHA_HSYNC_PULSE_WIDTH_LOW	0x2C//
#define  SN65DSI85_CHA_HSYNC_PULSE_WIDTH_HIGH	0x2D//
#define  SN65DSI85_CHB_HSYNC_PULSE_WIDTH_LOW	0x2E
#define  SN65DSI85_CHB_HSYNC_PULSE_WIDTH_HIGH	0x2F
#define  SN65DSI85_CHA_VSYNC_PULSE_WIDTH_LOW	0x30//
#define  SN65DSI85_CHA_VSYNC_PULSE_WIDTH_HIGH	0x31//
#define  SN65DSI85_CHB_VSYNC_PULSE_WIDTH_LOW	0x32//
#define  SN65DSI85_CHB_VSYNC_PULSE_WIDTH_HIGH	0x33//
#define  SN65DSI85_CHA_HORIZONTAL_BACK_PORCH	0x34//
#define  SN65DSI85_CHB_HORIZONTAL_BACK_PORCH	0x35
#define  SN65DSI85_CHA_VERTICAL_BACK_PORCH	0x36//
#define  SN65DSI85_CHB_VERTICAL_BACK_PORCH	0x37
#define  SN65DSI85_CHA_HORIZONTAL_FRONT_PORCH	0x38//
#define  SN65DSI85_CHB_HORIZONTAL_FRONT_PORCH	0x39
#define  SN65DSI85_CHA_VERTICAL_FRONT_PORCH	0x3a//
#define  SN65DSI85_CHB_VERTICAL_FRONT_PORCH	0x3b
#define  SN65DSI85_COLOR_BAR_CFG		0x3c//
#define  SN65DSI85_RIGHT_CROP             0x3d//
#define  SN65DSI85_LEFT_CROP              0x3e//


//ePD definition---------------------------------
#define ASSR_SUPPORT                           (1<<0)
#define ENHANCE_FRAMING                    (1<<1)
#define MAX_LINK_RATE_1P6Gbps          1
#define MAX_LINK_RATE_2P7Gbps          2
#define EDP_CONFIGURATION_SET_ADD        0x010A
#define EDP_EN_ASSR                            (1<<0)

#endif
