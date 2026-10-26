// SPDX-License-Identifier: GPL-2.0-only
/*
 * Capella Microsystems CM36653 color and proximity sensor
 *
 * Register programming is based on the original Samsung CM36653 driver.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/property.h>
#include <linux/pm_wakeup.h>
#include <linux/regulator/consumer.h>
#include <linux/string.h>

#include <linux/iio/events.h>
#include <linux/iio/iio.h>

#define CM36653_REG_ALS_CONF		0x00
#define CM36653_REG_PS_CONF1		0x03
#define CM36653_REG_PS_THD		0x05
#define CM36653_REG_PS_CANC		0x06
#define CM36653_REG_PS_DATA		0x07
#define CM36653_REG_RED		0x08
#define CM36653_REG_GREEN		0x09
#define CM36653_REG_BLUE		0x0a
#define CM36653_REG_CLEAR		0x0b

#define CM36653_ALS_ENABLE		0x00
#define CM36653_ALS_DISABLE		0x01
#define CM36653_PS_ENABLE		0x430c
#define CM36653_PS_DISABLE		0x0001

#define CM36653_DEFAULT_THD_LOW		8
#define CM36653_DEFAULT_THD_HIGH	10
#define CM36653_ALS_STARTUP_MS		200
#define CM36653_ALS_CACHE_MS		250
#define CM36653_PS_STARTUP_MS		50

struct cm36653_data {
	struct i2c_client *client;
	struct regulator *vdd;
	struct regulator *vled;
	/* Protect sensor mode and I2C register accesses. */
	struct mutex lock;
	u8 threshold_low;
	u8 threshold_high;
	bool proximity_event_enabled;
	bool proximity_near;
	u16 als_cache[4];
	unsigned long als_cache_expires;
	bool als_cache_valid;
};

#define CM36653_COLOR_CHANNEL(_color, _reg) { \
	.type = IIO_INTENSITY, \
	.modified = 1, \
	.channel2 = IIO_MOD_LIGHT_##_color, \
	.address = (_reg), \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
}

static const struct iio_event_spec cm36653_proximity_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE),
	},
};

static const struct iio_chan_spec cm36653_channels[] = {
	CM36653_COLOR_CHANNEL(RED, CM36653_REG_RED),
	CM36653_COLOR_CHANNEL(GREEN, CM36653_REG_GREEN),
	CM36653_COLOR_CHANNEL(BLUE, CM36653_REG_BLUE),
	CM36653_COLOR_CHANNEL(CLEAR, CM36653_REG_CLEAR),
	{
		.type = IIO_LIGHT,
		.address = CM36653_REG_CLEAR,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	},
	{
		.type = IIO_PROXIMITY,
		.address = CM36653_REG_PS_DATA,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.event_spec = cm36653_proximity_events,
		.num_event_specs = ARRAY_SIZE(cm36653_proximity_events),
	},
};

static int cm36653_write_byte(struct cm36653_data *data, u8 reg, u8 value)
{
	return i2c_smbus_write_byte_data(data->client, reg, value);
}

static int cm36653_write_word(struct cm36653_data *data, u8 reg, u16 value)
{
	return i2c_smbus_write_word_data(data->client, reg, value);
}

static int cm36653_read_word(struct cm36653_data *data, u8 reg)
{
	return i2c_smbus_read_word_data(data->client, reg);
}

static int cm36653_write_thresholds(struct cm36653_data *data)
{
	u16 value = data->threshold_low | (data->threshold_high << 8);

	return cm36653_write_word(data, CM36653_REG_PS_THD, value);
}

static int cm36653_set_proximity(struct cm36653_data *data, bool enable)
{
	int ret;

	if (enable) {
		ret = regulator_enable(data->vled);
		if (ret)
			return ret;

		ret = cm36653_write_thresholds(data);
		if (ret)
			goto disable_vled;

		ret = cm36653_write_word(data, CM36653_REG_PS_CANC, 0);
		if (ret)
			goto disable_vled;

		ret = cm36653_write_word(data, CM36653_REG_PS_CONF1,
					 CM36653_PS_ENABLE);
		if (ret)
			goto disable_vled;

		msleep(CM36653_PS_STARTUP_MS);
		return 0;
	}

	ret = cm36653_write_word(data, CM36653_REG_PS_CONF1,
				 CM36653_PS_DISABLE);
	regulator_disable(data->vled);
	return ret;

disable_vled:
	regulator_disable(data->vled);
	return ret;
}

static int cm36653_color_index(u8 reg)
{
	switch (reg) {
	case CM36653_REG_RED:
		return 0;
	case CM36653_REG_GREEN:
		return 1;
	case CM36653_REG_BLUE:
		return 2;
	case CM36653_REG_CLEAR:
		return 3;
	default:
		return -EINVAL;
	}
}

