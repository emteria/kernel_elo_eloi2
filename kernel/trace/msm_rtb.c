/*
 * Copyright (c) 2013-2018, The Linux Foundation. All rights reserved.
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

#include <linux/atomic.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/atomic.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/io.h>
/* EloI2 A14 ROUND K: the memory probe */
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <asm-generic/sizes.h>
#include <linux/msm_rtb.h>
#include <asm/timex.h>
#include <soc/qcom/minidump.h>

#define SENTINEL_BYTE_1 0xFF
#define SENTINEL_BYTE_2 0xAA
#define SENTINEL_BYTE_3 0xFF

#define RTB_COMPAT_STR	"qcom,msm-rtb"

/* Write
 * 1) 3 bytes sentinel
 * 2) 1 bytes of log type
 * 3) 8 bytes of where the caller came from
 * 4) 4 bytes index
 * 4) 8 bytes extra data from the caller
 * 5) 8 bytes of timestamp
 * 6) 8 bytes of cyclecount
 *
 * Total = 40 bytes.
 */
struct msm_rtb_layout {
	unsigned char sentinel[3];
	unsigned char log_type;
	uint32_t idx;
	uint64_t caller;
	uint64_t data;
	uint64_t timestamp;
	uint64_t cycle_count;
} __attribute__ ((__packed__));


/*
 * EloI2 A14 bring-up 2026-08-09 evening, ROUND I.
 *
 * WHY THIS HEADER EXISTS. Round H established (finding F75) that this device is
 * reset roughly a millisecond after the abort, between two adjacent pr_crit()
 * calls, so msm_rtb_dump_last() can never run in the fault path here. The only
 * way to read the trace back is to keep the buffer at a FIXED physical address
 * and replay it on the NEXT boot, the way pstore/ramoops already does on this
 * board. This 64-byte header is what lets the next boot recognise the region.
 *
 * The buffer needs no live write index: uncached_logk_pc_idx() already stamps
 * every entry with its own monotonic idx, so the newest entry can be recovered
 * by scanning. That keeps the hot path (every readl/writel on the system)
 * completely unchanged - not one extra store.
 *
 * self_addr is a cheap build check. If the kernel that wrote the trace is not
 * the kernel replaying it, the %pS symbols would be silently wrong; comparing
 * the address of a known function catches a reflash and lets us say so.
 */
#define RTB_PERSIST_MAGIC	0x52544250	/* 'RTBP' */
#define RTB_PERSIST_VERSION	1
#define RTB_FAULT_MAGIC		0x46415554	/* 'FAUT' - a fault record is present */

/*
 * THE LAST-WORDS RECORD, and why it is not just belt and braces.
 *
 * The one console line we do get out is 88 characters, which is 7.6 ms of wire
 * time at 115200. So the SoC survives at least that long, yet the very next
 * pr_crit() produces not one character. Whether that is because the CPU stops
 * executing or because printk defers, the conclusion for an instrument is the
 * same: DO NOT PUT THE PAYLOAD BEHIND printk.
 *
 * msm_rtb_note_fault() below writes these fields with plain stores to uncached
 * memory and a barrier. No locks, no console, no allocation, nothing that can
 * block or be deferred. If the CPU executes the store at all, the next boot
 * prints it.
 */
struct msm_rtb_persist_hdr {
	uint32_t magic;
	uint32_t version;
	uint32_t nentries;
	uint32_t entry_size;
	uint32_t boot_count;
	uint32_t fault_valid;		/* RTB_FAULT_MAGIC once written */
	uint64_t self_addr;
	uint32_t fault_esr;
	uint32_t fault_cpu;
	uint64_t fault_pc;
	uint64_t fault_lr;
	uint64_t fault_sp;
	uint64_t fault_far;
	uint64_t fault_ts;
	uint64_t fault_pstate;
	/*
	 * ROUND K - THE MEMORY PROBE'S CROSS-REBOOT CURSOR.
	 * probe_cur_pfn is written BEFORE each page is touched. If touching a
	 * page resets the SoC instead of aborting - and F83 says some accesses
	 * do exactly that - then the next boot finds the pfn that killed us
	 * still sitting here, records it, and resumes past it. That turns a
	 * fatal probe into a bisection that completes across reboots instead of
	 * a device that will not boot.
	 */
	uint32_t probe_state;		/* RTB_PROBE_* below */
	uint32_t probe_cur_pfn;		/* page being touched right now */
	uint32_t probe_resume_pfn;	/* where the next boot should start */
	uint32_t probe_killer_count;	/* pfns that reset the SoC when touched */
	uint32_t probe_killer[8];	/* the first few of them */
	uint32_t probe_bad_count;	/* pfns that aborted but were survivable */
	uint32_t reserved[3];
} __attribute__ ((__packed__));		/* 176 bytes, keeps entries 8-aligned */

#define RTB_PROBE_IDLE		0
#define RTB_PROBE_RUNNING	1
#define RTB_PROBE_DONE		2

struct msm_rtb_state {
	struct msm_rtb_layout *rtb;
	phys_addr_t phys;
	int nentries;
	int size;
	int enabled;
	int initialized;
	uint32_t filter;
	int step_size;
	struct msm_rtb_persist_hdr *persist_hdr;
	void __iomem *persist_base;
	int persistent;
};

#if defined(CONFIG_QCOM_RTB_SEPARATE_CPUS)
DEFINE_PER_CPU(atomic_t, msm_rtb_idx_cpu);
#else
static atomic_t msm_rtb_idx;
#endif

static struct msm_rtb_state msm_rtb = {
	.filter = 1 << LOGK_LOGBUF,
	.enabled = 1,
};

module_param_named(filter, msm_rtb.filter, uint, 0644);
module_param_named(enable, msm_rtb.enabled, int, 0644);

