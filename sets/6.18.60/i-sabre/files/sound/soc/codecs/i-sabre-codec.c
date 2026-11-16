/*
 * Driver for I-Sabre Q2M
 *
 * Author: Satoru Kawase
 * Modified by: Xiao Qingyong
 * Modified by: JC BARBAUD (Mute)
 * Update kernel v4.18+ by : Audiophonics
 * 		Copyright 2018 Audiophonics
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


#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/i2c.h>
#include <linux/workqueue.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>

#include "i-sabre-codec.h"


/*
 * The device behind these registers is a microcontroller. It does not
 * acknowledge every transfer, least of all while the audio clocks are
 * stopping or starting, and it has been seen to acknowledge a write and not
 * take it. A transfer is tried this many times, this far apart.
 */
#define ISABRECODEC_XFER_TRIES		5
#define ISABRECODEC_XFER_PAUSE_MS	40

/* I-Sabre Q2M Codec Private Data */
struct i_sabre_codec_priv {
	struct regmap *regmap;
	unsigned int fmt;
	/* Drops the register cache after a write the device never took. */
	struct work_struct forget;
	/*
	 * The device has one mute. The listener's and the stream's are kept
	 * apart here, and the device is muted while either asks for it.
	 */
	struct mutex mute_lock;
	bool user_mute;
	bool stream_mute;
};


/*
 * I-Sabre Q2M Codec Default Register Value
 *
 * Only for the register the driver alone writes. The volume, the mute, the
 * filter and the input are the device's own to hold: it keeps them over a
 * restart of the host and can be set from its front panel, so they are
 * read from it the first time they are asked for, not assumed.
 */
static const struct reg_default i_sabre_codec_reg_defaults[] = {
	{ ISABRECODEC_REG_10, 0x00 },
};


static bool i_sabre_codec_writeable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ISABRECODEC_REG_10:
	case ISABRECODEC_REG_20:
	case ISABRECODEC_REG_21:
	case ISABRECODEC_REG_22:
	case ISABRECODEC_REG_24:
		return true;

	default:
		return false;
	}
}

static bool i_sabre_codec_readable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ISABRECODEC_REG_01:
	case ISABRECODEC_REG_02:
	case ISABRECODEC_REG_10:
	case ISABRECODEC_REG_20:
	case ISABRECODEC_REG_21:
	case ISABRECODEC_REG_22:
	case ISABRECODEC_REG_24:
		return true;

	default:
		return false;
	}
}

static bool i_sabre_codec_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ISABRECODEC_REG_01:
	case ISABRECODEC_REG_02:
		return true;

	default:
		return false;
	}
}


/* The registers the device gives back as they were written. */
static bool i_sabre_codec_reads_back(unsigned int reg)
{
	switch (reg) {
	case ISABRECODEC_REG_20:
	case ISABRECODEC_REG_21:
	case ISABRECODEC_REG_22:
		return true;

	default:
		return false;
	}
}

static int i_sabre_codec_reg_read(void *context, unsigned int reg,
				  unsigned int *val)
{
	struct i2c_client *i2c = context;
	int ret, try;

	for (try = 1; ; try++) {
		ret = i2c_smbus_read_byte_data(i2c, reg);
		if (ret >= 0) {
			*val = ret;
			return 0;
		}
		if (try == ISABRECODEC_XFER_TRIES)
			return ret;
		msleep(ISABRECODEC_XFER_PAUSE_MS);
	}
}

