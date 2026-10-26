#!/usr/bin/env python3
"""G385: audit original SM-T311 BCM4752 pin/clock UART vs Linux6.12 and
physical *idle* GNSS plumbing. No GPS position is collected; no GPS start.
"""
from pathlib import Path
import argparse
import json
import re

def need(cond, what):
    if not cond:
        raise ValueError(what)

def audit(cfg, gpio, stock_driver, kernel_dts, kernel_dev, live):
    need("CONFIG_TARGET_TAB3_3G8=y" in cfg, "incorrect stock model")
    need("CONFIG_GPS_BCM4752=y" in cfg and
         "# CONFIG_GPS_BCM47521 is not set" in cfg, "wrong factory GPS variant")
    for name, index in [("RXD",4),("TXD",5),("CTS",6),("RTS",7)]:
        need(re.search(r"#define GPIO_GPS_"+name+r"\s+EXYNOS4_GPA0\("+str(index)+r"\)",gpio) is not None,
             "stock pin changed: "+name)
        need(re.search(r"gps_uart_(?:rx|tx|fctl):.*?\};",kernel_dts,re.S) is not None,
             "modern GPS pinctrl nodes absent")
        need('"gpa0-'+str(index)+'"' in kernel_dts, "modern GPS UART pin missing: "+name)
        need("GPIO_GPS_"+name in stock_driver, "not a real stock GPS UART signal: "+name)
    for name in ("RXD","TXD","CTS","RTS"):
        need(re.search(r"#define GPIO_GPS_"+name+r"_AF\s+2\b",gpio) is not None,
             "factory UART alternate function differs: "+name)
    # Preserve stock RX pull-up, CTS pull-none, DTS UART hw flow control.
    need("gps_uart_rx: gps-uart-rx-pins" in kernel_dts and
         re.search(r'gps_uart_rx:.*?EXYNOS_PIN_PULL_UP',kernel_dts,re.S),
         "missing stock RX pull-up")
    need("uart-has-rtscts;" in kernel_dts and
         "samsung,uart-force-hw-flow-control;" in kernel_dts,
         "UART hardware flow control missing")
    need('&serial_1 {' in kernel_dts and 'status = "okay";' in kernel_dts,
         "GNSS UART port missing")
    need("GPIO_GPS_PWR_EN" in stock_driver and
         re.search(r"#define GPIO_GPS_PWR_EN\s+EXYNOS4_GPL2\(2\)",gpio),
         "wrong stock receiver GPIO")
    need('enable-gpios = <&gpl2 2 GPIO_ACTIVE_HIGH>;' in kernel_dts,
         "modern receiver power pin differs from stock")
    need('clock-names = "lpo";' in kernel_dts and
         "devm_clk_get_enabled" in kernel_dev and
         "devm_gpiod_get" in kernel_dev,
         "modern BCM4752 32.768kHz clock / power driver missing")
    def val(marker):
        m=re.search(r'^'+re.escape(marker)+r'\n([^\n]*)',live,re.M)
        need(m is not None, "missing recorded live field "+marker)
        return m.group(1).strip()
    need(val("G385_MODEL") == "lt013g", "wrong live model")
    need(val("G385_BOOT") == "1", "system not booted")
    need(val("G385_SELINUX") == "Enforcing", "global SELinux unexpected")
    need(val("G385_GPS_INIT") == "running", "legacy GNSS launcher not running")
    processes=live.split("G385_GPS_PROCESSES\n",1)[-1].split("\nG385_UART",1)[0]
    for process in ("gpsd", "lt013g-gps-bridge-host",
                    "android.hardware.gnss@1.0-service"):
        need(process in processes.splitlines(),"missing GNSS process "+process)
    need("character device" in val("G385_UART"), "GPS UART missing")
    power=val("G385_GPS_POWER")
    need(power in ("0","1"),"unexpected BCM4752 power value")
    kpi=live.split("G385_ANDROID_KPI\n",1)[-1].split("\nG385_END",1)[0]
    started=re.search(r"mStarted=(true|false)",kpi)
    need(started is not None,"framework GNSS started state absent")
    n=re.search(r"Total number of sv status messages processed:\s*(\d+)",kpi)
    need(n is not None,"no framework GNSS KPI counter")
    return {"result":"PASS_STOCK_BCM4752_UART_POWER_AND_RUNTIME_ENUMERATION",
            "physical_android":"G379",
            "physical_gnss_started":started.group(1)=="true",
            "physical_receiver_power":int(power),
            "framework_sv_status_total":int(n.group(1)),
            "stock_uart":"GPA0(4-7), AF2, RX pull-up, CTS/RTS",
            "modern_uart":"serial_1 /dev/ttySAC1, hardware flow control",
            "stock_power":"GPL2(2), modern kernel 32.768kHz clock",
            "receiver_started_or_satellite_fix_verified":False,
            "no_location_coordinates_recorded":True,
            "no_device_writes":True}

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--stock",type=Path,required=True)
    ap.add_argument("--kernel",type=Path,required=True)
    ap.add_argument("--live",type=Path,required=True)
    ap.add_argument("--json-output",type=Path)
    a=ap.parse_args()
    d=a.stock/"arch/arm"
    cfg=(d/"configs/tab3_3G8_01_defconfig").read_text()
    gpio=(d/"mach-exynos/include/mach/gpio-rev00-tab3.h").read_text()
    orig=(d/"mach-exynos/sec_gps_bcm4752.c").read_text()
    kernel_dts=(a.kernel/"arch/arm/boot/dts/samsung/exynos4212-tab3-3g8.dts").read_text()
    kernel_driver=(a.kernel/"drivers/misc/samsung-tab3-bcm4752.c").read_text()
    raw=a.live.read_text()
    report=audit(cfg,gpio,orig,kernel_dts,kernel_driver,raw)
    broken={
        "wrong_model":(cfg.replace("CONFIG_TARGET_TAB3_3G8=y","CONFIG_TARGET_TAB3_WIFI8=y"),gpio,orig,kernel_dts,kernel_driver,raw),
        "wrong_stock_gps":(cfg.replace("# CONFIG_GPS_BCM47521 is not set","CONFIG_GPS_BCM47521=y"),gpio,orig,kernel_dts,kernel_driver,raw),
        "uart_pin_mismatch":(cfg,gpio,orig,kernel_dts.replace('"gpa0-4"','"gpa0-3"'),kernel_driver,raw),
        "uart_rtscts_disabled":(cfg,gpio,orig,kernel_dts.replace("uart-has-rtscts;",""),kernel_driver,raw),
        "power_gpio_wrong":(cfg,gpio,orig,kernel_dts.replace("enable-gpios = <&gpl2 2 GPIO_ACTIVE_HIGH>;","enable-gpios = <&gpl2 7 GPIO_ACTIVE_HIGH>;"),kernel_driver,raw),
        "missing_legacy_daemon":(cfg,gpio,orig,kernel_dts,kernel_driver,raw.replace("\ngpsd\n","\n")),
        "missing_uart":(cfg,gpio,orig,kernel_dts,kernel_driver,raw.replace("cc:41 character device","unavailable")),
    }
    for name,inputs in broken.items():
        try:
            audit(*inputs)
        except ValueError:
            pass
        else:
            raise AssertionError("fail-open negative control: "+name)
    report["deliberately_invalid_input_tests"]=len(broken)
    if a.json_output:
        a.json_output.parent.mkdir(exist_ok=True,parents=True)
        a.json_output.write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps(report,indent=2))

if __name__=="__main__":
    main()