/*
 * How many RAW entries the next-boot replay walks for its detailed tail. Runs of
 * identical entries are coalesced into one line, so the number of LINES printed
 * is much smaller than this - the 1 kHz tick arrives in clusters of 8 identical
 * records and collapses to a single line each. Tune from the cmdline with
 * msm_rtb.replay_entries=N; 0 disables the replay entirely.
 */
static unsigned int replay_entries = 256;
module_param_named(replay_entries, replay_entries, uint, 0644);

/*
 * ROUND J. The per-caller summary, and the reason it exists.
 *
 * Round I's tail-only view was misleading in a specific way worth recording: the
 * newest entries are dominated by whichever driver does the MOST MMIO, which at
 * idle is not necessarily the one that matters. Reading "eMMC was last" off a
 * 5 ms keyhole says little when eMMC is simply the busiest device on the bus.
 *
 * This walks the WHOLE ring - now about 5 seconds, which covers the entire
 * boot_completed-to-death window from F77 - and reports per calling function:
 * how many accesses, and how long before the end its FIRST and LAST one happened.
 * That answers "which driver genuinely went quiet last" and "what only appears
 * once", instead of "who is noisiest".
 */
#define RTB_SUMMARY_MAX 64

struct rtb_caller_stat {
	uint64_t caller;
	uint64_t newest_ts;
	uint64_t oldest_ts;
	uint32_t count;
	unsigned char log_type;
};

static struct rtb_caller_stat rtb_summary[RTB_SUMMARY_MAX];

static int msm_rtb_panic_notifier(struct notifier_block *this,
					unsigned long event, void *ptr)
{
	msm_rtb.enabled = 0;
	return NOTIFY_DONE;
}

static struct notifier_block msm_rtb_panic_blk = {
	.notifier_call  = msm_rtb_panic_notifier,
	.priority = INT_MAX,
};

int notrace msm_rtb_event_should_log(enum logk_event_type log_type)
{
	return msm_rtb.initialized && msm_rtb.enabled &&
		((1 << (log_type & ~LOGTYPE_NOPC)) & msm_rtb.filter);
}
EXPORT_SYMBOL(msm_rtb_event_should_log);

static void msm_rtb_emit_sentinel(struct msm_rtb_layout *start)
{
	start->sentinel[0] = SENTINEL_BYTE_1;
	start->sentinel[1] = SENTINEL_BYTE_2;
	start->sentinel[2] = SENTINEL_BYTE_3;
}

static void msm_rtb_write_type(enum logk_event_type log_type,
			struct msm_rtb_layout *start)
{
	start->log_type = (char)log_type;
}

static void msm_rtb_write_caller(uint64_t caller, struct msm_rtb_layout *start)
{
	start->caller = caller;
}

static void msm_rtb_write_idx(uint32_t idx,
				struct msm_rtb_layout *start)
{
	start->idx = idx;
}

static void msm_rtb_write_data(uint64_t data, struct msm_rtb_layout *start)
{
	start->data = data;
}

static void msm_rtb_write_timestamp(struct msm_rtb_layout *start)
{
	start->timestamp = sched_clock();
}

static void msm_rtb_write_cyclecount(struct msm_rtb_layout *start)
{
	start->cycle_count = get_cycles();
}

static void uncached_logk_pc_idx(enum logk_event_type log_type, uint64_t caller,
				 uint64_t data, int idx)
{
	struct msm_rtb_layout *start;

	start = &msm_rtb.rtb[idx & (msm_rtb.nentries - 1)];

	msm_rtb_emit_sentinel(start);
	msm_rtb_write_type(log_type, start);
	msm_rtb_write_caller(caller, start);
	msm_rtb_write_idx(idx, start);
	msm_rtb_write_data(data, start);
	msm_rtb_write_timestamp(start);
	msm_rtb_write_cyclecount(start);
	mb();

}

static void uncached_logk_timestamp(int idx)
{
	unsigned long long timestamp;

	timestamp = sched_clock();
	uncached_logk_pc_idx(LOGK_TIMESTAMP|LOGTYPE_NOPC,
			(uint64_t)lower_32_bits(timestamp),
			(uint64_t)upper_32_bits(timestamp), idx);
}

#if defined(CONFIG_QCOM_RTB_SEPARATE_CPUS)
static int msm_rtb_get_idx(void)
{
	int cpu, i, offset;
	atomic_t *index;

	/*
	 * ideally we would use get_cpu but this is a close enough
	 * approximation for our purposes.
	 */
	cpu = raw_smp_processor_id();

	index = &per_cpu(msm_rtb_idx_cpu, cpu);

	i = atomic_add_return(msm_rtb.step_size, index);
	i -= msm_rtb.step_size;

	/* Check if index has wrapped around */
	offset = (i & (msm_rtb.nentries - 1)) -
		 ((i - msm_rtb.step_size) & (msm_rtb.nentries - 1));
	if (offset < 0) {
		uncached_logk_timestamp(i);
		i = atomic_add_return(msm_rtb.step_size, index);
		i -= msm_rtb.step_size;
	}

	return i;
}
#else
static int msm_rtb_get_idx(void)
{
	int i, offset;

	i = atomic_inc_return(&msm_rtb_idx);
	i--;

	/* Check if index has wrapped around */
	offset = (i & (msm_rtb.nentries - 1)) -
		 ((i - 1) & (msm_rtb.nentries - 1));
	if (offset < 0) {
		uncached_logk_timestamp(i);
		i = atomic_inc_return(&msm_rtb_idx);
		i--;
	}

	return i;
}
#endif

