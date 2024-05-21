// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * tda1541a.c  --  tda1541a ALSA SoC Audio driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/of_device.h>
#include <linux/kernel.h>
#include <linux/device.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <linux/of.h>

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

static const u32 tda1541a_dai_rates[] = {
	11025, 22050, 44100, 48000, 88200, 96000, 176400, 192000,
};

static const struct snd_pcm_hw_constraint_list dai_constraints = {
	.count = ARRAY_SIZE(tda1541a_dai_rates),
	.list = tda1541a_dai_rates,
};

/* codec private data */
struct tda1541a_private {
	unsigned int rate;
};

static int tda1541a_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	fmt &= (SND_SOC_DAIFMT_FORMAT_MASK | SND_SOC_DAIFMT_INV_MASK |
		SND_SOC_DAIFMT_MASTER_MASK);

	if (fmt != (SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		    SND_SOC_DAIFMT_CBM_CFS)) {
		dev_err(codec_dai->dev, "Invalid DAI format\n");
		return -EINVAL;
	}

	return 0;
}

static int tda1541a_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params,
			      struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct tda1541a_private *priv =
		snd_soc_component_get_drvdata(component);

	dev_dbg(component->dev, "hw_params %u Hz, %u width\n",
		params_rate(params), params_width(params));

	dev_info(component->dev, "hw_params %u Hz, %u width\n",
		 params_rate(params),
		 params_width(params)); // ###

	priv->rate = params_rate(params);

	switch (params_width(params)) {
	case 16:
		break;
	default:
		dev_err(component->dev, "Bad frame size: %d\n",
			params_width(params));
		return -EINVAL;
	}

	return 0;
}

static int tda1541a_startup(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	// struct snd_soc_component *component = dai->component;
	// struct tda1541a_priv *tda1541a = snd_soc_component_get_drvdata(component);
	struct device *dev = dai->dev;

	dev_info(dev, "tda1541a_startup"); // ###
	return snd_pcm_hw_constraint_list(substream->runtime, 0,
					  SNDRV_PCM_HW_PARAM_RATE,
					  &dai_constraints);
}

static const struct snd_soc_dai_ops tda1541a_dai_ops = {
	.startup = tda1541a_startup,
	.set_fmt = tda1541a_set_dai_fmt,
	.hw_params = tda1541a_hw_params,
};

static const struct snd_soc_dapm_widget tda1541a_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("DAC", "Playback", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_OUTPUT("LINEVOUTL"),
	SND_SOC_DAPM_OUTPUT("LINEVOUTR"),
};

static const struct snd_soc_dapm_route tda1541a_dapm_routes[] = {
	{ "LINEVOUTL", NULL, "DAC" },
	{ "LINEVOUTR", NULL, "DAC" },
};

#define TDA1541A_RATES SNDRV_PCM_RATE_8000_192000

// #define TDA1541A_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE)
#define TDA1541A_FORMATS (SNDRV_PCM_FMTBIT_S16_LE)

static struct snd_soc_dai_driver tda1541a_dai = {
	.name = "tda1541a-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = TDA1541A_RATES,
		// .rates = SNDRV_PCM_RATE_CONTINUOUS,
		.rate_min = 10000,
		.rate_max = 200000,
		.formats = TDA1541A_FORMATS,
	},
	.ops = &tda1541a_dai_ops,
};

static const struct snd_soc_component_driver soc_component_dev_tda1541a = {
	.dapm_widgets = tda1541a_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(tda1541a_dapm_widgets),
	.dapm_routes = tda1541a_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(tda1541a_dapm_routes),
	.idle_bias_on = 1,
	.use_pmdown_time = 1,
	.endianness = 1,
};

static int tda1541a_probe(struct platform_device *pdev)
{
	struct tda1541a_private *tda1541a;

	tda1541a = devm_kzalloc(&pdev->dev, sizeof(struct tda1541a_private),
				GFP_KERNEL);
	if (!tda1541a)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, tda1541a);

	return devm_snd_soc_register_component(
		&pdev->dev, &soc_component_dev_tda1541a, &tda1541a_dai, 1);
}

static int tda1541a_remove(struct platform_device *pdev)
{
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id tda1541a_of_match[] = {
	{
		.compatible = "phillips,tda1541a",
	},
	{
		.compatible = "phillips,tda1541",
	},
	{}
};
MODULE_DEVICE_TABLE(of, tda1541a_of_match);
#endif

static struct platform_driver tda1541a_codec_driver = {
	.driver = {
		.name = "tda1541a-codec",
		.of_match_table = of_match_ptr(tda1541a_of_match),
	},

	.probe = tda1541a_probe,
	.remove = tda1541a_remove,
};

module_platform_driver(tda1541a_codec_driver);

MODULE_DESCRIPTION("ASoC tda1541a driver");
MODULE_AUTHOR("Eugene Aleynikiov <beinguid0@gmail.com>");
MODULE_ALIAS("platform:tda1541a-codec");
MODULE_LICENSE("GPL");
