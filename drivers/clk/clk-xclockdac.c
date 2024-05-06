// SPDX-License-Identifier: GPL-2.0
/*
 * Clock Driver for XclockDAC
 *
 * Author: Eugene Aleynikov
 *         Copyright 2023
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/err.h>
#include <linux/errno.h>

#define DEFAULT_RATE 44100
#define SMBUS_DAC_SET 0x2F // set clock command

struct xclockdac_rate {
	unsigned long out;
	uint8_t reg_value;
};

/* PCM RATES for 16 bits
 * RATE  XTL D  BITRATE
   11025 11M 32 352800
   12000 12M 32 384000
   22050 11M 16 705600
   24000 12M 16 768000
   44100 11M 8 1411200
   48000 12M 8 1536000
   88200 11M 4 2822400
   96000 12M 4 3072000
   176400 11M 2 5644800
   192000 12M 2 6144000
 * 
 * Bitrate doubles for 32bit

 * Xtal1 = 11289600
 * Xtal2 = 12288000
 * 
 Standard PCM rates are
 * 5512, 8000, 11025, 12000, 16000, 22050,
 * 24000, 32000, 44100, 48000, 64000, 88200,
 * 6000, 176400, 192000, 352800, 384000
 * 
 * We ignore rates that do not have ^2 division.
 */
/*
 * Ordered by frequency. For frequency the hardware can generate with
 * multiple settings, the one with lowest jitter is listed first.
 */
static const struct xclockdac_rate xclockdac_rates[] = {
	{ 11025, 0b00000100 },	{ 22050, 0b00001100 },	{ 44100, 0b00000011 },
	{ 48000, 0b00001011 },	{ 88200, 0b00000010 },	{ 96000, 0b00001010 },
	{ 176400, 0b00000001 }, { 192000, 0b00001001 }, {} /* sentinel */
};

/**
 * struct clk_xclockdac_drvdata - Common struct to the XclockDAC i2c interface
 * @hw: clk_hw for the common clk framework
 */
struct clk_xclockdac_drvdata {
	struct regmap *regmap;
	struct clk *clk;
	struct i2c_client *client;
	struct clk_hw hw;
	unsigned int reg_value;
};

#define to_xclockdac_clk(_hw) \
	container_of(_hw, struct clk_xclockdac_drvdata, hw)

static int xclockdac_write_reg(struct clk_xclockdac_drvdata *drvdata,
			       uint8_t value)
{
	int ret;

	dev_dbg(&drvdata->client->dev, "updating value 0x%02x -> 0x%02x\n",
		drvdata->reg_value, value);

	drvdata->reg_value = value;

	ret = regmap_write(drvdata->regmap, SMBUS_DAC_SET, value);

	if (ret < 0) {
		dev_warn(&drvdata->client->dev, "Unable to regmap_write device register, code: %d\n", ret);
	}
	return ret < 0 ? ret : 0;
}

static int xclockdac_set_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long parent_rate)
{
	struct clk_xclockdac_drvdata *drvdata = to_xclockdac_clk(hw);
	const struct xclockdac_rate *entry;

	// actual_rate = (unsigned long)clk_xclockdac_round_rate(hw, rate,
	// 	&parent_rate);

	for (entry = xclockdac_rates; entry->out != 0; entry++)
		if (entry->out == rate)
			break;

	if (entry->out == 0)
		return -EINVAL;

	return xclockdac_write_reg(drvdata, entry->reg_value);
}

static unsigned long xclockdac_recalc_rate(struct clk_hw *hw,
					   unsigned long parent_rate)
{
	struct clk_xclockdac_drvdata *drvdata = to_xclockdac_clk(hw);
	uint8_t val = drvdata->reg_value;
	const struct xclockdac_rate *entry;

	for (entry = xclockdac_rates; entry->out != 0; entry++)
		if (val == entry->reg_value)
			return entry->out;

	return 0;
}