int notrace uncached_logk_pc(enum logk_event_type log_type, void *caller,
				void *data)
{
	int i;

	if (!msm_rtb_event_should_log(log_type))
		return 0;

	i = msm_rtb_get_idx();
	uncached_logk_pc_idx(log_type, (uint64_t)((unsigned long) caller),
				(uint64_t)((unsigned long) data), i);

	return 1;
}
EXPORT_SYMBOL(uncached_logk_pc);

noinline int notrace uncached_logk(enum logk_event_type log_type, void *data)
{
	return uncached_logk_pc(log_type, __builtin_return_address(0), data);
}
EXPORT_SYMBOL(uncached_logk);

/*
 * EloI2 A14 bring-up 2026-08-09. See the comment on the prototype in
 * include/linux/msm_rtb.h for why this exists.
 *
 * WHY THE ENTRIES ARE USEFUL FOR AN SError: arch/arm64/include/asm/io.h:109-135
 * logs the access BEFORE performing it, and records `data` = the MMIO address and
 * `caller` = __builtin_return_address(0) of the accessor. So the newest entries are
 * the last register accesses the CPU made before the abort was delivered.
 *
 * READ THE RESULT WITH CARE. An SError is ASYNCHRONOUS - the abort is reported when
 * the CPU next takes an exception, not at the instruction that caused it, and
 * Cortex-A53 has no ESB to make it precise. The newest entry is therefore a STRONG
 * HINT, not proof. What makes it usable is repetition: if the same driver and the
 * same register window sit at the top of this list across several deaths, that is
 * the culprit. One sample is not.
 */
static const char *msm_rtb_type_name(unsigned char log_type)
{
	switch (log_type & ~LOGTYPE_NOPC) {
	case LOGK_NONE:		return "none";
	case LOGK_READL:	return "READL";
	case LOGK_WRITEL:	return "WRITEL";
	case LOGK_LOGBUF:	return "logbuf";
	case LOGK_HOTPLUG:	return "hotplug";
	case LOGK_CTXID:	return "ctxid";
	case LOGK_TIMESTAMP:	return "tstamp";
	case LOGK_L2CPREAD:	return "l2cprd";
	case LOGK_L2CPWRITE:	return "l2cpwr";
	case LOGK_IRQ:		return "irq";
	default:		return "?";
	}
}

void msm_rtb_dump_last(unsigned int n)
{
	int cur, i, shown = 0;

	if (!msm_rtb.initialized) {
		pr_crit("RTB: not initialized, no register trace available\n");
		return;
	}

	/*
	 * Stop logging first. Everything below calls printk, printk drives the
	 * UART, and the UART is MMIO - without this the dump would overwrite the
	 * very entries it is trying to show.
	 */
	msm_rtb.enabled = 0;
	mb();

	cur = atomic_read(&msm_rtb_idx);

	if (n > (unsigned int)msm_rtb.nentries)
		n = msm_rtb.nentries;

	pr_crit("RTB: ==== last %u of %d register accesses, NEWEST FIRST ====\n",
		n, msm_rtb.nentries);
	pr_crit("RTB: filter=0x%x idx=%d (READL/WRITEL logged = %s)\n",
		msm_rtb.filter, cur,
		((msm_rtb.filter & ((1 << LOGK_READL) | (1 << LOGK_WRITEL))) ==
		 ((1 << LOGK_READL) | (1 << LOGK_WRITEL))) ? "both" : "PARTIAL");

	for (i = 1; i <= (int)n; i++) {
		struct msm_rtb_layout *e;

		e = &msm_rtb.rtb[(cur - i) & (msm_rtb.nentries - 1)];

		/* the region is uncached DMA memory - it may hold anything */
		if (e->sentinel[0] != SENTINEL_BYTE_1 ||
		    e->sentinel[1] != SENTINEL_BYTE_2 ||
		    e->sentinel[2] != SENTINEL_BYTE_3)
			continue;

		pr_crit("RTB %3d: %-7s addr=0x%016llx ts=%llu from %pS\n",
			i, msm_rtb_type_name(e->log_type),
			(unsigned long long)e->data,
			(unsigned long long)e->timestamp,
			(void *)(uintptr_t)e->caller);
		shown++;
	}

	pr_crit("RTB: ==== end of register trace, %d valid entries ====\n", shown);
}
EXPORT_SYMBOL(msm_rtb_dump_last);

/*
 * EloI2 A14 ROUND I. Called from the arm64 abort paths. Plain stores to uncached
 * memory plus a barrier - no locks, no printk, nothing that can defer. Safe to
 * call from bad_mode() and do_mem_abort() with interrupts in any state.
 *
 * It also stops the RTB, so that everything the fault path does afterwards -
 * printk drives the UART, and the UART is MMIO - cannot overwrite the register
 * trace we are trying to preserve for the next boot.
 */
void msm_rtb_note_fault(unsigned int esr, unsigned long pc, unsigned long lr,
			unsigned long sp, unsigned long far,
			unsigned long pstate)
{
	struct msm_rtb_persist_hdr *hdr = msm_rtb.persist_hdr;

	msm_rtb.enabled = 0;
	mb();

	if (!msm_rtb.persistent || !hdr)
		return;

	hdr->fault_esr    = esr;
	hdr->fault_cpu    = raw_smp_processor_id();
	hdr->fault_pc     = pc;
	hdr->fault_lr     = lr;
	hdr->fault_sp     = sp;
	hdr->fault_far    = far;
	hdr->fault_pstate = pstate;
	hdr->fault_ts     = sched_clock();
	mb();
	hdr->fault_valid  = RTB_FAULT_MAGIC;	/* published last, on purpose */
	mb();
}
EXPORT_SYMBOL(msm_rtb_note_fault);

