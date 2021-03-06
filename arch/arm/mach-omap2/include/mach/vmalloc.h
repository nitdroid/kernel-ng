/*
 *  arch/arm/plat-omap/include/mach/vmalloc.h
 *
 *  Copyright (C) 2000 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifdef CONFIG_VMSPLIT_2G
/* HACK: place vmalloc space after 1G of physical ram */
#define VMALLOC_START	  (PAGE_OFFSET + 0x40000000)
#define VMALLOC_END	  (PAGE_OFFSET + 0x78000000)
#elif defined(CONFIG_VMSPLIT_3G_OPT)
/* HACK: place vmalloc space after 1G of physical ram */
#define VMALLOC_START	  (PAGE_OFFSET + 0x40000000)
#define VMALLOC_END	  (PAGE_OFFSET + 0x48000000)
#else
/* retain old behavior for 3G split */
#define VMALLOC_END	  (PAGE_OFFSET + 0x38000000)
#endif
