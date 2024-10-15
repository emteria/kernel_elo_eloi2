/*!
  @file
  qotp_api.h
*/

#ifndef _QOTP_API_H_
#define _QOTP_API_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "qotp.h"

///////////////////////////QOTP interface//////////////////////////////////////
static inline unsigned int otp_tail_len(void)
{
	return sizeof(qcfg_items_tail_type_lock);
}
#define QOTP_TAIL_LENGTH otp_tail_len()

static inline int qotp_is_valid(char * tail_buf, int item, uint32_t cal_crc)
{
	qcfg_items_tail_type_lock * read_tail = (qcfg_items_tail_type_lock *)tail_buf;

	return ((cal_crc == read_tail->base.checksum ) && ( read_tail->base.checksum != 0) &&
			(read_tail->base.magic == QOTP_TAIL_MAGIC) && (read_tail->base.flag == QOTP_FLAG_SEED * ( ( unsigned int)item + 1)));
}

static inline void qotp_prepare_tail(char * tail_buf, int item, uint32_t cal_crc)
{
	qcfg_items_tail_type_lock * write_tail = (qcfg_items_tail_type_lock *)tail_buf;

	write_tail->base.magic = QOTP_TAIL_MAGIC;
	write_tail->base.checksum = (unsigned int)cal_crc;
	write_tail->base.flag = QOTP_FLAG_SEED * (item + 1);
}

static inline void qotp_data_process(char * data, unsigned int length)
{
	for (unsigned int i = 0; i < length; i++) {
		*(data + i) ^= QOTP_NUMBER_XOR;
	}
}

static inline int qotp_check_data_format(int item, char * data, unsigned int length)
{
	unsigned int i;

	/*check for MAC address*/
	if( item >= 3 && item <= 5)
	{
		/*check is x digit*/
		for( i = 0; i < length; i++)
		{
			if( i % 3 == 2)
			{
				if(data[i] != ':')
					return -1;
				continue;
			}

			if( !(data[i] >= '0' && data[i]<= '9') && !(data[i] >= 'a' && data[i]<= 'f') && !(data[i] >= 'A' && data[i]<= 'F'))
				return -1;
		}

		/*check is all f*/
		for( i = 0; i < length; i++)
		{
			if( data[i] != 'f' && data[i] != 'F')
				return 0;
		}
		return -1; //data is all f
	}

	return 0;
}

#ifdef __cplusplus
}
#endif
#endif /* _QOTP_API_H_*/