static int cm36653_refresh_color_cache(struct cm36653_data *data)
{
	static const u8 registers[] = {
		CM36653_REG_RED,
		CM36653_REG_GREEN,
		CM36653_REG_BLUE,
		CM36653_REG_CLEAR,
	};
	u16 snapshot[ARRAY_SIZE(registers)];
	int disable_ret;
	int ret;
	size_t i;

	ret = cm36653_write_byte(data, CM36653_REG_ALS_CONF,
				 CM36653_ALS_ENABLE);
	if (ret)
		return ret;

	msleep(CM36653_ALS_STARTUP_MS);
	for (i = 0; i < ARRAY_SIZE(registers); i++) {
		ret = cm36653_read_word(data, registers[i]);
		if (ret < 0)
			break;
		snapshot[i] = ret;
	}

	disable_ret = cm36653_write_byte(data, CM36653_REG_ALS_CONF,
					 CM36653_ALS_DISABLE);
	if (ret >= 0 && disable_ret)
		ret = disable_ret;
	if (ret < 0) {
		data->als_cache_valid = false;
		return ret;
	}

	memcpy(data->als_cache, snapshot, sizeof(snapshot));
	data->als_cache_expires = jiffies +
		msecs_to_jiffies(CM36653_ALS_CACHE_MS);
	data->als_cache_valid = true;

	return 0;
}

static int cm36653_read_color(struct cm36653_data *data, u8 reg)
{
	int index;
	int ret;

	index = cm36653_color_index(reg);
	if (index < 0)
		return index;

	if (!data->als_cache_valid ||
	    time_after_eq(jiffies, data->als_cache_expires)) {
		ret = cm36653_refresh_color_cache(data);
		if (ret)
			return ret;
	}

	return data->als_cache[index];
}

static int cm36653_read_proximity(struct cm36653_data *data)
{
	bool temporary_enable = !data->proximity_event_enabled;
	int ret;

	if (temporary_enable) {
		ret = cm36653_set_proximity(data, true);
		if (ret)
			return ret;
	}

	ret = cm36653_read_word(data, CM36653_REG_PS_DATA);

	if (temporary_enable)
		cm36653_set_proximity(data, false);

	return ret;
}

static int cm36653_read_raw(struct iio_dev *indio_dev,
			    const struct iio_chan_spec *chan,
			    int *val, int *val2, long mask)
{
	struct cm36653_data *data = iio_priv(indio_dev);
	int ret;

	if (mask != IIO_CHAN_INFO_RAW)
		return -EINVAL;

	mutex_lock(&data->lock);
	if (chan->type == IIO_PROXIMITY)
		ret = cm36653_read_proximity(data);
	else
		ret = cm36653_read_color(data, chan->address);
	mutex_unlock(&data->lock);

	if (ret < 0)
		return ret;

	*val = ret;
	return IIO_VAL_INT;
}

static int cm36653_read_event_config(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir)
{
	struct cm36653_data *data = iio_priv(indio_dev);

	return data->proximity_event_enabled;
}

static int cm36653_write_event_config(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan,
				      enum iio_event_type type,
				      enum iio_event_direction dir,
				      bool state)
{
	struct cm36653_data *data = iio_priv(indio_dev);
	int ret = 0;

	mutex_lock(&data->lock);
	if (state == data->proximity_event_enabled)
		goto out_unlock;

	if (state) {
		ret = cm36653_set_proximity(data, true);
		if (ret)
			goto out_unlock;

		data->proximity_event_enabled = true;
		enable_irq(data->client->irq);
		if (device_may_wakeup(&data->client->dev))
			enable_irq_wake(data->client->irq);
	} else {
		if (device_may_wakeup(&data->client->dev))
			disable_irq_wake(data->client->irq);
		disable_irq(data->client->irq);
		data->proximity_event_enabled = false;
		ret = cm36653_set_proximity(data, false);
	}

out_unlock:
	mutex_unlock(&data->lock);
	return ret;
}

static const struct iio_info cm36653_info = {
	.read_raw = cm36653_read_raw,
	.read_event_config = cm36653_read_event_config,
	.write_event_config = cm36653_write_event_config,
};

static irqreturn_t cm36653_irq_thread(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct cm36653_data *data = iio_priv(indio_dev);
	enum iio_event_direction direction;
	bool near;
	int ret;

	mutex_lock(&data->lock);
	ret = cm36653_read_word(data, CM36653_REG_PS_DATA);
	if (ret < 0) {
		mutex_unlock(&data->lock);
		return IRQ_HANDLED;
	}

	near = (ret & 0xff) >= data->threshold_high;
	if (!near && (ret & 0xff) > data->threshold_low)
		near = data->proximity_near;

	if (near == data->proximity_near) {
		mutex_unlock(&data->lock);
		return IRQ_HANDLED;
	}

	data->proximity_near = near;
	direction = near ? IIO_EV_DIR_RISING : IIO_EV_DIR_FALLING;
	mutex_unlock(&data->lock);

	iio_push_event(indio_dev,
		       IIO_UNMOD_EVENT_CODE(IIO_PROXIMITY, 0,
					    IIO_EV_TYPE_THRESH, direction),
		       iio_get_time_ns(indio_dev));
	pm_wakeup_event(&data->client->dev, 3000);

	return IRQ_HANDLED;
}