/*
 * EloI2 A14 ROUND I - THE NEXT-BOOT REPLAY. This is the whole point of the round.
 *
 * Round H's in-fault dump can never run on this device (F75). This runs instead,
 * at probe time on the FOLLOWING boot, against the same physical buffer, and it
 * prints the register accesses the machine made just before it was reset.
 *
 * FINDING THE NEWEST ENTRY WITHOUT A LIVE INDEX. Every entry carries the
 * monotonic idx it was written with, and it lives at slot (idx & (nentries-1)).
 * An entry is therefore only believable if it has the sentinel AND its idx hashes
 * back to the slot it was found in - that rejects stale entries from an older,
 * differently sized buffer and rejects uninitialised DRAM. The largest surviving
 * idx is the newest write.
 *
 * WHAT THE NUMBERS MEAN. `addr` is the MMIO address that was about to be touched
 * and `from` is the caller, because arch/arm64/include/asm/io.h:109-135 logs the
 * access BEFORE performing it. `age` is how long before the last recorded access
 * this one happened, which is the number to read: a burst of accesses into one
 * register window in the last few microseconds is the signature we are hunting.
 *
 * THIS IS STILL AN ASYNCHRONOUS ABORT. One boot's trace is a hint. The test is
 * REPETITION across several deaths - same driver, same window, at the top.
 */
