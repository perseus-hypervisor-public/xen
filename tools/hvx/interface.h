/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#ifndef __HVX_VM_INTERFACE_H__
#define __HVX_VM_INTERFACE_H__

#define VMI_VERSION        0
#define VMI_CONSOLE        1
#define VMI_DOMAIN_CONTROL 2
#define VMI_VCPU_CONTROL   3

#define VMI_DOMAIN_CREATE  0
#define VMI_DOMAIN_DESTROY 1
#define VMI_DOMAIN_PAUSE   2
#define VMI_DOMAIN_UNPAUSE 3

#define HVX_API_VERSION_MAJOR	1
#define HVX_API_VERSION_MINOR	0

struct domain_control {
    union {
        unsigned long id;
		struct {
			unsigned long vcpus;
	        unsigned long xlate;
		};
    };
};

#define VMI_VCPU_CREATE  0
#define VMI_VCPU_DESTROY 1

struct vcpu_control {
    unsigned long domain;
	struct {
		unsigned long entry;
		unsigned long affinity;
		unsigned long contextid;
    };
};

#define vmcall(a, b, c, d, e, f) \
    hypercall((unsigned long)(a), \
              (unsigned long)(b), \
              (unsigned long)(c), \
              (unsigned long)(d), \
              (unsigned long)(e), \
              (unsigned long)(f))

int hypercall(long, long, long, long, long, long);
#endif /*!__HVX_VM_INTERFACE_H__*/
