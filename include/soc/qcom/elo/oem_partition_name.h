/*!
  @file
  oem_partition_name.h
*/

#ifndef __OEM_PARTITION_NAME_H
#define __OEM_PARTITION_NAME_H

#ifdef __cplusplus
extern "C" {
#endif

// symbolic links used in android
#define QFA_LINK_STR		"/dev/block/bootdevice/by-name/Qfa"
#define QCFG_LINK_STR		"/dev/block/bootdevice/by-name/Qcfg"
#define QOTP_LINK_STR		"/dev/block/bootdevice/by-name/QOTP"
#define QDLOG_LINK_STR		"/dev/block/bootdevice/by-name/Qdlog"
#define QVARIABLE_LINK_STR	"/dev/block/bootdevice/by-name/Qvariables"
#define QLOGFILTER_LINK_STR	"/dev/block/bootdevice/by-name/Qlogfilter"
#define QGLOG_LINK_STR		"/dev/block/bootdevice/by-name/Qglog"
#define FSG_LINK_STR		"/dev/block/bootdevice/by-name/fsg"
#define QL1LOG_LINK_STR		"/dev/block/bootdevice/by-name/QL1Log"
#define QMDMCFG_LINK_STR	"/dev/block/bootdevice/by-name/QModemCfg"


// partition names used in aboot
#define PART_NAME_LEN_MAX	16
#define PART_FA				"Qfa"
#define PART_CFG			"Qcfg"
#define PART_OTP			"QOTP"
#define PART_VAR			"Qvariables"
#define PART_DLOG			"Qdlog"
#define PART_QMODEMCFG		"QModemCfg"

#ifdef __cplusplus
}
#endif
#endif /* __OEM_PARTITION_NAME_H */