static void msm_rtb_replay_previous(struct msm_rtb_persist_hdr *hdr,
				    struct msm_rtb_layout *ent,
				    int nentries, phys_addr_t phys)
{
	int i, slot, newest_slot = -1, shown = 0, valid = 0;
	int32_t newest_idx = 0;
	uint64_t newest_ts = 0;
	unsigned int want = replay_entries;

	/* %pa prints its own 0x prefix - do not add another one (F81) */
	pr_info("RTB-REPLAY: region %pa, %d entries of %zu bytes\n",
		&phys, nentries, sizeof(struct msm_rtb_layout));

	if (hdr->magic != RTB_PERSIST_MAGIC) {
		pr_info("RTB-REPLAY: no previous trace (magic 0x%08x != 0x%08x). Expected on the first boot after a fastboot flash, or if this region does not survive a reset on this board.\n",
			hdr->magic, RTB_PERSIST_MAGIC);
		return;
	}

	if (hdr->version != RTB_PERSIST_VERSION ||
	    hdr->entry_size != sizeof(struct msm_rtb_layout) ||
	    (int)hdr->nentries != nentries) {
		pr_warn("RTB-REPLAY: header mismatch (version %u, entry_size %u, nentries %u) - layout changed between builds, discarding\n",
			hdr->version, hdr->entry_size, hdr->nentries);
		return;
	}

	if (hdr->self_addr != (uint64_t)(uintptr_t)&msm_rtb_dump_last)
		pr_warn("RTB-REPLAY: *** the previous boot ran a DIFFERENT KERNEL (self 0x%llx, now 0x%llx). The addresses below are real but the %%pS symbol names are from THIS build and are NOT trustworthy. Read the raw addresses. ***\n",
			(unsigned long long)hdr->self_addr,
			(unsigned long long)(uintptr_t)&msm_rtb_dump_last);

	pr_info("RTB-REPLAY: previous boot_count %u\n", hdr->boot_count);

	/*
	 * The last-words record first - it is the single most valuable line in
	 * this whole block, because it is the one the previous boot managed to
	 * write without needing printk to work.
	 */
	if (hdr->fault_valid == RTB_FAULT_MAGIC) {
		pr_warn("RTB-REPLAY: *** THE PREVIOUS BOOT TOOK A FATAL ABORT AND RECORDED IT ***\n");
		pr_warn("RTB-REPLAY: FAULT ESR=0x%08x on CPU%u at sched_clock %llu ns\n",
			hdr->fault_esr, hdr->fault_cpu,
			(unsigned long long)hdr->fault_ts);
		pr_warn("RTB-REPLAY: FAULT PC =0x%016llx %pS\n",
			(unsigned long long)hdr->fault_pc,
			(void *)(uintptr_t)hdr->fault_pc);
		pr_warn("RTB-REPLAY: FAULT LR =0x%016llx %pS\n",
			(unsigned long long)hdr->fault_lr,
			(void *)(uintptr_t)hdr->fault_lr);
		pr_warn("RTB-REPLAY: FAULT SP =0x%016llx FAR=0x%016llx PSTATE=0x%016llx\n",
			(unsigned long long)hdr->fault_sp,
			(unsigned long long)hdr->fault_far,
			(unsigned long long)hdr->fault_pstate);
		pr_warn("RTB-REPLAY: FAULT decode: EC=0x%02x IL=%u IDS=%u ISS=0x%06x. For an SError (EC 0x2f) the PC is where the CPU was when the abort was DELIVERED, not what caused it. For a data abort (EC 0x25) FAR is the faulting address and the PC is exact.\n",
			(hdr->fault_esr >> 26) & 0x3f,
			(hdr->fault_esr >> 25) & 1,
			(hdr->fault_esr >> 24) & 1,
			hdr->fault_esr & 0xffffff);
	} else {
		pr_info("RTB-REPLAY: no fault record - the previous boot died without reaching bad_mode() or do_mem_abort(), so whatever killed it did not go through a CPU abort at all\n");
	}

	for (slot = 0; slot < nentries; slot++) {
		struct msm_rtb_layout *e = &ent[slot];

		if (e->sentinel[0] != SENTINEL_BYTE_1 ||
		    e->sentinel[1] != SENTINEL_BYTE_2 ||
		    e->sentinel[2] != SENTINEL_BYTE_3)
			continue;
		if ((int)(e->idx & (nentries - 1)) != slot)
			continue;

		valid++;
		if (newest_slot < 0 || (int32_t)(e->idx - newest_idx) > 0) {
			newest_idx = e->idx;
			newest_slot = slot;
			newest_ts = e->timestamp;
		}
	}

	if (newest_slot < 0) {
		pr_info("RTB-REPLAY: header is valid but no entry survived - the region kept its header and lost its contents, or nothing was ever logged\n");
		return;
	}

	if (want > (unsigned int)nentries)
		want = nentries;

	/*
	 * ---- PART 1: THE WHOLE-RING SUMMARY, BY CALLER. Read this first. ----
	 */
	{
		int used = 0, dropped = 0, j, k;
		uint64_t span;

		memset(rtb_summary, 0, sizeof(rtb_summary));

		for (i = 0; i < nentries; i++) {
			struct msm_rtb_layout *e;

			slot = (newest_slot - i) & (nentries - 1);
			e = &ent[slot];

			if (e->sentinel[0] != SENTINEL_BYTE_1 ||
			    e->sentinel[1] != SENTINEL_BYTE_2 ||
			    e->sentinel[2] != SENTINEL_BYTE_3)
				continue;
			if ((int)(e->idx & (nentries - 1)) != slot)
				continue;
			/* only walk backwards through this boot's own writes */
			if ((int32_t)(e->idx - newest_idx) > 0)
				continue;

			for (j = 0; j < used; j++)
				if (rtb_summary[j].caller == e->caller)
					break;

			if (j == used) {
				if (used == RTB_SUMMARY_MAX) {
					dropped++;
					continue;
				}
				used++;
				rtb_summary[j].caller	 = e->caller;
				rtb_summary[j].log_type	 = e->log_type;
				rtb_summary[j].newest_ts = e->timestamp;
				rtb_summary[j].oldest_ts = e->timestamp;
				rtb_summary[j].count	 = 0;
			}

			rtb_summary[j].count++;
			if (e->timestamp > rtb_summary[j].newest_ts)
				rtb_summary[j].newest_ts = e->timestamp;
			if (e->timestamp < rtb_summary[j].oldest_ts)
				rtb_summary[j].oldest_ts = e->timestamp;
		}

		/* selection sort, most recently active caller first */
		for (j = 0; j < used; j++) {
			int best = j;

			for (k = j + 1; k < used; k++)
				if (rtb_summary[k].newest_ts >
				    rtb_summary[best].newest_ts)
					best = k;
			if (best != j) {
				struct rtb_caller_stat t = rtb_summary[j];

				rtb_summary[j] = rtb_summary[best];
				rtb_summary[best] = t;
			}
		}

		span = (used && rtb_summary[used - 1].oldest_ts < newest_ts) ?
			newest_ts - rtb_summary[used - 1].oldest_ts : 0;

		pr_info("RTB-REPLAY: ==== SUMMARY BY CALLER over the whole ring: %d valid entries covering %llu.%03llu ms, %d distinct callers%s ====\n",
			valid, span / 1000000, (span / 1000) % 1000, used,
			dropped ? " (TABLE FULL, some callers not shown)" : "");
		pr_info("RTB-REPLAY: SORTED BY LAST ACTIVITY. 'last' and 'first' are how long BEFORE THE END that caller was seen. The driver that went quiet last is at the top.\n");

		for (j = 0; j < used; j++) {
			long long last = (long long)newest_ts -
					 (long long)rtb_summary[j].newest_ts;
			long long first = (long long)newest_ts -
					  (long long)rtb_summary[j].oldest_ts;

			pr_info("RTB-SUMMARY %2d: %-7s n=%-6u last=-%lld.%03lld ms first=-%lld.%03lld ms %pS\n",
				j, msm_rtb_type_name(rtb_summary[j].log_type),
				rtb_summary[j].count,
				last / 1000000, (last / 1000) % 1000,
				first / 1000000, (first / 1000) % 1000,
				(void *)(uintptr_t)rtb_summary[j].caller);
		}
	}

	/*
	 * ---- PART 2: THE DETAILED TAIL, runs of identical records coalesced. ----
	 */
	pr_info("RTB-REPLAY: ==== detailed tail, last %u raw entries, NEWEST FIRST, identical runs coalesced ====\n",
		want);
	pr_info("RTB-REPLAY: newest idx %d in slot %d, its sched_clock was %llu ns into that boot\n",
		newest_idx, newest_slot, (unsigned long long)newest_ts);

	{
		uint64_t run_caller = 0, run_data = 0, run_first_ts = 0;
		unsigned char run_type = 0;
		int run_len = 0, have_run = 0;

		for (i = 0; i <= (int)want; i++) {
			struct msm_rtb_layout *e = NULL;
			int match = 0;

			if (i < (int)want) {
				slot = (newest_slot - i) & (nentries - 1);
				e = &ent[slot];

				if (e->sentinel[0] != SENTINEL_BYTE_1 ||
				    e->sentinel[1] != SENTINEL_BYTE_2 ||
				    e->sentinel[2] != SENTINEL_BYTE_3)
					continue;
				if ((int)(e->idx & (nentries - 1)) != slot)
					continue;

				match = have_run &&
					e->caller == run_caller &&
					e->data == run_data &&
					e->log_type == run_type;
			}

			if (match) {
				run_len++;
				continue;
			}

			if (have_run) {
				long long age = (long long)newest_ts -
						(long long)run_first_ts;
				char mult[16] = "";

				if (run_len > 1)
					snprintf(mult, sizeof(mult), " x%d",
						 run_len);

				pr_info("RTB-REPLAY %3d: %-7s addr=0x%016llx age=-%lld.%03lld us%s from 0x%016llx %pS\n",
					shown, msm_rtb_type_name(run_type),
					(unsigned long long)run_data,
					age / 1000, age % 1000, mult,
					(unsigned long long)run_caller,
					(void *)(uintptr_t)run_caller);
				shown++;
			}

			if (i == (int)want)
				break;

			run_caller   = e->caller;
			run_data     = e->data;
			run_type     = e->log_type;
			run_first_ts = e->timestamp;
			run_len      = 1;
			have_run     = 1;
		}
	}

	pr_info("RTB-REPLAY: ==== end of previous-boot trace, %d lines printed ====\n",
		shown);
}

