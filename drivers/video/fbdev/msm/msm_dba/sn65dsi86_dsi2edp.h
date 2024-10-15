 #ifndef __DRIVERS_VIDEO_SN65DSI86_DSI2EDP_H
 #define __DRIVERS_VIDEO_SN65DSI86_DSI2EDP_H

 struct sn65dsi86_platform_data {
 	struct i2c_client *client_i2c;
 	struct regmap *regmap;
 	bool dsi2edp_enabled;
	struct delayed_work intr_work;
 	struct mutex lock;
 };

 #define  SN65DSI86_DEVICE_ID			0x00
 #define  SN65DSI86_DEVICE_REV			0x08
 #define  SN65DSI86_SOFT_RESET			0X09
 #define  SN65DSI86_PLL_REFCLK_CFG		0x0A
 #define  SN65DSI86_PLL_EN			0x0D
 #define  SN65DSI86_DSI_CFG1			0x10
 #define  SN65DSI86_DSI_CFG2			0x11
 #define  SN65DSI86_DSI_CHA_CLK_RANGE		0x12
 #define  SN65DSI86_DSI_CHB_CLK_RANGE		0x13
 #define  SN65DSI86_VIDEO_CHA_LINE_LOW		0x20
 #define  SN65DSI86_VIDEO_CHA_LINE_HIGH		0x21
 #define  SN65DSI86_VIDEO_CHB_LINE_LOW		0x22
 #define  SN65DSI86_VIDEO_CHB_LINE_HIGH		0x23
 #define  SN65DSI86_CHA_VERT_DISP_SIZE_LOW	0x24
 #define  SN65DSI86_CHA_VERT_DISP_SIZE_HIGH	0x25
 #define  SN65DSI86_CHA_HSYNC_PULSE_WIDTH_LOW	0x2C
 #define  SN65DSI86_CHA_HSYNC_PULSE_WIDTH_HIGH	0x2D
 #define  SN65DSI86_CHA_VSYNC_PULSE_WIDTH_LOW	0x30
 #define  SN65DSI86_CHA_VSYNC_PULSE_WIDTH_HIGH	0x31
 #define  SN65DSI86_CHA_HORIZONTAL_BACK_PORCH	0x34
 #define  SN65DSI86_CHA_VERTICAL_BACK_PORCH	0x36
 #define  SN65DSI86_CHA_HORIZONTAL_FRONT_PORCH	0x38
 #define  SN65DSI86_CHA_VERTICAL_FRONT_PORCH	0x3a
 #define  SN65DSI86_COLOR_BAR_CFG		0x3c
 #define  SN65DSI86_RIGHT_CROP                0x3d
 #define  SN65DSI86_LEFT_CROP                   0x3e
 #define  SN65DSI86_ADEN                            0x3f
 #define  SN65DSI86_FRAMING_CFG			0x5a
 #define  SN65DSI86_DP_18BPP_EN			0x5b
 #define  SN65DSI86_DP_HPD_EN                        0x5c
 #define  SN65DSI86_GPIO_CTRL_CFG		0x5f
 #define  SN65DSI86_AUX_WDATA0             0x64
 #define  SN65DSI86_AUX_WDATA1             0x65
 #define  SN65DSI86_AUX_WDATA2             0x66
 #define  SN65DSI86_AUX_WDATA3             0x67
 #define  SN65DSI86_AUX_WDATA4             0x68
 #define  SN65DSI86_AUX_WDATA5             0x69
 #define  SN65DSI86_AUX_WDATA6             0x6a
 #define  SN65DSI86_AUX_WDATA7             0x6b
 #define  SN65DSI86_AUX_WDATA8             0x6c
 #define  SN65DSI86_AUX_WDATA9             0x6d
 #define  SN65DSI86_AUX_WDATA10           0x6e
 #define  SN65DSI86_AUX_WDATA11           0x6f
 #define  SN65DSI86_AUX_WDATA12           0x70
 #define  SN65DSI86_AUX_WDATA13           0x71
 #define  SN65DSI86_AUX_WDATA14           0x72
 #define  SN65DSI86_AUX_WDATA15           0x73
 #define  SN65DSI86_AUX_ADDR_19_16      0x74
 #define  SN65DSI86_AUX_ADDR_15_8        0x75
 #define  SN65DSI86_AUX_ADDR_7_0          0x76
 #define  SN65DSI86_AUX_LENGTH              0x77
 #define  SN65DSI86_AUX_CMD_SEND         0x78
 #define  SN65DSI86_AUX_RDATA0             0x79
 #define  SN65DSI86_AUX_RDATA1             0x7A
 #define  SN65DSI86_AUX_RDATA2             0x7B
 #define  SN65DSI86_AUX_RDATA3             0x7C
 #define  SN65DSI86_AUX_RDATA4             0x7D
 #define  SN65DSI86_AUX_RDATA5             0x7E
 #define  SN65DSI86_AUX_RDATA6             0x7F
 #define  SN65DSI86_AUX_RDATA7             0x80
 #define  SN65DSI86_AUX_RDATA8             0x81
 #define  SN65DSI86_AUX_RDATA9             0x82
 #define  SN65DSI86_AUX_RDATA10            0x83
 #define  SN65DSI86_AUX_RDATA11            0x84
 #define  SN65DSI86_AUX_RDATA12            0x85
 #define  SN65DSI86_AUX_RDATA13             0x86
 #define  SN65DSI86_AUX_RDATA14             0x87
 #define  SN65DSI86_AUX_RDATA15             0x88
 #define  SN65DSI86_DP_SSC_CFG			0x93
 #define  SN65DSI86_DP_CFG			0x94
 #define  SN65DSI86_TRAINING_CFG		0x95
 #define  SN65DSI86_ML_TX_MODE			0x96
 #define  SN65DSI86_IRQ_EN                        0xE0
 #define  SN65DSI86_IRQ_EN_5                        0xE5
 #define SN65DSI86_IRQ_EN_6                        0xE6
 #define  SN65DSI86_IRQ_STATUS0              0xF0
 #define  SN65DSI86_IRQ_STATUS1              0xF1
 #define  SN65DSI86_IRQ_STATUS2              0xF2
 #define  SN65DSI86_IRQ_STATUS3              0xF3
 #define  SN65DSI86_IRQ_STATUS4              0xF4
 #define  SN65DSI86_IRQ_STATUS5              0xF5
 #define  SN65DSI86_IRQ_STATUS6              0xF6
 #define  SN65DSI86_IRQ_STATUS7              0xF7
 #define  SN65DSI86_IRQ_STATUS8              0xF8

//ePD definition---------------------------------
 #define ASSR_SUPPORT                           (1<<0)
 #define ENHANCE_FRAMING                    (1<<1)
 #define MAX_LINK_RATE_1P6Gbps          1
 #define MAX_LINK_RATE_2P7Gbps          2
 #define EDP_CONFIGURATION_SET_ADD        0x010A
 #define EDP_EN_ASSR                            (1<<0)

 #endif