static int i_sabre_codec_reg_write(void *context, unsigned int reg,
				   unsigned int val)
{
	struct i2c_client *i2c = context;
	struct i_sabre_codec_priv *i_sabre_codec = i2c_get_clientdata(i2c);
	int ret, try;

	for (try = 1; ; try++) {
		ret = i2c_smbus_write_byte_data(i2c, reg, val);
		if (!ret && i_sabre_codec_reads_back(reg)) {
			/* Acknowledged is not taken: ask for it back. */
			ret = i2c_smbus_read_byte_data(i2c, reg);
			if (ret >= 0)
				ret = (unsigned int)ret == val ? 0 : -EIO;
		}
		if (!ret)
			return 0;
		if (try == ISABRECODEC_XFER_TRIES)
			break;
		msleep(ISABRECODEC_XFER_PAUSE_MS);
	}

	dev_err(&i2c->dev, "register 0x%02x not written in %d tries: %d\n",
		reg, try, ret);

	/*
	 * The cache was given the value before the device was. Left there,
	 * it would have a later write of the same value skipped, and the
	 * device would never get it.
	 */
	if (i_sabre_codec)
		schedule_work(&i_sabre_codec->forget);

	return ret;
}

static void i_sabre_codec_forget(struct work_struct *work)
{
	struct i_sabre_codec_priv *i_sabre_codec =
		container_of(work, struct i_sabre_codec_priv, forget);

	regcache_drop_region(i_sabre_codec->regmap, ISABRECODEC_REG_10,
			     ISABRECODEC_MAX_REG);
}

/* Volume Scale */
static const DECLARE_TLV_DB_SCALE(volume_tlv, -10000, 100, 0);


/* Filter Type */
static const char * const fir_filter_type_texts[] = {
	"brick wall",
	"corrected minimum phase fast",
	"minimum phase slow",
	"minimum phase fast",
	"linear phase slow",
	"linear phase fast",
	"apodizing fast",
};

static SOC_ENUM_SINGLE_DECL(i_sabre_fir_filter_type_enum,
				ISABRECODEC_REG_22, 0, fir_filter_type_texts);


/* I2S / SPDIF Select */
static const char * const iis_spdif_sel_texts[] = {
	"I2S",
	"SPDIF",
};

static SOC_ENUM_SINGLE_DECL(i_sabre_iis_spdif_sel_enum,
				ISABRECODEC_REG_24, 0, iis_spdif_sel_texts);


/* Mute: called with mute_lock held. */
static int i_sabre_codec_set_mute(struct snd_soc_component *component)
{
	struct i_sabre_codec_priv *i_sabre_codec =
		snd_soc_component_get_drvdata(component);
	bool mute = i_sabre_codec->user_mute || i_sabre_codec->stream_mute;

	return snd_soc_component_update_bits(component, ISABRECODEC_REG_21,
					     0x01, mute ? 0x01 : 0x00);
}

static int i_sabre_codec_switch_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct i_sabre_codec_priv *i_sabre_codec =
		snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = !i_sabre_codec->user_mute;

	return 0;
}

static int i_sabre_codec_switch_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct i_sabre_codec_priv *i_sabre_codec =
		snd_soc_component_get_drvdata(component);
	bool mute = !ucontrol->value.integer.value[0];
	int ret = 0;

	mutex_lock(&i_sabre_codec->mute_lock);
	if (i_sabre_codec->user_mute != mute) {
		i_sabre_codec->user_mute = mute;
		ret = i_sabre_codec_set_mute(component);
		if (ret >= 0)
			ret = 1;
	}
	mutex_unlock(&i_sabre_codec->mute_lock);

	return ret;
}

/* Control */
static const struct snd_kcontrol_new i_sabre_codec_controls[] = {
SOC_SINGLE_RANGE_TLV("Digital Playback Volume", ISABRECODEC_REG_20, 0, 0, 100, 1, volume_tlv),
SOC_SINGLE_BOOL_EXT("Digital Playback Switch", 0,
		    i_sabre_codec_switch_get, i_sabre_codec_switch_put),
SOC_ENUM("FIR Filter Type", i_sabre_fir_filter_type_enum),
SOC_ENUM("I2S/SPDIF Select", i_sabre_iis_spdif_sel_enum),
};


static const u32 i_sabre_codec_dai_rates_slave[] = {
	8000, 11025, 16000, 22050, 32000,
	44100, 48000, 64000, 88200, 96000,
	176400, 192000, 352800, 384000,
	705600, 768000, 1411200, 1536000
};

