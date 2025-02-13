/******************************************************************************
 * event.h
 * 
 * A nice interface for passing asynchronous events to guest OSes.
 * 
 * Copyright (c) 2002-2006, K A Fraser
 */

#ifndef __XEN_EVENT_H__
#define __XEN_EVENT_H__

#include <xen/sched.h>
#include <xen/smp.h>
#include <xen/softirq.h>
#include <xen/bitops.h>
#include <asm/event.h>

/*
 * send_guest_vcpu_virq: Notify guest via a per-VCPU VIRQ.
 *  @v:        VCPU to which virtual IRQ should be sent
 *  @virq:     Virtual IRQ number (VIRQ_*)
 */
void send_guest_vcpu_virq(struct vcpu *v, uint32_t virq);

/*
 * send_global_virq: Notify the domain handling a global VIRQ.
 *  @virq:     Virtual IRQ number (VIRQ_*)
 */
void send_global_virq(uint32_t virq);

/*
 * sent_global_virq_handler: Set a global VIRQ handler.
 *  @d:        New target domain for this VIRQ
 *  @virq:     Virtual IRQ number (VIRQ_*), must be global
 */
int set_global_virq_handler(struct domain *d, uint32_t virq);

/*
 * send_guest_pirq:
 *  @d:        Domain to which physical IRQ should be sent
 *  @pirq:     Physical IRQ number
 */
void send_guest_pirq(struct domain *, const struct pirq *);

/* Send a notification from a given domain's event-channel port. */
int evtchn_send(struct domain *d, unsigned int lport);

/* Bind a local event-channel port to the specified VCPU. */
long evtchn_bind_vcpu(unsigned int port, unsigned int vcpu_id);

/* Bind a VIRQ. */
int evtchn_bind_virq(evtchn_bind_virq_t *bind, evtchn_port_t port);

/* Get the status of an event channel port. */
int evtchn_status(evtchn_status_t *status);

/* Close an event channel. */
int evtchn_close(struct domain *d1, int port1, bool guest);

/* Free an event channel. */
void evtchn_free(struct domain *d, struct evtchn *chn);

/* Allocate a specific event channel port. */
int evtchn_allocate_port(struct domain *d, unsigned int port);

/* Unmask a local event-channel port. */
int evtchn_unmask(unsigned int port);

/* Move all PIRQs after a vCPU was moved to another pCPU. */
void evtchn_move_pirqs(struct vcpu *v);

/* Allocate/free a Xen-attached event channel port. */
typedef void (*xen_event_channel_notification_t)(
    struct vcpu *v, unsigned int port);
int alloc_unbound_xen_event_channel(
    struct domain *ld, unsigned int lvcpu, domid_t remote_domid,
    xen_event_channel_notification_t notification_fn);
void free_xen_event_channel(struct domain *d, int port);

/* Query if event channel is in use by the guest */
int guest_enabled_event(struct vcpu *v, uint32_t virq);

/* Notify remote end of a Xen-attached event channel.*/
void notify_via_xen_event_channel(struct domain *ld, int lport);

/*
 * Internal event channel object storage.
 *
 * The objects (struct evtchn) are indexed using a two level scheme of
 * groups and buckets.  Each group is a page of bucket pointers.  Each
 * bucket is a page-sized array of struct evtchn's.
 *
 * The first bucket is directly accessed via d->evtchn.
 */
#define group_from_port(d, p) \
    ((d)->evtchn_group[(p) / EVTCHNS_PER_GROUP])
#define bucket_from_port(d, p) \
    ((group_from_port(d, p))[((p) % EVTCHNS_PER_GROUP) / EVTCHNS_PER_BUCKET])

static inline unsigned int max_evtchns(const struct domain *d)
{
    return d->evtchn_fifo ? EVTCHN_FIFO_NR_CHANNELS
                          : BITS_PER_EVTCHN_WORD(d) * BITS_PER_EVTCHN_WORD(d);
}

static inline void evtchn_read_lock(struct evtchn *evtchn)
{
    read_lock(&evtchn->lock);
}

static inline bool evtchn_read_trylock(struct evtchn *evtchn)
{
    return read_trylock(&evtchn->lock);
}

static inline void evtchn_read_unlock(struct evtchn *evtchn)
{
    read_unlock(&evtchn->lock);
}

static inline bool_t port_is_valid(struct domain *d, unsigned int p)
{
    if ( p >= read_atomic(&d->valid_evtchns) )
        return false;

    /*
     * The caller will usually access the event channel afterwards and
     * may be done without taking the per-domain lock. The barrier is
     * going in pair the smp_wmb() barrier in evtchn_allocate_port().
     */
    smp_rmb();

    return true;
}

static inline struct evtchn *evtchn_from_port(struct domain *d, unsigned int p)
{
    if ( p < EVTCHNS_PER_BUCKET )
        return &d->evtchn[p];
    return bucket_from_port(d, p) + (p % EVTCHNS_PER_BUCKET);
}

/*
 * "usable" as in "by a guest", i.e. Xen consumed channels are assumed to be
 * taken care of separately where used for Xen's internal purposes.
 */
static bool evtchn_usable(const struct evtchn *evtchn)
{
    if ( evtchn->xen_consumer )
        return false;

#ifdef arch_evtchn_is_special
    if ( arch_evtchn_is_special(evtchn) )
        return true;
#endif

    BUILD_BUG_ON(ECS_FREE > ECS_RESERVED);
    return evtchn->state > ECS_RESERVED;
}

