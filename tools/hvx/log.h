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
#ifndef __HVX_LOGGER_H__
#define __HVX_LOGGER_H__

#include <linux/printk.h>

#define LOG_TAG "hvx: "

#define fmt(f) f

#define hvx_error(f, ...) \
	printk(KERN_ERR LOG_TAG fmt(f), ##__VA_ARGS__)

#define hvx_warn(f, ...) \
	printk(KERN_WARNING LOG_TAG fmt(f), ##__VA_ARGS__)

#define hvx_info(f, ...) \
	printk(KERN_INFO LOG_TAG fmt(f), ##__VA_ARGS__)

#define hvx_debug(f, ...) \
	printk(KERN_DEBUG LOG_TAG fmt(f), ##__VA_ARGS__)

#endif  /*! __HVX_LOGGER_H__ */
