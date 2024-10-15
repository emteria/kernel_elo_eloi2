/*!
  @file
  qotp.h
*/

#ifndef _QOTP_H_
#define _QOTP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "oem_partition_name.h"
// access QOTP_LINK_STR or PART_OTP

//////////////////// QOTP tail type define ////////////////////
typedef struct
{
	unsigned int  magic; //QOTP version, now magic 0
	unsigned int  checksum;
	unsigned int  flag; //item flag: QOTP_FLAG_SEED * (index+1)
} qcfg_items_tail_type;

typedef struct
{
	qcfg_items_tail_type base;
	unsigned int  unlock_magic;
} qcfg_items_tail_type_lock;


//////////////////// Area related defines ////////////////////
#define MAX_OTP_NUM                 1
#define OTP_SIZE                    0x00400000  // 4MB

#define QOTP_ITEM_SIZE              1024
#define QOTP_NUMBER_XOR             0xFA


#define QOTP_PER_MAGIC_NUM_OFFSET   0x0
#define QOTP_PER_MAGIC_NUM_LENGTH   4

#define QOTP_MAGIC_NUM_0            0x16024958
#define QOTP_MAGIC_NUM_1            0x648009F9
#define QOTP_MAGIC_NUM_2            0xAB419598
#define QOTP_MAGIC_NUM_3            0xEC479A90

#define QOTP_MAGIC_NUM \
	static const unsigned int qotp_magic_num[4] = {QOTP_MAGIC_NUM_0, QOTP_MAGIC_NUM_1, QOTP_MAGIC_NUM_2, QOTP_MAGIC_NUM_3}


#define QOTP_FLAG_SEED              0x24958
#define QOTP_TAIL_MAGIC             0xFB979B57
#define QOTP_LOCK_MAGIC             0xBC008C60
#define QOTP_UNLOCK_MAGIC           0xC7875A30


//////////////////// Item related defines ////////////////////

#define QOTP_SN_STRING              "SN"
#define QOTP_SN_OFFSET              QOTP_ITEM_SIZE   //reserve one item
#define QOTP_SN_LENGTH              10
#define QOTP_SN_SMEM_LENGTH         10

#define QOTP_SN1_STRING             "SN1"
#define QOTP_SN1_OFFSET             QOTP_ITEM_SIZE*2
#define QOTP_SN1_LENGTH             13
#define QOTP_SN1_SMEM_LENGTH        13

#define QOTP_SN2_STRING             "SN2"
#define QOTP_SN2_OFFSET             QOTP_ITEM_SIZE*3
#define QOTP_SN2_LENGTH             22
#define QOTP_SN2_SMEM_LENGTH        22

#define QOTP_ETHADDR_STRING         "ETHADDR"
#define QOTP_ETHADDR_OFFSET         QOTP_ITEM_SIZE*4
#define QOTP_ETHADDR_LENGTH         17
#define QOTP_ETHADDR_SMEM_LENGTH    17

#define QOTP_BTADDR_STRING          "BTADDR"
#define QOTP_BTADDR_OFFSET          QOTP_ITEM_SIZE*5
#define QOTP_BTADDR_LENGTH          17
#define QOTP_BTADDR_SMEM_LENGTH     17

#define QOTP_WIFIADDR_STRING        "WIFIADDR"
#define QOTP_WIFIADDR_OFFSET        QOTP_ITEM_SIZE*6
#define QOTP_WIFIADDR_LENGTH        17
#define QOTP_WIFIADDR_SMEM_LENGTH   17

#define QOTP_WIFIMODCONFIG_STRING        "WIFIMODCONFIG"
#define QOTP_WIFIMODCONFIG_OFFSET        QOTP_ITEM_SIZE*7
#define QOTP_WIFIMODCONFIG_LENGTH        7
#define QOTP_WIFIMODCONFIG_SMEM_LENGTH   7

#define QOTP_CDPWIDTH_STRING        "CDPWIDTH"
#define QOTP_CDPWIDTH_OFFSET        QOTP_ITEM_SIZE*8
#define QOTP_CDPWIDTH_LENGTH        5
#define QOTP_CDPWIDTH_SMEM_LENGTH   5

#define QOTP_INFO_ITEM(x)    {QOTP_##x##_STRING, QOTP_##x##_OFFSET, QOTP_##x##_LENGTH, QOTP_##x##_SMEM_LENGTH}

/*
!!! NOTE: The index order in QOTP_ITEMS and QOTP_INFO_TABLE must match !!!
      For example ,FA SN is the first item in QOTP_ITEMS. ( ignore ITEMS_START)
      Then ,it also should be the first item in QOTP_INFO_TABLE
*/
enum QOTP_ITEMS
{
	ITEMS_START         = 0,
	ITEMS_SN            = ITEMS_START,    //= 0, the first item
	ITEMS_SN1 ,                           //= 1,
	ITEMS_SN2 ,                           //= 2,
	ITEMS_ETH_ADDR ,                      //= 3,
	ITEMS_BT_ADDR ,                       //= 4,
	ITEMS_WIFI_ADDR ,                     //= 5,
	ITEMS_WIFIMODCONFIG,                  //= 6,
	ITEMS_CDPWIDTH,                       //= 7,
	//if you want to add an item ,please put it before ITEMS_MAX
	ITEMS_MAX ,
};

#define QOTP_INFO_TABLE \
	static const struct { \
		char *name; \
		unsigned long offset; \
		unsigned int length; \
		unsigned int smem_length; \
	} qotp_info_table[] = { \
		QOTP_INFO_ITEM(SN), \
		QOTP_INFO_ITEM(SN1), \
		QOTP_INFO_ITEM(SN2), \
		QOTP_INFO_ITEM(ETHADDR), \
		QOTP_INFO_ITEM(BTADDR), \
		QOTP_INFO_ITEM(WIFIADDR), \
		QOTP_INFO_ITEM(WIFIMODCONFIG), \
		QOTP_INFO_ITEM(CDPWIDTH), \
	}

//////////////////// Error Code ////////////////////
#define QOTP_SUCCESS                       0
#define QOTP_ERR_UNSUPPORT_ITEM            -21
#define QOTP_ERR_INVALID_PARAM             -22
#define QOTP_ERR_WRONG_DATA_SIZE           -23
#define QOTP_ERR_WRONG_DATA_FORMAT         -24
#define QOTP_ERR_OPEN_PARTITION_FAIL       -25
#define QOTP_ERR_LSEEK_FAIL                -26
#define QOTP_ERR_WRITE_DATA_FAIL           -27
#define QOTP_ERR_WRITE_CHECKSUM_FAIL       -28
#define QOTP_ERR_WRITE_VERIFY_FAIL         -29
#define QOTP_ERR_READ_DATA_FAIL            -30
#define QOTP_ERR_READ_CHECKSUM_FAIL        -31
#define QOTP_ERR_VERIFY_CHECKSUM_FAIL      -32
#define QOTP_ERR_FLASH_NOT_FOUND           -33
#define QOTP_ERR_FLASH_INIT_FAIL           -34
#define QOTP_ERR_MALLOC_FAIL               -35
#define QOTP_ERR_FLASH_LOCKED              -36
#define QOTP_ERR_FLASH_NOT_LOCKED          -37
#define QOTP_ERR_WIRTE_UNLOCK_FAIL         -38

#ifdef __cplusplus
}
#endif
#endif /* _QOTP_H_*/