/* Wait on a Xen-attached event channel. */
#define wait_on_xen_event_channel(port, condition)                      \
    do {                                                                \
        if ( condition )                                                \
            break;                                                      \
        set_bit(_VPF_blocked_in_xen, &current->pause_flags);            \
        smp_mb(); /* set blocked status /then/ re-evaluate condition */ \
        if ( condition )                                                \
        {                                                               \
            clear_bit(_VPF_blocked_in_xen, &current->pause_flags);      \
            break;                                                      \
        }                                                               \
        raise_softirq(SCHEDULE_SOFTIRQ);                                \
        do_softirq();                                                   \
    } while ( 0 )

#define prepare_wait_on_xen_event_channel(port)                         \
    do {                                                                \
        set_bit(_VPF_blocked_in_xen, &current->pause_flags);            \
        raise_softirq(SCHEDULE_SOFTIRQ);                                \
        smp_mb(); /* set blocked status /then/ caller does his work */  \
    } while ( 0 )

void evtchn_check_pollers(struct domain *d, unsigned int port);

void evtchn_2l_init(struct domain *d);

/* Close all event channels and reset to 2-level ABI. */
int evtchn_reset(struct domain *d, bool resuming);

/*
 * Low-level event channel port ops.
 *
 * All hooks have to be called with a lock held which prevents the channel
 * from changing state. This may be the domain event lock, the per-channel
 * lock, or in the case of sending interdomain events also the other side's
 * per-channel lock. Exceptions apply in certain cases for the PV shim.
 */
struct evtchn_port_ops {
    void (*init)(struct domain *d, struct evtchn *evtchn);
    void (*set_pending)(struct vcpu *v, struct evtchn *evtchn);
    void (*clear_pending)(struct domain *d, struct evtchn *evtchn);
    void (*unmask)(struct domain *d, struct evtchn *evtchn);
    bool (*is_pending)(const struct domain *d, const struct evtchn *evtchn);
    bool (*is_masked)(const struct domain *d, const struct evtchn *evtchn);
    /*
     * Is the port unavailable because it's still being cleaned up
     * after being closed?
     */
    bool (*is_busy)(const struct domain *d, const struct evtchn *evtchn);
    int (*set_priority)(struct domain *d, struct evtchn *evtchn,
                        unsigned int priority);
    void (*print_state)(struct domain *d, const struct evtchn *evtchn);
};

static inline void evtchn_port_init(struct domain *d, struct evtchn *evtchn)
{
    if ( d->evtchn_port_ops->init )
        d->evtchn_port_ops->init(d, evtchn);
}

static inline void evtchn_port_set_pending(struct domain *d,
                                           unsigned int vcpu_id,
                                           struct evtchn *evtchn)
{
    if ( evtchn_usable(evtchn) )
        d->evtchn_port_ops->set_pending(d->vcpu[vcpu_id], evtchn);
}

static inline void evtchn_port_clear_pending(struct domain *d,
                                             struct evtchn *evtchn)
{
    if ( evtchn_usable(evtchn) )
        d->evtchn_port_ops->clear_pending(d, evtchn);
}

static inline void evtchn_port_unmask(struct domain *d,
                                      struct evtchn *evtchn)
{
    if ( evtchn_usable(evtchn) )
        d->evtchn_port_ops->unmask(d, evtchn);
}

static inline bool evtchn_is_pending(const struct domain *d,
                                     const struct evtchn *evtchn)
{
    return evtchn_usable(evtchn) && d->evtchn_port_ops->is_pending(d, evtchn);
}

static inline bool evtchn_port_is_pending(struct domain *d, evtchn_port_t port)
{
    struct evtchn *evtchn = evtchn_from_port(d, port);
    bool rc;

    evtchn_read_lock(evtchn);
    rc = evtchn_is_pending(d, evtchn);
    evtchn_read_unlock(evtchn);

    return rc;
}

static inline bool evtchn_is_masked(const struct domain *d,
                                    const struct evtchn *evtchn)
{
    return !evtchn_usable(evtchn) || d->evtchn_port_ops->is_masked(d, evtchn);
}

static inline bool evtchn_port_is_masked(struct domain *d, evtchn_port_t port)
{
    struct evtchn *evtchn = evtchn_from_port(d, port);
    bool rc;

    evtchn_read_lock(evtchn);

    rc = evtchn_is_masked(d, evtchn);

    evtchn_read_unlock(evtchn);

    return rc;
}

static inline bool evtchn_is_busy(const struct domain *d,
                                  const struct evtchn *evtchn)
{
    return d->evtchn_port_ops->is_busy &&
           d->evtchn_port_ops->is_busy(d, evtchn);
}

static inline int evtchn_port_set_priority(struct domain *d,
                                           struct evtchn *evtchn,
                                           unsigned int priority)
{
    if ( !d->evtchn_port_ops->set_priority )
        return -ENOSYS;
    if ( !evtchn_usable(evtchn) )
        return -EACCES;
    return d->evtchn_port_ops->set_priority(d, evtchn, priority);
}

static inline void evtchn_port_print_state(struct domain *d,
                                           const struct evtchn *evtchn)
{
    d->evtchn_port_ops->print_state(d, evtchn);
}

#endif /* __XEN_EVENT_H__ */
