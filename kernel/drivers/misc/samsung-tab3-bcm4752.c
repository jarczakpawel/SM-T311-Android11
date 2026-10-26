// SPDX-License-Identifier: GPL-2.0
/*
 * Samsung Galaxy Tab 3 BCM4752 power control
 *
 * Copyright (C) 2026 Pawel Jarczak
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/kobject.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

struct tab3_bcm4752 {
	struct device *dev;
	struct gpio_desc *enable_gpio;
	struct clk *lpo_clk;
	struct class *sec_class;
	struct device *gps_dev;
	struct kobject *power_kobj;
	struct mutex lock;
	bool powered;
};

static struct tab3_bcm4752 *tab3_bcm4752_from_kobj(struct kobject *kobj)
{
	struct device *gps_dev;

	if (!kobj->parent)
		return NULL;

	gps_dev = kobj_to_dev(kobj->parent);
	return dev_get_drvdata(gps_dev);
}

static ssize_t value_show(struct kobject *kobj, struct kobj_attribute *attr,
			  char *buf)
{
	struct tab3_bcm4752 *gps = tab3_bcm4752_from_kobj(kobj);
	int value;

	if (!gps)
		return -ENODEV;

	mutex_lock(&gps->lock);
	value = gpiod_get_value_cansleep(gps->enable_gpio);
	if (value >= 0)
		gps->powered = value;
	mutex_unlock(&gps->lock);

	if (value < 0)
		return value;

	return sysfs_emit(buf, "%d\n", value);
}

static ssize_t value_store(struct kobject *kobj, struct kobj_attribute *attr,
			   const char *buf, size_t count)
{
	struct tab3_bcm4752 *gps = tab3_bcm4752_from_kobj(kobj);
	bool enable;
	int ret;

	if (!gps)
		return -ENODEV;

	ret = kstrtobool(buf, &enable);
	if (ret)
		return ret;

	mutex_lock(&gps->lock);
	gpiod_set_value_cansleep(gps->enable_gpio, enable);
	gps->powered = enable;
	mutex_unlock(&gps->lock);

	dev_info(gps->dev, "TAB3_V161_BCM4752_POWER=%u\n", enable);
	return count;
}

static struct kobj_attribute value_attribute = __ATTR(value, 0664,
					       value_show, value_store);

static int tab3_bcm4752_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tab3_bcm4752 *gps;
	int ret;

	gps = devm_kzalloc(dev, sizeof(*gps), GFP_KERNEL);
	if (!gps)
		return -ENOMEM;

	gps->dev = dev;
	mutex_init(&gps->lock);

	gps->enable_gpio = devm_gpiod_get(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(gps->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(gps->enable_gpio),
				     "failed to acquire GPS enable GPIO\n");

	gps->lpo_clk = devm_clk_get_enabled(dev, "lpo");
	if (IS_ERR(gps->lpo_clk))
		return dev_err_probe(dev, PTR_ERR(gps->lpo_clk),
				     "failed to enable 32.768 kHz LPO clock\n");

	gps->sec_class = class_create("sec");
	if (IS_ERR(gps->sec_class))
		return dev_err_probe(dev, PTR_ERR(gps->sec_class),
				     "failed to create sec class\n");

	gps->gps_dev = device_create(gps->sec_class, NULL, 0, gps, "gps");
	if (IS_ERR(gps->gps_dev)) {
		ret = PTR_ERR(gps->gps_dev);
		dev_err(dev, "failed to create gps class device: %d\n", ret);
		goto err_class;
	}

	gps->power_kobj = kobject_create_and_add("GPS_PWR_EN",
						 &gps->gps_dev->kobj);
	if (!gps->power_kobj) {
		ret = -ENOMEM;
		dev_err(dev, "failed to create GPS_PWR_EN sysfs node\n");
		goto err_device;
	}

	ret = sysfs_create_file(gps->power_kobj, &value_attribute.attr);
	if (ret) {
		dev_err(dev, "failed to create GPS power value attribute: %d\n",
			ret);
		goto err_kobject;
	}

	platform_set_drvdata(pdev, gps);
	dev_info(dev,
		 "TAB3_V161_BCM4752_READY uart=/dev/ttySAC1 power=/sys/class/sec/gps/GPS_PWR_EN/value lpo=32768Hz\n");
	return 0;

err_kobject:
	kobject_put(gps->power_kobj);
err_device:
	device_unregister(gps->gps_dev);
err_class:
	class_destroy(gps->sec_class);
	return ret;
}

static void tab3_bcm4752_remove(struct platform_device *pdev)
{
	struct tab3_bcm4752 *gps = platform_get_drvdata(pdev);

	mutex_lock(&gps->lock);
	gpiod_set_value_cansleep(gps->enable_gpio, 0);
	gps->powered = false;
	mutex_unlock(&gps->lock);

	sysfs_remove_file(gps->power_kobj, &value_attribute.attr);
	kobject_put(gps->power_kobj);
	device_unregister(gps->gps_dev);
	class_destroy(gps->sec_class);
}

static const struct of_device_id tab3_bcm4752_of_match[] = {
	{ .compatible = "samsung,tab3-bcm4752" },
	{ }
};
MODULE_DEVICE_TABLE(of, tab3_bcm4752_of_match);

static struct platform_driver tab3_bcm4752_driver = {
	.probe = tab3_bcm4752_probe,
	.remove = tab3_bcm4752_remove,
	.driver = {
		.name = "samsung-tab3-bcm4752",
		.of_match_table = tab3_bcm4752_of_match,
	},
};
module_platform_driver(tab3_bcm4752_driver);

MODULE_AUTHOR("Pawel Jarczak");
MODULE_DESCRIPTION("Samsung Galaxy Tab 3 BCM4752 power control");
MODULE_LICENSE("GPL");
