/*
 *********************************************************************************
 *     Copyright (c) 2005 ASIX Electronic Corporation      All rights reserved.
 *
 *     This is unpublished proprietary source code of ASIX Electronic Corporation
 *
 *     The copyright notice above does not evidence any actual or intended
 *     publication of such source code.
 *********************************************************************************
 */
 
#ifndef __COMMAND_H__
#define __COMMAND_H__

/* NAMING CONSTANT DECLARATIONS */
#define CYUSB3610_SIGNATURE	"CYUSB3610"
#define CYUSB3610_DRV_NAME	"CYUSB3610"

/* ioctl Command Definition */
#define CY_PRIVATE		SIOCDEVPRIVATE

/* private Command Definition */
#define CY_SIGNATURE			0
#define CY_IEEETEST			1

#define IEEE_1000M1			0
#define IEEE_1000M4			1
#define IEEE_100CA			2
#define IEEE_100CB			3
#define IEEE_10R			4
#define IEEE_10TER			5
#define IEEE_10FF			6
#define IEEE_10TEFF			7
#define IEEE_10F0			8
#define IEEE_10TEF0			9
#define IEEE_TYPEMASK			0x7f
#define IEEE_DISABLE			0x80

typedef struct _CY_IOCTL_COMMAND {
	unsigned short	ioctl_cmd;
	unsigned char	sig[16];
	unsigned char type;
	unsigned short *buf;
	unsigned short size;
}CY_IOCTL_COMMAND;

#endif /* end of command.h */
