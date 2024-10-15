/*!
  @file
  oem_smem_struct.h
*/

#ifndef _OEM_SMEM_STRUCT_H_
#define _OEM_SMEM_STRUCT_H_

#ifdef __cplusplus
extern "C" {
#endif
/*===========================================================================

                           INCLUDE FILES

===========================================================================*/
#include "qotp.h"

/*===========================================================================

    data struct declaration

===========================================================================*/

/* This enum indicates OEM Project/HW/PANEL id */
typedef enum
{
	PROJECT_PAYPOINT_REFRESH	= 0x01,
	PROJECT_PAYPOINT2			= 0x02,
	PROJECT_PUCK				= 0x03,
	PROJECT_AAIO2_STD_101		= 0x04,
	PROJECT_AAIO2_STD_156		= 0x05,
	PROJECT_AAIO2_STD_215		= 0x06,
	PROJECT_AAIO2_VALUE_101		= 0x07,
	PROJECT_AAIO2_VALUE_156		= 0x08,

	UNKNOWN_PR_ID				= 0x7FFFFFFF
} PROJECT_ID_TYPE;

typedef enum
{
	BOARD_EVT0				= 0x01,
	BOARD_EVT2				= 0x02,
	BOARD_DVT				= 0x03,
	BOARD_PVT				= 0x04,
	BOART_PVT_1				= 0x05,
	BOART_PVT_2				= 0x06,
	BOART_PVT_3				= 0x07,
	BOART_PVT_4				= 0x08,

	UNKNOWN_BOARD_ID		= 0x7FFFFFFF
} BOARD_ID_TYPE;

typedef enum
{
	AUO_B133HAN027			= 0x10,
	TM101JDHP01_00			= 0x20,
	BOE_NV156FHM_N43		= 0x30,
	HR215WU1_120			= 0x40,
	BOE_NV133FHM_N63		= 0x50,
	BOE_NT156FHM_N41		= 0x60,
	LP156WF6_SPB1			= 0x70,
	PUCK_NO_PANEL			= 0x80,

	UNKNOWN_PANEL_ID		= 0x7FFFFFFF
} PANEL_ID_TYPE;

typedef struct
{
	PROJECT_ID_TYPE		project_id;
	BOARD_ID_TYPE		board_id;
	PANEL_ID_TYPE		panel_id;
}hw_id_type;

/* Project ID/Panle ID/ HW ID table*/
/*~~~~~~~~PROJECT ~~~~~~~~~PP_ID~~Panel_ID~~~~~~~~~Panel~~~~~~~FTD board ID~~~HW ID~~persist.sys.oem.model.id
PROJECT_PAYPOINT_REFRESH----01------10---------AUO_B133HAN027-----0x110x00---0x110x00--------110
PROJECT_PAYPOINT_REFRESH----01------50---------BOE_NV133FHM_N63---0x150x00---0x150x00--------150
*PROJECT_PAYPOINT2.0--------01------30---------BOE_NV156FHM_N43---0x130100---0x130100--------130
*PROJECT_PAYPOINT2.0--------01------70---------LP156WF6_SPB1------0x170100---0x170100--------170
PROJECT_PAYPOINT2.0---------02------30---------BOE_NV156FHM_N43---0x230x00---0x230x00--------230
PROJECT_PAYPOINT2.0---------02------70---------LP156WF6_SPB1------0x270x00---0x270x00--------270
PROJECT_PUCK----------------03------80---------PUCK_NO_PANEL------0x380x00---0x380x00--------380
PROJECT_AAIO2_STD_101-------04------20---------TM101JDHP01_00-----0x420x00---0x420x00--------420
PROJECT_AAIO2_STD_156-------05------30---------BOE_NV156FHM_N43---0x530x00---0x530x00--------530
PROJECT_AAIO2_STD_215-------06------40---------HR215WU1-120-------0x640x00---0x640x00--------640
PROJECT_AAIO2_VALUE_101-----07------20---------TM101JDHP01_00-----0x720x00---0x720x00--------720
PROJECT_AAIO2_VALUE_156-----08------60---------BOE_NT156FHM_N41---0x860x00---0x860x00--------860
*/


/* structure definition for
  SMEM_ID_VENDOR0,
  SMEM_ID_VENDOR1,
  SMEM_ID_VENDOR2,

  Notice!! SMEM space is limited.
  Please remove structure definition not use anymore!!
*/
/** DDR types. */

/*
typedef enum
{
  DDR_TYPE_LPDDR1 = 0,
  DDR_TYPE_LPDDR2 = 2,
  DDR_TYPE_PCDDR2 = 3,
  DDR_TYPE_PCDDR3 = 4,

  DDR_TYPE_LPDDR3 = 5,
  DDR_TYPE_LPDDR4 = 6,

  DDR_TYPE_RESERVED = 7,
  DDR_TYPE_UNUSED = 0x7FFFFFFF
} DDR_TYPE;
*/

/** DDR manufacturers. */
/*
typedef enum
{
  RESERVED_0,
  SAMSUNG,
  QIMONDA,
  ELPIDA,
  ETRON,
  NANYA,
  HYNIX,
  MOSEL,
  WINBOND,
  ESMT,
  RESERVED_1,
  SPANSION,
  SST,
  ZMOS,
  INTEL,
  NUMONYX = 254,
  MICRON = 255,
  DDR_MANUFACTURES_MAX = 0x7FFFFFFF
} DDR_MANUFACTURES;
*/
/* Vendor ID 0 : data written by Boot loader*/
typedef struct
{
	hw_id_type			hw_id;
	unsigned int        		ddr_size;
	unsigned int			ddr_type;
	unsigned int			ddr_vendor;
} smem_vendor_id0_bl_data;

/* Vendor ID 1 : data written by APPS */
typedef struct
{
	unsigned int  sn_valid;
	unsigned char sn[QOTP_SN_LENGTH + 1];
	unsigned int  sn1_valid;
	unsigned char sn1[QOTP_SN1_LENGTH + 1];
	unsigned int  ethaddr_valid;
	unsigned char ethaddr[QOTP_ETHADDR_LENGTH + 1];
	unsigned int  btaddr_valid;
	unsigned char btaddr[QOTP_BTADDR_LENGTH + 1];
	unsigned int  wifiaddr_valid;
	unsigned char wifiaddr[QOTP_WIFIADDR_LENGTH + 1];
} smem_vendor_id1_apps_data;

/* Vendor ID 2 : data written by AMSS */
typedef struct
{
	unsigned int		dummy;
 }smem_vendor_id2_amss_data;

#ifdef __cplusplus
}
#endif
#endif /* _OEM_SMEM_STRUCT_H_ */