static long xclockdac_round_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long *parent_rate)
{
	const struct xclockdac_rate *curr, *prev = NULL;

	for (curr = xclockdac_rates; curr->out != 0; curr++) {
		/* Exact matches */
		if (curr->out == rate)
			return rate;

		/*
		 * Find the first entry that has a frequency higher than the
		 * requested one.
		 */
		if (curr->out > rate) {
			unsigned int mid;

			/*
			 * If this is the first entry, clamp the value to the
			 * lowest possible frequency.
			 */
			if (!prev)
				return curr->out;

			/*
			 * Otherwise, determine whether the previous entry or
			 * current one is closer.
			 */
			mid = prev->out + ((curr->out - prev->out) / 2);

			return (mid > rate) ? prev->out : curr->out;
		}

		prev = curr;
	}

	/* If the last entry was still too high, clamp the value */
	return prev->out;
}

const struct clk_ops clk_xclockdac_rate_ops = {
	.recalc_rate = xclockdac_recalc_rate,
	.round_rate = xclockdac_round_rate,
	.set_rate = xclockdac_set_rate,
};

const struct regmap_config xclockdac_clk_regmap = {
        .reg_bits = 8,
        .val_bits = 8,
        .max_register = SMBUS_DAC_SET,
};
EXPORT_SYMBOL_GPL(xclockdac_clk_regmap);

static int xclockdac_i2c_probe(struct i2c_client *client,
						const struct i2c_device_id *id)
{
	struct clk_xclockdac_drvdata *drvdata;
	struct device *dev = &client->dev;
	struct device_node *dev_node = dev->of_node;
	struct regmap_config config = xclockdac_clk_regmap;
	struct clk_init_data init;
	int ret = 0;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->clk = devm_clk_get(dev, "clk");
	if (IS_ERR(drvdata->clk))
		return PTR_ERR(drvdata->clk);

	i2c_set_clientdata(client, drvdata);

	drvdata->regmap = devm_regmap_init_i2c(client, &config);

	if (IS_ERR(drvdata->regmap))
        return PTR_ERR(drvdata->regmap);

	drvdata->client = client;

	ret = regmap_read(drvdata->regmap, SMBUS_DAC_SET, &drvdata->reg_value);
	if (ret < 0) {
		dev_warn(dev, "Unable to regmap_read device register, code: %d\n", ret);
		return ret;
	}

	init.name = "clk-xclockdac";
	init.ops = &clk_xclockdac_rate_ops;
	init.flags = 0;
	init.parent_names = NULL;
	init.num_parents = 0;

	drvdata->hw.init = &init;

	drvdata->clk = devm_clk_register(dev, &drvdata->hw);
	if (IS_ERR(drvdata->clk)) {
		dev_err(dev, "unable to register %s\n", init.name);
		return PTR_ERR(drvdata->clk);
	}

	ret = of_clk_add_provider(dev_node, of_clk_src_simple_get,
				  drvdata->clk);
	if (ret != 0) {
		dev_err(dev, "Cannot of_clk_add_provider");
		return ret;
	}

	ret = clk_set_rate(drvdata->hw.clk, DEFAULT_RATE);
	if (ret != 0) {
		dev_err(dev, "Cannot set rate : %d\n", ret);
		return -EINVAL;
	}
	return ret;
}

static int clk_xclockdac_remove(struct device *dev)
{
	of_clk_del_provider(dev->of_node);
	return 0;
}

static void clk_xclockdac_i2c_remove(struct i2c_client *i2c)
{
	clk_xclockdac_remove(&i2c->dev);
}

static const struct i2c_device_id clk_xclockdac_i2c_ids[] = {
	{
		.name = "xclockdac-clk",
	},
	{}
};
MODULE_DEVICE_TABLE(i2c, clk_xclockdac_i2c_ids);

static const struct of_device_id clk_xclockdac_dt_ids[] = {
	{
		.compatible = "xclockdac,xclockdac-clk",
	},
	{}
};
MODULE_DEVICE_TABLE(of, clk_xclockdac_dt_ids);

static struct i2c_driver clk_xclockdac_i2c_driver = {
	.driver = {
		.name		= "xclockdac-clk",
		.of_match_table	= clk_xclockdac_dt_ids,
	},
	.probe = xclockdac_i2c_probe,
	.remove = clk_xclockdac_i2c_remove,
	.id_table = clk_xclockdac_i2c_ids,
};
module_i2c_driver(clk_xclockdac_i2c_driver);

MODULE_AUTHOR("Eugene Aleynikov <a1j@github.com>");
MODULE_DESCRIPTION("xclockdac Programmable Audio Clock Generator");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:clk-xclockdac");
