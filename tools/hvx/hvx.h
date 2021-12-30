#ifndef __HVX_H__
#define __HVX_H__

#include <linux/types.h>
#include <linux/ioctl.h>

#ifndef __user
#define __user
#endif

#define HVX_IOCTL_HYPERCALL			\
	_IOC(_IOC_NONE, 'P', 0, sizeof(struct hvx_proto_hypercall))
struct hvx_proto_hypercall {
	__u64 op;
	__u64 params[5];
};

#define HVX_IOCTL_DOMAIN_CREATE	\
	_IOC(_IOC_NONE, 'P', 1, sizeof(struct hvx_proto_domain_create))
struct hvx_proto_domain_create {
	__u64 vcpus;
	__u64 memory;
};

#define HVX_IOCTL_DOMAIN_DESTROY \
	_IOC(_IOC_NONE, 'P', 2, sizeof(struct hvx_proto_domain_destroy))
struct hvx_proto_domain_destroy {
	__u64 id;
	__u64 force;
};

#define HVX_IOCTL_VCPU_CONTEXT	\
	_IOC(_IOC_NONE, 'P', 3, sizeof(struct hvx_proto_vcpu_context))
struct hvx_proto_vcpu_context {
	__u64 entry;
	__u64 affinity;
	__u64 contextid;
};

#define HVX_IOCTL_MEMORY	\
	_IOC(_IOC_NONE, 'P', 4, sizeof(struct hvx_proto_memory))
struct hvx_proto_memory {
	__u64 type;
	__u64 base;
	__u64 size;
};

#endif /*!__HVX_H__*/
