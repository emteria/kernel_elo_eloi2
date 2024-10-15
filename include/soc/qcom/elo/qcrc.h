/*!
  @file
  qcrc.h
*/

#ifndef _QCRC_H_
#define _QCRC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define CRC_32_SEED             0x00000000UL

static uint32_t crc_table[256];

static void init_crc_table(void);
static uint32_t crc_32_calc(unsigned char *buffer, unsigned int size, uint32_t crc) ;


static inline void init_crc_table(void)
{
	uint32_t c;
	unsigned int i, j;

	for (i = 0; i < 256; i++) {
		c = (unsigned int)i;
		for (j = 0; j < 8; j++) {
			if (c & 1)
				c = 0xedb88320L ^ (c >> 1);
			else
				c = c >> 1;
		}
		crc_table[i] = c;
	}
}
static inline uint32_t crc_32_calc(unsigned char *buffer, unsigned int size, uint32_t crc)
{
	unsigned int i;
	for (i = 0; i < size; i++) {
		crc = crc_table[(crc ^ buffer[i]) & 0xff] ^ (crc >> 8);
	}
	return crc ;
}


#ifdef __cplusplus
}
#endif
#endif /* _QOTP_H_*/