/*
 * Map the fixed carveout named by the "memory-region" phandle, replay whatever
 * the previous boot left in it, then claim it for this boot.
 *
 * Returns 0 on success; on any failure the caller falls back to the stock
 * dma_alloc_coherent buffer and the trace simply does not survive a reset, which
 * is exactly the behaviour before this round.
 */
static int msm_rtb_init_persist(struct platform_device *pdev)
{
	struct msm_rtb_persist_hdr *hdr;
	struct msm_rtb_layout *ent;
	struct device_node *np;
	struct resource res;
	void __iomem *base;
	resource_size_t rsize;
	uint32_t prev_boot_count = 0;
	size_t entries_sz;
	int nentries, ret;

	if (!pdev->dev.of_node)
		return -ENODEV;

	np = of_parse_phandle(pdev->dev.of_node, "memory-region", 0);
	if (!np)
		return -ENODEV;

	ret = of_address_to_resource(np, 0, &res);
	of_node_put(np);
	if (ret)
		return ret;

	rsize = resource_size(&res);
	if (rsize <= sizeof(*hdr) + sizeof(struct msm_rtb_layout))
		return -EINVAL;

	/*
	 * ioremap_wc, not ioremap. On arm64 this is Normal Non-cacheable: plain
	 * (including unaligned) loads and stores are architecturally fine, which
	 * is what the rest of this driver does to the buffer, and writes land in
	 * DRAM rather than in a cache line that a reset would throw away. Device
	 * memory would break the unaligned accesses; cacheable memory would lose
	 * the last entries. The region is "no-map" in the DT so there is no
	 * cacheable linear-map alias of it.
	 */
	base = ioremap_wc(res.start, rsize);
	if (!base)
		return -ENOMEM;

	hdr = (struct msm_rtb_persist_hdr *)(void __force *)base;
	ent = (struct msm_rtb_layout *)(hdr + 1);

	entries_sz = rsize - sizeof(*hdr);
	nentries = entries_sz / sizeof(struct msm_rtb_layout);
	nentries = __rounddown_pow_of_two(nentries);
	if (nentries < 2) {
		iounmap(base);
		return -EINVAL;
	}

	/* Read the previous boot out BEFORE anything here writes to the region. */
	if (replay_entries)
		msm_rtb_replay_previous(hdr, ent, nentries, res.start);

	if (hdr->magic == RTB_PERSIST_MAGIC) {
		prev_boot_count = hdr->boot_count;
	} else {
		/*
		 * ROUND K: cold region - the probe cursor and its killer list are
		 * uninitialised DRAM, not state. Zero them, or the first scan
		 * would "resume" from a garbage pfn and silently skip most of
		 * memory. When the magic IS valid these fields are deliberately
		 * left alone: carrying them across the reset is the whole point.
		 */
		hdr->probe_state	= RTB_PROBE_IDLE;
		hdr->probe_cur_pfn	= 0;
		hdr->probe_resume_pfn	= 0;
		hdr->probe_killer_count	= 0;
		hdr->probe_bad_count	= 0;
		memset(hdr->probe_killer, 0, sizeof(hdr->probe_killer));
		mb();
	}

	memset(ent, 0, (size_t)nentries * sizeof(struct msm_rtb_layout));

	hdr->magic	 = RTB_PERSIST_MAGIC;
	hdr->version	 = RTB_PERSIST_VERSION;
	hdr->nentries	 = nentries;
	hdr->entry_size	 = sizeof(struct msm_rtb_layout);
	hdr->boot_count	 = prev_boot_count + 1;
	hdr->self_addr	 = (uint64_t)(uintptr_t)&msm_rtb_dump_last;
	/* clear the last-words record, this boot has not faulted yet */
	hdr->fault_valid = 0;
	hdr->fault_esr	 = 0;
	hdr->fault_cpu	 = 0;
	hdr->fault_pc	 = 0;
	hdr->fault_lr	 = 0;
	hdr->fault_sp	 = 0;
	hdr->fault_far	 = 0;
	hdr->fault_ts	 = 0;
	hdr->fault_pstate = 0;
	memset(hdr->reserved, 0, sizeof(hdr->reserved));
	mb();

	msm_rtb.persist_base	= base;
	msm_rtb.persist_hdr	= hdr;
	msm_rtb.persistent	= 1;
	msm_rtb.rtb		= ent;
	msm_rtb.phys		= res.start + sizeof(*hdr);
	msm_rtb.nentries	= nentries;
	msm_rtb.size		= nentries * sizeof(struct msm_rtb_layout);

	pr_info("RTB: PERSISTENT at %pa (%d entries), boot_count %u - the trace will survive a reset and replay on the next boot\n",
		&res.start, nentries, hdr->boot_count);

	return 0;
}