static const struct snd_pcm_hw_constraint_list constraints_slave = {
	.list  = i_sabre_codec_dai_rates_slave,
	.count = ARRAY_SIZE(i_sabre_codec_dai_rates_slave),
};

static int i_sabre_codec_dai_startup_slave(
		struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	int ret;

	ret = snd_pcm_hw_constraint_list(substream->runtime,
			0, SNDRV_PCM_HW_PARAM_RATE, &constraints_slave);
	if (ret != 0) {
		dev_err(component->card->dev, "Failed to setup rates constraints: %d\n", ret);
	}

	return ret;
}

static int i_sabre_codec_dai_startup(
		struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct snd_soc_component      *component = dai->component;
	struct i_sabre_codec_priv *i_sabre_codec
					= snd_soc_component_get_drvdata(component);

	switch (i_sabre_codec->fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBC_CFC:
		return i_sabre_codec_dai_startup_slave(substream, dai);

	default:
		return (-EINVAL);
	}
}

static int i_sabre_codec_hw_params(
	struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params,
	struct snd_soc_dai *dai)
{
	struct snd_soc_component      *component = dai->component;
	struct i_sabre_codec_priv *i_sabre_codec
					= snd_soc_component_get_drvdata(component);
	unsigned int daifmt;
	int format_width;

	dev_dbg(component->card->dev, "hw_params %u Hz, %u channels\n",
			params_rate(params), params_channels(params));

	/* Check I2S Format (Bit Size) */
	format_width = snd_pcm_format_width(params_format(params));
	if ((format_width != 32) && (format_width != 16)) {
		dev_err(component->card->dev, "Bad frame size: %d\n",
				snd_pcm_format_width(params_format(params)));
		return (-EINVAL);
	}

	/* Check Slave Mode */
	daifmt = i_sabre_codec->fmt & SND_SOC_DAIFMT_MASTER_MASK;
	if (daifmt != SND_SOC_DAIFMT_CBC_CFC) {
		return (-EINVAL);
	}

	/* Notify Sampling Frequency  */
	switch (params_rate(params))
	{
	case 44100:
	case 48000:
	case 88200:
	case 96000:
	case 176400:
	case 192000:
		snd_soc_component_update_bits(component, ISABRECODEC_REG_10, 0x01, 0x00);
		break;

	case 352800:
	case 384000:
	case 705600:
	case 768000:
	case 1411200:
	case 1536000:
		snd_soc_component_update_bits(component, ISABRECODEC_REG_10, 0x01, 0x01);
		break;
	}

	return 0;
}

static int i_sabre_codec_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component      *component = dai->component;
	struct i_sabre_codec_priv *i_sabre_codec
					= snd_soc_component_get_drvdata(component);

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;

	case SND_SOC_DAIFMT_RIGHT_J:
	case SND_SOC_DAIFMT_LEFT_J:
	default:
		return (-EINVAL);
	}

	/* clock inversion */
	if ((fmt & SND_SOC_DAIFMT_INV_MASK) != SND_SOC_DAIFMT_NB_NF) {
		return (-EINVAL);
	}

	/* Set Audio Data Format */
	i_sabre_codec->fmt = fmt;

	return 0;
}

static int i_sabre_codec_dac_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;
	struct i_sabre_codec_priv *i_sabre_codec =
		snd_soc_component_get_drvdata(component);

	mutex_lock(&i_sabre_codec->mute_lock);
	i_sabre_codec->stream_mute = mute;
	i_sabre_codec_set_mute(component);
	mutex_unlock(&i_sabre_codec->mute_lock);

	return 0;
}


static const struct snd_soc_dai_ops i_sabre_codec_dai_ops = {
	.startup      = i_sabre_codec_dai_startup,
	.hw_params    = i_sabre_codec_hw_params,
	.set_fmt      = i_sabre_codec_set_fmt,
	.mute_stream  = i_sabre_codec_dac_mute,
};

