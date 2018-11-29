/*!
  @file
  qvariables.h
*/

#ifndef __QVARIABLE_H
#define __QVARIABLE_H

#ifdef __cplusplus
extern "C" {
#endif

#define VAR_LEN     5

#define VAR_ROOT_STR        "root"
#define VAR_ROOT_OFFSET     0x100
#define VAR_ROOT_OnSTR      "R1oOt"

#define VAR_ADB_STR         "adb"
#define VAR_ADB_OFFSET      0x252
#define VAR_ADB_OnSTR       "qa2Db"

#define VAR_DIAG_STR        "diag"
#define VAR_DIAG_OFFSET     0x3C3
#define VAR_DIAG_OnSTR      "DIa3g"

#define VAR_FTD_STR         "ftd"
#define VAR_FTD_OFFSET      0x507
#define VAR_FTD_OnSTR       "ftdQ4"

#define VAR_UART_STR        "uart"
#define VAR_UART_OFFSET     0x66D
#define VAR_UART_OnSTR      "U5ARt"

#define VAR_FIXUSBID_STR    "fixusbid"
#define VAR_FIXUSBID_OFFSET 0x7C2
#define VAR_FIXUSBID_OnSTR  "Us6bQ"

#define VAR_DEBUGFS_STR     "debugfs"
#define VAR_DEBUGFS_OFFSET  0x917
#define VAR_DEBUGFS_OnSTR   "DbG7q"

#define VAR_FASTBOOT_STR       "fastboot"
#define VAR_FASTBOOT_OFFSET    0xA5B
#define VAR_FASTBOOT_OnSTR     "FaBT8"

#define VAR_UARTCONSOLE_STR     "uartconsole"
#define VAR_UARTCONSOLE_OFFSET  0xBA7
#define VAR_UARTCONSOLE_OnSTR   "9nUrL"

#define VAR_FTDIP_STR           "ftdip"
#define VAR_FTDIP_OFFSET        0xC81
#define VAR_FTDIP_OnSTR         "f0TP1"

#define VAR_DEBUGLEVEL_STR          "debuglevel"
#define VAR_DEBUGLEVEL_OFFSET       0xD00
#define VAR_DEBUGLEVEL_OnSTR        "D36ug"
#define VAR_DEBUGLEVEL_VALUE_OFFSET 0xD10

#define VAR_ADBTCPPORT_STR      "adbtcpport"
#define VAR_ADBTCPPORT_OFFSET   0xEBA
#define VAR_ADBTCPPORT_OnSTR    "ptAdb"

#define VAR_QDLMODE_STR     "qdlmode"
#define VAR_QDLMODE_OFFSET  0xF42
#define VAR_QDLMODE_OnSTR   "9d1md"

#define VAR_DO_FSCK_STR     "do_fsck"
#define VAR_DO_FSCK_OFFSET  0x10A4
#define VAR_DO_FSCK_OnSTR   "dfsck"

#define VAR_SU_STR          "superuser"
#define VAR_SU_OFFSET       0x11B7
#define VAR_SU_OnSTR        "su_5U"

#define VAR_NO_ZYGOTE_INIT_STR    "nozygoteinit"
#define VAR_NO_ZYGOTE_INIT_OFFSET 0x1200
#define VAR_NO_ZYGOTE_INIT_OnSTR  "NoZyg"

#define VAR_PERMISSIVE_STR      "permissive"
#define VAR_PERMISSIVE_OFFSET   0x1261
#define VAR_PERMISSIVE_OnSTR    "p3rm1"

#define VAR_TPUSB_STR      "tp_usb_if"
#define VAR_TPUSB_OFFSET   0x1300
#define VAR_TPUSB_OnSTR    "tpusb"

#define VAR_UNLOCK_STR      "unlock"
#define VAR_UNLOCK_OFFSET   0x4F45
#define VAR_UNLOCK_OnSTR    "U_l_C"


// name, offset, length, and on value pair
#define VAR_INFO_ITEM(x)    {VAR_##x##_STR, VAR_##x##_OFFSET, VAR_LEN, VAR_##x##_OnSTR}

// name, offset, length, and on value pair array
#define VAR_INFO_TABLE \
    static const struct { \
        char *name; \
        unsigned int offset; \
        unsigned int length; \
        unsigned char on_value[16]; \
    } variables_info_table[] = { \
        VAR_INFO_ITEM(ROOT), \
        VAR_INFO_ITEM(ADB), \
        VAR_INFO_ITEM(DIAG), \
        VAR_INFO_ITEM(FTD), \
        VAR_INFO_ITEM(UART), \
        VAR_INFO_ITEM(DEBUGFS), \
        VAR_INFO_ITEM(FASTBOOT), \
        VAR_INFO_ITEM(UARTCONSOLE), \
        VAR_INFO_ITEM(FTDIP), \
        VAR_INFO_ITEM(ADBTCPPORT), \
        VAR_INFO_ITEM(QDLMODE), \
        VAR_INFO_ITEM(SU), \
        VAR_INFO_ITEM(NO_ZYGOTE_INIT), \
        VAR_INFO_ITEM(PERMISSIVE), \
        VAR_INFO_ITEM(TPUSB), \
        VAR_INFO_ITEM(UNLOCK), \
    }

#ifdef __cplusplus
}
#endif
#endif /* __QVARIABLE_H */