static int msm_rtb_probe(struct platform_device *pdev)
{
	struct msm_rtb_platform_data *d = pdev->dev.platform_data;
	struct md_region md_entry;
#if defined(CONFIG_QCOM_RTB_SEPARATE_CPUS)
	unsigned int cpu;
#endif
	int ret;

	if (!pdev->dev.of_node) {
		msm_rtb.size = d->size;
	} else {
		u64 size;
		struct device_node *pnode;

		pnode = of_parse_phandle(pdev->dev.of_node,
						"linux,contiguous-region", 0);
		if (pnode != NULL) {
			const u32 *addr;

			addr = of_get_address(pnode, 0, &size, NULL);
			if (!addr) {
				of_node_put(pnode);
				return -EINVAL;
			}
			of_node_put(pnode);
		} else {
			ret = of_property_read_u32(pdev->dev.of_node,
					"qcom,rtb-size",
					(u32 *)&size);
			if (ret < 0)
				return ret;

		}

		msm_rtb.size = size;
	}

	if (msm_rtb.size <= 0 || msm_rtb.size > SZ_1M)
		return -EINVAL;

	/*
	 * EloI2 A14 ROUND I: prefer the fixed carveout, because a buffer whose
	 * physical address changes every boot cannot be read back after the
	 * reset - see msm_rtb_init_persist(). Falling back is safe and is the
	 * pre-Round-I behaviour, but say so loudly, because a silent fallback
	 * would look exactly like "the region did not survive" in the next log.
	 */
	ret = msm_rtb_init_persist(pdev);
	if (ret) {
		dev_warn(&pdev->dev,
			"RTB: *** NOT PERSISTENT (memory-region unusable, %d) - falling back to dma_alloc_coherent. THE TRACE WILL NOT SURVIVE A RESET AND THERE WILL BE NO REPLAY NEXT BOOT. ***\n",
			ret);

		msm_rtb.rtb = dma_alloc_coherent(&pdev->dev, msm_rtb.size,
							&msm_rtb.phys,
							GFP_KERNEL);

		if (!msm_rtb.rtb)
			return -ENOMEM;

		msm_rtb.nentries = msm_rtb.size / sizeof(struct msm_rtb_layout);

		/* Round this down to a power of 2 */
		msm_rtb.nentries = __rounddown_pow_of_two(msm_rtb.nentries);

		memset(msm_rtb.rtb, 0, msm_rtb.size);
	}

	strlcpy(md_entry.name, "KRTB_BUF", sizeof(md_entry.name));
	md_entry.virt_addr = (uintptr_t)msm_rtb.rtb;
	md_entry.phys_addr = msm_rtb.phys;
	md_entry.size = msm_rtb.size;
	if (msm_minidump_add_region(&md_entry) < 0)
		pr_info("Failed to add RTB in Minidump\n");

#if defined(CONFIG_QCOM_RTB_SEPARATE_CPUS)
	for_each_possible_cpu(cpu) {
		atomic_t *a = &per_cpu(msm_rtb_idx_cpu, cpu);

		atomic_set(a, cpu);
	}
	msm_rtb.step_size = num_possible_cpus();
#else
	atomic_set(&msm_rtb_idx, 0);
	msm_rtb.step_size = 1;
#endif

	atomic_notifier_chain_register(&panic_notifier_list,
						&msm_rtb_panic_blk);
	msm_rtb.initialized = 1;
	return 0;
}

static const struct of_device_id msm_match_table[] = {
	{.compatible = RTB_COMPAT_STR},
	{},
};

static struct platform_driver msm_rtb_driver = {
	.driver         = {
		.name = "msm_rtb",
		.owner = THIS_MODULE,
		.of_match_table = msm_match_table
	},
};

/*
 * ============================================================================
 * EloI2 A14 ROUND K, 2026-08-10 - THE MEMORY PROBE. Finding F91.
 *
 * THE QUESTION IT ANSWERS. F88 established that PA 0xC6504000 refuses a plain
 * kernel read, repeatably, across two different kernel builds, and F90 proposed
 * that a physical region Linux believes it owns is the single cause behind all
 * four death signatures. F92 then showed the Android 7 device tree reserves
 * 110 MB that we do not - but ALSO that Android 7 does not reserve 0xC65xxxxx
 * either, so that particular page cannot be a missing static reservation.
 *
 * So: read every page of memory Linux thinks it owns, at init, and find out.
 *   * bad pages found here      -> the memory map is wrong and we know exactly
 *                                  what to reserve
 *   * all pages readable here   -> nothing is wrong with the map; something
 *                                  takes that memory away AT RUNTIME, and the
 *                                  hunt moves to hyp_assign / qseecom / ION
 *                                  secure (note F80: hyp_assign_phys already
 *                                  fails with EIO on this device)
 *   * all pages readable always -> F90 IS WRONG AND MUST BE ABANDONED.
 * All three are useful. That is what makes this worth a round.
 *
 * WHY IT IS SAFE TO RUN. A read cannot corrupt anything, and the abort is
 * caught: arch/arm64/mm/fault.c honours an exception-table fixup for kernel
 * external aborts (see the ROUND K comment there), so probe_read_byte() returns
 * an error instead of dying. The residual risk is a page whose access resets
 * the SoC outright rather than aborting - handled by the cross-reboot cursor in
 * the persistent header, so the worst case costs reboots, not the device.
 * ============================================================================
 */
static unsigned int probe_mem = 1;
module_param_named(probe_mem, probe_mem, uint, 0644);

/*
 * Periodic RE-SCAN, to catch memory that goes bad after init. DEFAULT OFF, and
 * that is a deliberate choice, not an oversight: this round's primary
 * experiment is the F92 device-tree restore, and a re-scan that itself resets
 * the SoC would make "did the DT fix work?" unanswerable. Turn it on in a
 * LATER round, once the DT verdict is in, with msm_rtb.probe_rescan_ms=<ms>.
 */
static unsigned int probe_rescan_ms;
module_param_named(probe_rescan_ms, probe_rescan_ms, uint, 0644);

static int notrace probe_read_byte(unsigned long addr)
{
	int err = 0;
	unsigned char val;

	asm volatile(
	"1:	ldrb	%w1, [%2]\n"
	"2:\n"
	"	.section .fixup, \"ax\"\n"
	"	.align	2\n"
	"3:	mov	%w0, %3\n"
	"	mov	%w1, #0\n"
	"	b	2b\n"
	"	.previous\n"
	_ASM_EXTABLE(1b, 3b)
	: "+r" (err), "=&r" (val)
	: "r" (addr), "i" (-EIO)
	: "memory");

	return err;
}

