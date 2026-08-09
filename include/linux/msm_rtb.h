/*
 * Copyright (c) 2012-2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __MSM_RTB_H__
#define __MSM_RTB_H__

/*
 * These numbers are used from the kernel command line and sysfs
 * to control filtering. Remove items from here with extreme caution.
 */
enum logk_event_type {
	LOGK_NONE = 0,
	LOGK_READL = 1,
	LOGK_WRITEL = 2,
	LOGK_LOGBUF = 3,
	LOGK_HOTPLUG = 4,
	LOGK_CTXID = 5,
	LOGK_TIMESTAMP = 6,
	LOGK_L2CPREAD = 7,
	LOGK_L2CPWRITE = 8,
	LOGK_IRQ = 9,
};

#define LOGTYPE_NOPC 0x80

struct msm_rtb_platform_data {
	unsigned int size;
};

#if defined(CONFIG_QCOM_RTB)
/*
 * returns 1 if data was logged, 0 otherwise
 */
int uncached_logk_pc(enum logk_event_type log_type, void *caller,
				void *data);

/*
 * returns 1 if data was logged, 0 otherwise
 */
int uncached_logk(enum logk_event_type log_type, void *data);

/*
 * EloI2 A14 bring-up 2026-08-09. Dump the newest `n` RTB entries with pr_crit.
 *
 * The RTB has been recording every readl()/writel() on this device all along -
 * CONFIG_QCOM_RTB=y and the cmdline already carries msm_rtb.filter=0x237, which
 * has LOGK_READL and LOGK_WRITEL set. Each entry holds the MMIO address that was
 * touched and the return address of the caller that touched it. Nothing has ever
 * read that buffer back. This is the reader.
 *
 * Called from bad_mode() on an SError so we can see which register access the
 * external abort belongs to. Logging is switched off before the walk so that the
 * printing itself does not overwrite the evidence.
 */
void msm_rtb_dump_last(unsigned int n);

/*
 * EloI2 A14 ROUND I. Record a fatal abort into the persistent RTB header so the
 * NEXT boot can print it. Round H proved printk cannot be relied on at the
 * moment of death on this device (finding F75), so this deliberately does
 * nothing but plain stores to uncached memory and a barrier: no locks, no
 * console, no allocation. Call it as the FIRST thing in the abort path.
 *
 * It also disables RTB logging, so the printing that follows - which drives the
 * UART, which is MMIO - cannot overwrite the register trace being preserved.
 */
void msm_rtb_note_fault(unsigned int esr, unsigned long pc, unsigned long lr,
			unsigned long sp, unsigned long far,
			unsigned long pstate);

#define ETB_WAYPOINT  do { \
				BRANCH_TO_NEXT_ISTR; \
				nop(); \
				BRANCH_TO_NEXT_ISTR; \
				nop(); \
			} while (0)

#define BRANCH_TO_NEXT_ISTR  asm volatile("b .+4\n" : : : "memory")
/*
 * both the mb and the isb are needed to ensure enough waypoints for
 * etb tracing
 */
#define LOG_BARRIER	do { \
				mb(); \
				isb();\
			 } while (0)
#else

static inline int uncached_logk_pc(enum logk_event_type log_type,
					void *caller,
					void *data) { return 0; }

static inline int uncached_logk(enum logk_event_type log_type,
					void *data) { return 0; }

static inline void msm_rtb_dump_last(unsigned int n) { }

static inline void msm_rtb_note_fault(unsigned int esr, unsigned long pc,
					unsigned long lr, unsigned long sp,
					unsigned long far,
					unsigned long pstate) { }

#define ETB_WAYPOINT
#define BRANCH_TO_NEXT_ISTR
/*
 * Due to a GCC bug, we need to have a nop here in order to prevent an extra
 * read from being generated after the write.
 */
#define LOG_BARRIER		nop()
#endif
#endif
