/*
 * vpt.h: Virtual Platform Timer definitions
 *
 * Copyright (c) 2004, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __ASM_X86_HVM_VPT_H__
#define __ASM_X86_HVM_VPT_H__

#include <xen/init.h>
#include <xen/lib.h>
#include <xen/time.h>
#include <xen/errno.h>
#include <xen/time.h>
#include <xen/timer.h>
#include <xen/list.h>
#include <asm/hvm/vpic.h>
#include <asm/hvm/irq.h>
#include <public/hvm/save.h>

/*
 * Abstract layer of periodic time, one short time.
 */
typedef void time_cb(struct vcpu *v, void *opaque);

struct periodic_time {
    struct list_head list;
    bool_t on_list;
    bool_t one_shot;
    bool_t do_not_freeze;
    bool_t irq_issued;
    bool_t warned_timeout_too_short;
#define PTSRC_isa    1 /* ISA time source */
#define PTSRC_lapic  2 /* LAPIC time source */
#define PTSRC_ioapic 3 /* IOAPIC time source */
    u8 source;                  /* PTSRC_ */
    u8 irq;
    struct vcpu *vcpu;          /* vcpu timer interrupt delivers to */
    u32 pending_intr_nr;        /* pending timer interrupts */
    u64 period;                 /* frequency in ns */
    s_time_t scheduled;         /* scheduled timer interrupt */
    u64 last_plt_gtime;         /* platform time when last IRQ is injected */
    struct timer timer;         /* ac_timer */
    time_cb *cb;
    void *priv;                 /* point back to platform time source */
};


#define PIT_FREQ 1193182
#define PIT_BASE 0x40

typedef struct PITState {
    /* Hardware state */
    struct hvm_hw_pit hw;
    /* Last time the counters read zero, for calcuating counter reads */
    int64_t count_load_time[3];
    /* Channel 0 IRQ handling. */
    struct periodic_time pt0;
    spinlock_t lock;
} PITState;

struct hpet_registers {
    /* Memory-mapped, software visible registers */
    uint64_t capability;        /* capabilities */
    uint64_t config;            /* configuration */
    uint64_t isr;               /* interrupt status reg */
    uint64_t mc64;              /* main counter */
    struct {                    /* timers */
        uint64_t config;        /* configuration/cap */
        uint64_t cmp;           /* comparator */
        uint64_t fsb;           /* FSB route, not supported now */
    } timers[HPET_TIMER_NUM];

    /* Hidden register state */
    uint64_t period[HPET_TIMER_NUM]; /* Last value written to comparator */
    uint64_t comparator64[HPET_TIMER_NUM]; /* 64 bit running comparator */
};

typedef struct HPETState {
    struct hpet_registers hpet;
    uint64_t stime_freq;
    uint64_t hpet_to_ns_scale; /* hpet ticks to ns (multiplied by 2^10) */
    uint64_t hpet_to_ns_limit; /* max hpet ticks convertable to ns      */
    uint64_t mc_offset;
    struct periodic_time pt[HPET_TIMER_NUM];
    rwlock_t lock;
} HPETState;

typedef struct RTCState {
    /* Hardware state */
    struct hvm_hw_rtc hw;
    /* RTC's idea of the current time */
    struct tm current_tm;
    /* update-ended timer */
    struct timer update_timer;
    struct timer update_timer2;
    uint64_t next_update_time;
    /* alarm timer */
    struct timer alarm_timer;
    /* periodic timer */
    struct periodic_time pt;
    s_time_t start_time;
    s_time_t check_ticks_since;
    int period;
    uint8_t pt_dead_ticks;
    uint32_t use_timer;
    spinlock_t lock;
} RTCState;

#define FREQUENCE_PMTIMER  3579545  /* Timer should run at 3.579545 MHz */
typedef struct PMTState {
    struct vcpu *vcpu;          /* Keeps sync with this vcpu's guest-time */
    uint64_t last_gtime;        /* Last (guest) time we updated the timer */
    uint32_t not_accounted;     /* time not accounted at last update */
    uint64_t scale;             /* Multiplier to get from tsc to timer ticks */
    struct timer timer;         /* To make sure we send SCIs */
    spinlock_t lock;
} PMTState;

struct pl_time {    /* platform time */
    struct RTCState  vrtc;
    struct HPETState vhpet;
    struct PMTState  vpmt;
    /*
     * rwlock to prevent periodic_time vCPU migration. Take the lock in read
     * mode in order to prevent the vcpu field of periodic_time from changing.
     * Lock must be taken in write mode when changes to the vcpu field are
     * performed, as it allows exclusive access to all the timers of a domain.
     */
    rwlock_t pt_migrate;
    /* guest_time = Xen sys time + stime_offset */
    int64_t stime_offset;
    /* Ensures monotonicity in appropriate timer modes. */
    uint64_t last_guest_time;
    spinlock_t pl_time_lock;
    struct domain *domain;
};

void pt_save_timer(struct vcpu *v);
void pt_restore_timer(struct vcpu *v);
int pt_update_irq(struct vcpu *v);
void pt_intr_post(struct vcpu *v, struct hvm_intack intack);
void pt_migrate(struct vcpu *v);

void pt_adjust_global_vcpu_target(struct vcpu *v);
#define pt_global_vcpu_target(d) \
    (is_hvm_domain(d) && (d)->arch.hvm_domain.i8259_target ? \
     (d)->arch.hvm_domain.i8259_target : \
     (d)->vcpu ? (d)->vcpu[0] : NULL)

void pt_may_unmask_irq(struct domain *d, struct periodic_time *vlapic_pt);

/* Is given periodic timer active? */
#define pt_active(pt) ((pt)->on_list || (pt)->pending_intr_nr)

/*
 * Create/destroy a periodic (or one-shot!) timer.
 * The given periodic timer structure must be initialised with zero bytes,
 * except for the 'source' field which must be initialised with the
 * correct PTSRC_ value. The initialised timer structure can then be passed
 * to {create,destroy}_periodic_time() any number of times and in any order.
 * Note that, for a given periodic timer, invocations of these functions MUST
 * be serialised.
 */
void create_periodic_time(
    struct vcpu *v, struct periodic_time *pt, uint64_t delta,
    uint64_t period, uint8_t irq, time_cb *cb, void *data);
void destroy_periodic_time(struct periodic_time *pt);

int pv_pit_handler(int port, int data, int write);
void pit_reset(struct domain *d);

void pit_init(struct domain *d, unsigned long cpu_khz);
void pit_stop_channel0_irq(PITState * pit);
void pit_deinit(struct domain *d);
void rtc_init(struct domain *d);
void rtc_migrate_timers(struct vcpu *v);
void rtc_deinit(struct domain *d);
void rtc_reset(struct domain *d);
void rtc_update_clock(struct domain *d);

void pmtimer_init(struct vcpu *v);
void pmtimer_deinit(struct domain *d);
void pmtimer_reset(struct domain *d);
int pmtimer_change_ioport(struct domain *d, unsigned int version);

void hpet_init(struct domain *d);
void hpet_deinit(struct domain *d);
void hpet_reset(struct domain *d);

#endif /* __ASM_X86_HVM_VPT_H__ */
