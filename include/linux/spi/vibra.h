/*
 * Copyright (C) 2010 Nokia Corporation
 *
 * Contact: Ilkka Koskinen <ilkka.koskinen@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef _LINUX_VIBRA_SPI_H
#define _LINUX_VIBRA_SPI_H

struct vibra_spi_platform_data {
	void (*set_power)(bool enable);
};

#endif
