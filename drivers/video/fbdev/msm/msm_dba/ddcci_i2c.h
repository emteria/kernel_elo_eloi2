
#include <linux/version.h>

#ifndef _LINUX_DDCCI_I2C_H
#define _LINUX_DDCCI_I2C_H

typedef u32 uint32;
typedef u8 uint8;

#define DDCCI_I2C_NAME "ddcci_i2c"
#define DDCCI_SLAVE_ADDR					0xe0








/* SiS i2c error code */
#define DDCCI_ERR						-1
#define DDCCI_ERR_ACCESS_USER_MEM		-11 /* Access user memory fail */
#define DDCCI_ERR_ALLOCATE_KERNEL_MEM	-12 /* Allocate memory fail */
#define DDCCI_ERR_CLIENT				-13 /* Client not created */
#define DDCCI_ERR_COPY_FROM_USER		-14 /* Copy data from user fail */
#define DDCCI_ERR_COPY_FROM_KERNEL	-19 /* Copy data from kernel fail */
#define DDCCI_ERR_TRANSMIT_I2C		-21 /* Transmit error in I2C */


// ddc/ci command
#define DDC_CI_CMD_GET_VCP              0x01
#define DDC_CI_CMD_SET_VCP              0x03
#define DDC_CI_CMD_TIMING_REQ           0x07
#define DDC_CI_CMD_VCP_RESET            0x09
#define DDC_CI_CMD_SAVE_SETTING         0x0C
#define DDC_CI_CMD_CAP_REQ              0xF3

// ioctl command ID
#define DDC_CI_CID_GET_VCP              _IOWR(0xAA, DDC_CI_CMD_GET_VCP, unsigned char[40])
#define DDC_CI_CID_SET_VCP              _IOW(0xAA, DDC_CI_CMD_SET_VCP, unsigned char[40])
#define DDC_CI_CID_TIMING_REQ           _IOWR(0xAA, DDC_CI_CMD_TIMING_REQ, unsigned char[40])
#define DDC_CI_CID_VCP_RESET            _IOW(0xAA, DDC_CI_CMD_VCP_RESET, long)
#define DDC_CI_CID_SAVE_SETTING         _IOW(0xAA, DDC_CI_CMD_SAVE_SETTING, long)
#define DDC_CI_CID_CAP_REQ              _IOWR(0xAA, DDC_CI_CMD_CAP_REQ, unsigned char[40])



extern void ddcci_i2c_switch(u8 path);
enum ddcci_i2c_switch_setting {
    I2C_SWITCH_TO_EDID,
    I2C_SWITCH_TO_DDC
};

#endif /* _LINUX_SIS_I2C_H */


struct ddcci_data {
	int (*power)(int on);
	struct i2c_client *client;
};


