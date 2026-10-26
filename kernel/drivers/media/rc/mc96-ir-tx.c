// SPDX-License-Identifier: GPL-2.0-only
/*
 * Experimental SM-T311 MC96 I2C infrared TX port.
 * Samsung's GPL ir_remote_con_mc96.c documents the I2C packet format.
 * This driver NEVER flashes firmware. Deployed on SM-T311 in October 2026.
 */
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <media/rc-core.h>

#define MC96_MAX_BYTES 2048
#define MC96_MAX_EDGES ((MC96_MAX_BYTES - 7) / 2)

struct mc96_tx {
	struct i2c_client *client;
	struct gpio_desc *wake, *led, *ack;
	struct regulator *vled;
	struct mutex mutex;
	u32 carrier;
};

static int mc96_carrier(struct rc_dev *rc, u32 carrier)
{
	struct mc96_tx *dev = rc->priv;

	if (carrier < 30000 || carrier > 60000)
		return -EINVAL;
	WRITE_ONCE(dev->carrier, carrier);
	return 0;
}

static int mc96_transmit(struct rc_dev *rc, unsigned int *timings, unsigned int n)
{
	struct mc96_tx *dev = rc->priv;
	u8 *data;
	u32 freq, checksum = 0;
	size_t count;
	u64 total_us = 0;
	unsigned int i;
	int ret;

	if (!n || n > MC96_MAX_EDGES)
		return -EINVAL;

	mutex_lock(&dev->mutex);
	freq = READ_ONCE(dev->carrier);
	count = 5 + 2 * n;
	data = kzalloc(count + 2, GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto unlock;
	}

	data[0] = count >> 8;
	data[1] = count;
	data[2] = freq >> 16;
	data[3] = freq >> 8;
	data[4] = freq;
	for (i = 0; i < n; i++) {
		u64 cycles;

		if (!timings[i] || timings[i] > 500000) {
			ret = -EINVAL;
			goto free_buffer;
		}
		total_us += timings[i];
		if (total_us > 2000000ULL) {
			ret = -E2BIG;
			goto free_buffer;
		}
		/* Legacy Samsung interface uses carrier cycles, NOT microseconds. */
		cycles = DIV_ROUND_CLOSEST_ULL((u64)timings[i] * freq, 1000000);
		if (!cycles || cycles > 65535) {
			ret = -ERANGE;
			goto free_buffer;
		}
		data[5 + 2 * i] = cycles >> 8;
		data[6 + 2 * i] = cycles;
	}
	for (i = 0; i < count; i++)
		checksum += data[i];
	data[count] = checksum >> 8;
	data[count + 1] = checksum;

	ret = regulator_enable(dev->vled);
	if (ret)
		goto free_buffer;
	gpiod_set_value_cansleep(dev->led, 1);
	gpiod_set_value_cansleep(dev->wake, 1);
	msleep(60); /* Stock MC96 user-mode wake timing */
	ret = i2c_master_send(dev->client, data, count + 2);
	if (ret != count + 2) {
		ret = ret < 0 ? ret : -EIO;
		goto power_off;
	}
	msleep(10);
	/* MC96 IRQ high = checksum error; low = checksum accepted. */
	ret = gpiod_get_value_cansleep(dev->ack);
	if (ret != 0) {
		if (ret >= 0)
			ret = -EIO;
		goto power_off;
	}
	msleep(DIV_ROUND_UP_ULL(total_us, 1000) + 50);
	ret = gpiod_get_value_cansleep(dev->ack);
	if (ret != 1) {
		if (ret >= 0)
			ret = -EIO;
		goto power_off;
	}
	ret = n;
power_off:
	gpiod_set_value_cansleep(dev->wake, 0);
	gpiod_set_value_cansleep(dev->led, 0);
	regulator_disable(dev->vled);
free_buffer:
	kfree(data);
unlock:
	mutex_unlock(&dev->mutex);
	return ret;
}

static int mc96_probe(struct i2c_client *client)
{
	struct mc96_tx *dev;
	struct rc_dev *rc;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EOPNOTSUPP;
	dev = devm_kzalloc(&client->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	mutex_init(&dev->mutex);
	dev->client = client;
	dev->carrier = 38000;
	dev->wake = devm_gpiod_get(&client->dev, "wake", GPIOD_OUT_LOW);
	if (IS_ERR(dev->wake))
		return PTR_ERR(dev->wake);
	dev->led = devm_gpiod_get(&client->dev, "led", GPIOD_OUT_LOW);
	if (IS_ERR(dev->led))
		return PTR_ERR(dev->led);
	dev->ack = devm_gpiod_get(&client->dev, "ack", GPIOD_IN);
	if (IS_ERR(dev->ack))
		return PTR_ERR(dev->ack);
	dev->vled = devm_regulator_get(&client->dev, "vled");
	if (IS_ERR(dev->vled))
		return PTR_ERR(dev->vled);
	rc = devm_rc_allocate_device(&client->dev, RC_DRIVER_IR_RAW_TX);
	if (!rc)
		return -ENOMEM;
	rc->driver_name = "t311-mc96-ir";
	rc->device_name = "Samsung Galaxy Tab 3 8.0 MC96 IR";
	rc->priv = dev;
	rc->tx_ir = mc96_transmit;
	rc->s_tx_carrier = mc96_carrier;
	ret = devm_rc_register_device(&client->dev, rc);
	if (!ret)
		dev_info(&client->dev, "MC96 IR TX ready; firmware not touched\n");
	return ret;
}

static const struct of_device_id mc96_dt_ids[] = {
	{ .compatible = "samsung,tab3-mc96-ir" },
	{ }
};
MODULE_DEVICE_TABLE(of, mc96_dt_ids);

static struct i2c_driver mc96_i2c_driver = {
	.probe = mc96_probe,
	.driver = {
		.name = "t311-mc96-ir",
		.of_match_table = mc96_dt_ids,
	},
};
module_i2c_driver(mc96_i2c_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Experimental Galaxy Tab 3 MC96 IR transmitter, no FW updates");