static void msm_rtb_probe_memory(const char *when)
{
	struct msm_rtb_persist_hdr *hdr = msm_rtb.persist_hdr;
	unsigned long pfn, start_pfn = 0;
	unsigned long checked = 0, bad = 0;
	unsigned long run_start = 0, run_len = 0;
	struct memblock_region *reg;

	if (hdr && hdr->probe_state == RTB_PROBE_RUNNING) {
		/*
		 * The previous boot did not come back from a probe. The page it
		 * was touching is the one that reset the SoC - the single most
		 * valuable number this whole mechanism can produce.
		 */
		unsigned int k = hdr->probe_cur_pfn;

		pr_err("MEMPROBE: *** THE PREVIOUS BOOT WAS RESET WHILE TOUCHING PFN 0x%x (PA 0x%llx). THAT ACCESS DOES NOT ABORT - IT KILLS THE SoC. ***\n",
		       k, (unsigned long long)k << PAGE_SHIFT);
		if (hdr->probe_killer_count < ARRAY_SIZE(hdr->probe_killer))
			hdr->probe_killer[hdr->probe_killer_count] = k;
		hdr->probe_killer_count++;
		start_pfn = k + 1;
		hdr->probe_resume_pfn = start_pfn;
		mb();
	} else if (hdr && hdr->probe_resume_pfn) {
		start_pfn = hdr->probe_resume_pfn;
	}

	if (hdr) {
		unsigned int i;

		if (hdr->probe_killer_count) {
			pr_err("MEMPROBE: %u page(s) have reset this device when touched, so far:\n",
			       hdr->probe_killer_count);
			for (i = 0; i < hdr->probe_killer_count &&
				    i < ARRAY_SIZE(hdr->probe_killer); i++)
				pr_err("MEMPROBE:   killer PA 0x%llx\n",
				       (unsigned long long)hdr->probe_killer[i]
				       << PAGE_SHIFT);
		}
		hdr->probe_state = RTB_PROBE_RUNNING;
		mb();
	}

	pr_info("MEMPROBE: %s - reading one byte from every page Linux owns, resuming at pfn 0x%lx\n",
		when, start_pfn);

	for_each_memblock(memory, reg) {
		unsigned long end;

		pfn = memblock_region_memory_base_pfn(reg);
		end = memblock_region_memory_end_pfn(reg);

		for (; pfn < end; pfn++) {
			unsigned long pa = (unsigned long)pfn << PAGE_SHIFT;

			if (pfn < start_pfn)
				continue;
			/*
			 * no-map regions are not in the linear map at all;
			 * touching them would be a translation fault, not the
			 * bus error we are hunting.
			 */
			if (!pfn_valid(pfn))
				continue;

			if (hdr) {
				hdr->probe_cur_pfn = pfn;
				mb();
			}

			checked++;

			if (probe_read_byte((unsigned long)__va(pa))) {
				bad++;
				if (run_len && pfn == run_start + run_len) {
					run_len++;
				} else {
					if (run_len)
						pr_err("MEMPROBE: *** UNREADABLE PA 0x%llx - 0x%llx (%lu pages) ***\n",
						       (unsigned long long)run_start << PAGE_SHIFT,
						       ((unsigned long long)(run_start + run_len) << PAGE_SHIFT) - 1,
						       run_len);
					run_start = pfn;
					run_len = 1;
				}
			}
		}
	}

	if (run_len)
		pr_err("MEMPROBE: *** UNREADABLE PA 0x%llx - 0x%llx (%lu pages) ***\n",
		       (unsigned long long)run_start << PAGE_SHIFT,
		       ((unsigned long long)(run_start + run_len) << PAGE_SHIFT) - 1,
		       run_len);

	if (hdr) {
		hdr->probe_bad_count = bad;
		hdr->probe_state = RTB_PROBE_DONE;
		hdr->probe_cur_pfn = 0;
		hdr->probe_resume_pfn = 0;
		mb();
	}

	if (bad)
		pr_err("MEMPROBE: %s COMPLETE - %lu of %lu pages UNREADABLE. The memory map Linux was given does not match the hardware.\n",
		       when, bad, checked);
	else
		pr_info("MEMPROBE: %s COMPLETE - all %lu pages readable. Nothing is statically wrong with the memory map at this point in the boot.\n",
			when, checked);
}

static void msm_rtb_probe_work_fn(struct work_struct *work);
static DECLARE_DELAYED_WORK(msm_rtb_probe_work, msm_rtb_probe_work_fn);

static void msm_rtb_probe_work_fn(struct work_struct *work)
{
	msm_rtb_probe_memory("RESCAN");
	if (probe_rescan_ms)
		schedule_delayed_work(&msm_rtb_probe_work,
				      msecs_to_jiffies(probe_rescan_ms));
}

static int __init msm_rtb_probe_mem_init(void)
{
	if (!msm_rtb.initialized) {
		pr_warn("MEMPROBE: RTB not initialized, no cross-reboot cursor available - skipping the scan rather than risking an unresumable probe\n");
		return 0;
	}

	if (probe_mem)
		msm_rtb_probe_memory("INIT SCAN");

	if (probe_rescan_ms)
		schedule_delayed_work(&msm_rtb_probe_work,
				      msecs_to_jiffies(probe_rescan_ms));

	return 0;
}
late_initcall(msm_rtb_probe_mem_init);

static int __init msm_rtb_init(void)
{
	return platform_driver_probe(&msm_rtb_driver, msm_rtb_probe);
}

static void __exit msm_rtb_exit(void)
{
	platform_driver_unregister(&msm_rtb_driver);
}
module_init(msm_rtb_init)
module_exit(msm_rtb_exit)