static struct snd_soc_dai_driver i_sabre_codec_dai = {
	.name = "i-sabre-codec-dai",
	.playback = {
		.stream_name  = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
		.rate_min = 8000,
		.rate_max = 1536000,
		.formats      = SNDRV_PCM_FMTBIT_S16_LE
				| SNDRV_PCM_FMTBIT_S32_LE,
	},
	.ops = &i_sabre_codec_dai_ops,
};

static struct snd_soc_component_driver i_sabre_codec_codec_driver = {
		.controls         = i_sabre_codec_controls,
		.num_controls     = ARRAY_SIZE(i_sabre_codec_controls),
};


static const struct regmap_config i_sabre_codec_regmap = {
	.reg_bits         = 8,
	.val_bits         = 8,
	.max_register     = ISABRECODEC_MAX_REG,

	.reg_defaults     = i_sabre_codec_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(i_sabre_codec_reg_defaults),

	.writeable_reg    = i_sabre_codec_writeable,
	.readable_reg     = i_sabre_codec_readable,
	.volatile_reg     = i_sabre_codec_volatile,

	.reg_read         = i_sabre_codec_reg_read,
	.reg_write        = i_sabre_codec_reg_write,

	.cache_type       = REGCACHE_RBTREE,
};


static int i_sabre_codec_probe(struct device *dev, struct regmap *regmap)
{
	struct i_sabre_codec_priv *i_sabre_codec;
	int ret;

	i_sabre_codec = devm_kzalloc(dev, sizeof(*i_sabre_codec), GFP_KERNEL);
	if (!i_sabre_codec) {
		dev_err(dev, "devm_kzalloc");
		return (-ENOMEM);
	}

	i_sabre_codec->regmap = regmap;
	INIT_WORK(&i_sabre_codec->forget, i_sabre_codec_forget);
	mutex_init(&i_sabre_codec->mute_lock);

	dev_set_drvdata(dev, i_sabre_codec);

	ret = snd_soc_register_component(dev,
			&i_sabre_codec_codec_driver, &i_sabre_codec_dai, 1);
	if (ret != 0) {
		dev_err(dev, "Failed to register CODEC: %d\n", ret);
		return ret;
	}

	return 0;
}

static void i_sabre_codec_remove(struct device *dev)
{
	struct i_sabre_codec_priv *i_sabre_codec = dev_get_drvdata(dev);

	snd_soc_unregister_component(dev);
	cancel_work_sync(&i_sabre_codec->forget);
}


static int i_sabre_codec_i2c_probe(struct i2c_client *i2c)
{
	struct regmap *regmap;

	/* No bus is named: the two functions above do the transfers. */
	regmap = devm_regmap_init(&i2c->dev, NULL, i2c, &i_sabre_codec_regmap);
	if (IS_ERR(regmap)) {
		return PTR_ERR(regmap);
	}

	return i_sabre_codec_probe(&i2c->dev, regmap);
}

static void i_sabre_codec_i2c_remove(struct i2c_client *i2c)
{
	i_sabre_codec_remove(&i2c->dev);
}


static const struct i2c_device_id i_sabre_codec_i2c_id[] = {
	{ "i-sabre-codec", },
	{ }
};
MODULE_DEVICE_TABLE(i2c, i_sabre_codec_i2c_id);

static const struct of_device_id i_sabre_codec_of_match[] = {
	{ .compatible = "audiophonics,i-sabre-codec", },
	{ }
};
MODULE_DEVICE_TABLE(of, i_sabre_codec_of_match);

static struct i2c_driver i_sabre_codec_i2c_driver = {
	.driver = {
		.name           = "i-sabre-codec-i2c",
		.owner          = THIS_MODULE,
		.of_match_table = of_match_ptr(i_sabre_codec_of_match),
	},
	.probe    = i_sabre_codec_i2c_probe,
	.remove   = i_sabre_codec_i2c_remove,
	.id_table = i_sabre_codec_i2c_id,
};
module_i2c_driver(i_sabre_codec_i2c_driver);


MODULE_DESCRIPTION("ASoC I-Sabre Q2M codec driver");
MODULE_AUTHOR("Audiophonics <http://www.audiophonics.fr>");
MODULE_LICENSE("GPL");
