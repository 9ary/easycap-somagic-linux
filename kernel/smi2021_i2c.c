/*******************************************************************************
 * smi2021_i2c.c                                                               *
 *                                                                             *
 * USB Driver for SMI2021 - EasyCAP                                            *
 * USB ID 1c88:003c                                                            *
 *                                                                             *
 * *****************************************************************************
 *
 * Copyright 2011-2013 Jon Arne Jørgensen
 * <jonjon.arnearne--a.t--gmail.com>
 *
 * Copyright 2011, 2012 Tony Brown, Michal Demin, Jeffry Johnston
 *
 * This file is part of SMI2021
 * http://code.google.com/p/easycap-somagic-linux/
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * This driver is heavily influensed by the STK1160 driver.
 * Copyright (C) 2012 Ezequiel Garcia
 * <elezegarcia--a.t--gmail.com>
 *
 */

#include "smi2021.h"

/* The device will not return the chip_name on address 0x00.
 * But the saa7115 i2c driver needs the chip id to match "f7113"
 * if we want to use it,
 * so we have to fake the return of this value
 */

static char chip_id[] = { 'x',255,55,49,49,115,0 };
static int id_ptr = 0;
static unsigned int i2c_debug;
module_param(i2c_debug, int, 0644);
MODULE_PARM_DESC(i2c_debug, "enable debug messages [i2c]");

#define dprint_i2c(fmt, args...)				\
do {								\
	if (i2c_debug)						\
		printk(KERN_DEBUG "smi2021[i2c]::%s: " fmt,	\
			__func__, ##args);			\
} while(0)


static int i2c_xfer(struct i2c_adapter *i2c_adap,
				struct i2c_msg msgs[], int num)
{
	struct smi2021_dev *dev = i2c_adap->algo_data;

	switch(num) {
	case 2: { /* Read reg */
		if (msgs[0].len != 1 || msgs[1].len != 1) {
			dprint_i2c("both messages must be 1 byte\n");
			goto err_out;
	
		if ((msgs[1].flags & I2C_M_RD) != I2C_M_RD)
			dprint_i2c("last message should have rd flag\n");
			goto err_out;
		}

		if (msgs[0].buf[0] == 0) {
			msgs[1].buf[0] = chip_id[id_ptr]; 
			if (chip_id[id_ptr] != 0) {
				id_ptr += 1;
			}
		} else {
			smi2021_read_reg(dev, msgs[0].addr, msgs[0].buf[0], msgs[1].buf);
		}
		break;
	}
	case 1: { /* Write reg */
		if (msgs[0].len == 0) {
			break;
		} else if (msgs[0].len != 2) {
			dprint_i2c("unsupported len\n");
			goto err_out;
		}
		if (msgs[0].buf[0] == 0) {
			/* We don't handle writing to addr 0x00 */
			break;
		}

		smi2021_write_reg(dev, msgs[0].addr, msgs[0].buf[0], msgs[0].buf[1]);
		break;
	}
	default: {
		dprint_i2c("driver can only handle 1 or 2 messages\n");
		goto err_out;
	}
	}
	return num;

err_out:
	return -EOPNOTSUPP;
}

static u32 functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_SMBUS_EMUL;
}

static struct i2c_algorithm algo = {
	.master_xfer = i2c_xfer,
	.functionality = functionality,
};

static struct i2c_adapter adap_template = {
	.owner = THIS_MODULE,
	.name = "smi2021_easycap_dc60",
	.algo = &algo,
};

static struct i2c_client client_template = {
	.name = "smi2021 internal",
};

int smi2021_i2c_register(struct smi2021_dev *dev)
{
	int rc;

	id_ptr = 0;

	dev->i2c_adap = adap_template;
	dev->i2c_adap.dev.parent = dev->dev;
	strcpy(dev->i2c_adap.name, "smi2021");
	dev->i2c_adap.algo_data = dev;

	i2c_set_adapdata(&dev->i2c_adap, &dev->v4l2_dev);

	rc = i2c_add_adapter(&dev->i2c_adap);
	if (rc < 0) {
		smi2021_err("can't add i2c adapter, errno: %d\n", rc);
		return rc;
	}

	dev->i2c_client = client_template;
	dev->i2c_client.adapter = &dev->i2c_adap;

	return 0;
}

int smi2021_i2c_unregister(struct smi2021_dev *dev)
{
	i2c_del_adapter(&dev->i2c_adap);
	return 0;
} 
