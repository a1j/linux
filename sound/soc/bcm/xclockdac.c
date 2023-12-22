// SPDX-License-Identifier: GPL-2.0
/*
 * ASoC Driver for external clock TDA1541A DAC
 *
 * Author:	Eugene Aleynikov <beinguid0@gmail.com>
 *		Copyright 2020
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

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>

#include <linux/i2c.h>

#include "../codecs/pcm179x.h"

static const unsigned int xclockdac_rates[] = {
	11025, 22050, 44100, 48000, 88200, 96000, 176400, 192000,
};

#define DEFAULT_RATE 44100

struct xclockdac_drv_data {
	struct clk *sclk;
};

static struct xclockdac_drv_data drvdata;

static struct snd_pcm_hw_constraint_list xclockdac_constraints = {
	.list = xclockdac_rates,
	.count = ARRAY_SIZE(xclockdac_rates),
};

static int snd_rpi_xclockdac_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;

	dev_warn(card->dev, "snd_rpi_xclockdac_startup"); // ###

	/* constraints for standard sample rates */
	snd_pcm_hw_constraint_list(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_RATE,
				   &xclockdac_constraints);
	return 0;
}

static void snd_rpi_xclockdac_set_sclk(struct snd_soc_component *component,
				      int sample_rate)
{
	if (!IS_ERR(drvdata.sclk))
		clk_set_rate(drvdata.sclk, sample_rate);
}

static int snd_rpi_xclockdac_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai_link *dai = rtd->dai_link;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);

	dai->name = "XclockDAC TDA1541A";
	dai->stream_name = "XclockDAC TDA1541A";
	dai->dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		       SND_SOC_DAIFMT_CBM_CFM;

	/* allow only fixed 16 clock counts per channel */
	snd_soc_dai_set_bclk_ratio(cpu_dai, 16 * 2);

	return 0;
}

static int snd_rpi_xclockdac_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *params)
{
	int ret = 0;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	struct snd_soc_component *component =
		asoc_rtd_to_codec(rtd, 0)->component;

	snd_rpi_xclockdac_set_sclk(component, params_rate(params));
	return ret;
}

/* machine stream operations */
static struct snd_soc_ops snd_rpi_xclockdac_ops = {
	.hw_params = snd_rpi_xclockdac_hw_params,
	.startup = snd_rpi_xclockdac_startup,
};

SND_SOC_DAILINK_DEFS(
	hifi, DAILINK_COMP_ARRAY(COMP_CPU("bcm2708-i2s.0")),
	DAILINK_COMP_ARRAY(COMP_CODEC("tda1541a-codec", "tda1541a-hifi")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("bcm2708-i2s.0")));

static struct snd_soc_dai_link snd_rpi_xclockdac_dai[] = {
	{
		.name = "XclockDAC TDA1541A",
		.stream_name = "XclockDAC TDA1541A",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBS_CFS,
		.ops = &snd_rpi_xclockdac_ops,
		.init = snd_rpi_xclockdac_init,
		SND_SOC_DAILINK_REG(hifi),
	},
};

/* audio machine driver */
static struct snd_soc_card snd_rpi_xclockdac = {
	.name = "snd_rpi_xclockdac",
	.driver_name = "XClockDAC",
	.owner = THIS_MODULE,
	.dai_link = snd_rpi_xclockdac_dai,
	.num_links = ARRAY_SIZE(snd_rpi_xclockdac_dai),
};

static int snd_rpi_xclockdac_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;
	struct device_node *dev_node = dev->of_node;

	snd_rpi_xclockdac.dev = &pdev->dev;

	dev_warn(&pdev->dev, "snd_rpi_xclockdac_probe"); // ###
	if (pdev->dev.of_node) {
		struct device_node *i2s_node;
		struct snd_soc_dai_link *dai;

		dai = &snd_rpi_xclockdac_dai[0];
		i2s_node = of_parse_phandle(pdev->dev.of_node, "i2s-controller",
					    0);
		dev_warn(&pdev->dev, "snd_rpi_xclockdac_probe:of_node"); // ###

		if (i2s_node) {
			dai->cpus->of_node = i2s_node;
			dai->platforms->of_node = i2s_node;
			dai->cpus->dai_name = NULL;
			dai->platforms->name = NULL;
			dev_warn(
				&pdev->dev,
				"snd_rpi_xclockdac_probe:of_node:i2s_node"); // ###
		} else {
			return -EPROBE_DEFER;
		}
	}

	ret = devm_snd_soc_register_card(&pdev->dev, &snd_rpi_xclockdac);
	if (ret && ret != -EPROBE_DEFER) {
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
			ret);
		return ret;
	}
	if (ret == -EPROBE_DEFER)
		return ret;

	dev_set_drvdata(dev, &drvdata);
	if (dev_node == NULL) {
		dev_err(&pdev->dev, "Device tree node not found\n");
		return -ENODEV;
	}

	drvdata.sclk = devm_clk_get(dev, NULL);
	if (IS_ERR(drvdata.sclk)) {
		drvdata.sclk = ERR_PTR(-ENOENT);
		return -ENODEV;
	}

	clk_set_rate(drvdata.sclk, DEFAULT_RATE);

	return ret;
}

static const struct of_device_id snd_rpi_xclockdac_of_match[] = {
	{
		.compatible = "xclockdac,xclockdac",
	},
	{},
};
MODULE_DEVICE_TABLE(of, snd_rpi_xclockdac_of_match);

static struct platform_driver snd_rpi_xclockdac_driver = {
	.driver = {
		.name   = "snd-rpi-xclockdac",
		.owner  = THIS_MODULE,
		.of_match_table = snd_rpi_xclockdac_of_match,
	},
	.probe          = snd_rpi_xclockdac_probe,
};

module_platform_driver(snd_rpi_xclockdac_driver);

MODULE_AUTHOR("Eugene Aleynikov <beinguid0@gmail.com>");
MODULE_DESCRIPTION("ASoC Driver for External TDA1541A DAC");
MODULE_LICENSE("GPL v2");