static void cm36653_power_off(void *private)
{
	struct iio_dev *indio_dev = private;
	struct cm36653_data *data = iio_priv(indio_dev);

	if (data->proximity_event_enabled) {
		disable_irq(data->client->irq);
		cm36653_set_proximity(data, false);
	}
	cm36653_write_byte(data, CM36653_REG_ALS_CONF,
			   CM36653_ALS_DISABLE);
	regulator_disable(data->vdd);
}

static int cm36653_probe(struct i2c_client *client)
{
	struct cm36653_data *data;
	struct iio_dev *indio_dev;
	u32 value;
	int ret;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_WORD_DATA))
		return -EOPNOTSUPP;

	if (client->irq <= 0)
		return dev_err_probe(&client->dev, -EINVAL,
				     "missing proximity interrupt\n");

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->client = client;
	mutex_init(&data->lock);

	data->vdd = devm_regulator_get(&client->dev, "vdd");
	if (IS_ERR(data->vdd))
		return dev_err_probe(&client->dev, PTR_ERR(data->vdd),
				     "failed to get vdd supply\n");

	data->vled = devm_regulator_get(&client->dev, "vled");
	if (IS_ERR(data->vled))
		return dev_err_probe(&client->dev, PTR_ERR(data->vled),
				     "failed to get vled supply\n");

	data->threshold_low = CM36653_DEFAULT_THD_LOW;
	data->threshold_high = CM36653_DEFAULT_THD_HIGH;
	if (!device_property_read_u32(&client->dev,
				      "capella,proximity-threshold-low",
				      &value)) {
		if (value > U8_MAX)
			return -EINVAL;
		data->threshold_low = value;
	}
	if (!device_property_read_u32(&client->dev,
				      "capella,proximity-threshold-high",
				      &value)) {
		if (value > U8_MAX)
			return -EINVAL;
		data->threshold_high = value;
	}
	if (data->threshold_low >= data->threshold_high)
		return dev_err_probe(&client->dev, -EINVAL,
				     "invalid proximity thresholds\n");

	ret = regulator_enable(data->vdd);
	if (ret)
		return ret;

	ret = cm36653_write_byte(data, CM36653_REG_ALS_CONF,
				 CM36653_ALS_DISABLE);
	if (ret)
		goto disable_vdd;

	ret = cm36653_write_word(data, CM36653_REG_PS_CONF1,
				 CM36653_PS_DISABLE);
	if (ret)
		goto disable_vdd;

	ret = cm36653_write_thresholds(data);
	if (ret)
		goto disable_vdd;

	ret = devm_request_threaded_irq(&client->dev, client->irq, NULL,
					cm36653_irq_thread,
					IRQF_TRIGGER_RISING |
					IRQF_TRIGGER_FALLING |
					IRQF_ONESHOT | IRQF_NO_AUTOEN,
					dev_name(&client->dev), indio_dev);
	if (ret)
		goto disable_vdd;

	device_init_wakeup(&client->dev,
			   device_property_read_bool(&client->dev, "wakeup-source"));

	indio_dev->name = "cm36653";
	indio_dev->info = &cm36653_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = cm36653_channels;
	indio_dev->num_channels = ARRAY_SIZE(cm36653_channels);
	i2c_set_clientdata(client, indio_dev);

	ret = devm_add_action_or_reset(&client->dev, cm36653_power_off,
				       indio_dev);
	if (ret)
		return ret;

	ret = devm_iio_device_register(&client->dev, indio_dev);
	if (ret)
		return ret;

	dev_info(&client->dev,
		 "CM36653 RGB/prox, thresholds %u/%u, RGBC cache %u ms\n",
		 data->threshold_low, data->threshold_high,
		 CM36653_ALS_CACHE_MS);
	return 0;

disable_vdd:
	regulator_disable(data->vdd);
	return dev_err_probe(&client->dev, ret,
			     "failed to initialize sensor\n");
}

static const struct of_device_id cm36653_of_match[] = {
	{ .compatible = "capella,cm36653" },
	{ }
};
MODULE_DEVICE_TABLE(of, cm36653_of_match);

static const struct i2c_device_id cm36653_id[] = {
	{ "cm36653" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, cm36653_id);

static struct i2c_driver cm36653_driver = {
	.driver = {
		.name = "cm36653",
		.of_match_table = cm36653_of_match,
	},
	.probe = cm36653_probe,
	.id_table = cm36653_id,
};
module_i2c_driver(cm36653_driver);

MODULE_AUTHOR("Paweł Jarczak");
MODULE_DESCRIPTION("Capella CM36653 color and proximity sensor driver");
MODULE_LICENSE("GPL");
