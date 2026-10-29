# maixcam_npu — NPU frequency control for MaixCAM Pro

A kernel module that retunes the NPU PLL (`clk_tpll`) through `clk_set_rate()`,
so the NPU clock `clk_tpu` (= tpll / divider) runs at a requested frequency.
Target: MaixCAM Pro / LicheeRV Nano (SG2002), kernel `5.10.4-tag-`.

## Background

- The NPU runs at **700 MHz** by default (`clk_tpll` 1400 MHz / 2); the PLL is configured
  once at boot and there is no runtime frequency API;
- no cpufreq in the kernel, and MaixCDK / MaixPy only expose the read-only `npu_freq()`;
- `clk_tpu` is a mux + divider without `CLK_SET_RATE_PARENT`, so it cannot be tuned directly —
  its parent PLL `clk_tpll` has to be changed (what this tool does).

## Usage

```sh
# upload
scp prebuilt/npu_freq.ko root@<device>:/root/npu_tool/

# 750 MHz NPU (module converts: tpll = 750 x 2 = 1500 MHz)
insmod /root/npu_tool/npu_freq.ko freq=750000000 delay_ms=1500
dmesg | tail -10

# verify
cat /sys/kernel/debug/clk/clk_tpu/clk_rate      # 750000000
cat /sys/kernel/debug/clk/clk_tpll/clk_rate     # 1500000000
devmem 0x0300290c 32                            # TPLL_CSR
devmem 0x03002054 32                            # DIV_CLK_TPU

# restore 700 MHz
rmmod npu_freq
insmod /root/npu_tool/npu_freq.ko freq=700000000 delay_ms=1500
rmmod npu_freq
```

Parameters:

| Name | Default | Description |
|---|---|---|
| `freq` | `750000000` | target **NPU** frequency in Hz; the module derives the PLL rate from the current divider |
| `delay_ms` | `5000` | delay before touching the PLL |

## Benchmark

```sh
/usr/bin/model_runner --model /usr/bin/mobilenet_v2_rgb_224_int8.cvimodel --count 20 --enable-timer
```

## Measured (MaixCAM Pro, maixcam-bca0, launcher stopped during the tests)

| NPU clock | `TPLL_CSR` | `clk_tpu` | mobilenet_v2 (20 runs, ms/run) | fps | yolov5s (10 runs, ms/run) | fps |
|---|---|---|---|---|---|---|
| 700 MHz (default) | `0x07708101` | 700000000 | 4.454 / 4.452 / 4.449 | 224.6 | 9.662 / 9.659 | 103.5 |
| 750 MHz | `0x07788101` | 750000000 | 4.289 / 4.307 / 4.285 | 232.9 | 9.195 / 9.197 | 108.7 |
| 800 MHz | `0x07808101` | 800000000 | 4.149 / 4.149 / 4.151 | 241.0 | 8.801 / 8.811 | 113.6 |

* vs. 700 MHz: mobilenet is **3.7%** (750) / **7.3%** (800) faster; yolov5s is **5.0%** / **9.7%** faster;
* the ideal linear gain would be 7.1% / 14.3%; the measured gain is lower, so inference is also limited
  by DDR bandwidth, `clk_tpu_fab` (900 MHz) and other factors;
* `DIV_CLK_TPU` stays `0x00020009` at all three settings; only `TPLL_CSR` changes;
* load average about 1.8 during the tests (launcher stopped); run-to-run deviation < 0.5%;
* temperature 60.2 to 62.3 degC, no warnings/oops in dmesg.

## Notes

- **Not persistent**: a reboot returns to 700 MHz;
- **`rmmod` does not restore the frequency**;
- change the frequency only when the NPU is idle (`clk_tpu` `enable_count = 0`);
- only `clk_tpll` is touched (it feeds only `clk_tpu`);
- too high a frequency may be unstable; power-cycle to recover.

## Build

Requires Linux + a 5.10.4 kernel tree (`EXTRAVERSION=-tag-`) + the device `.config`
+ `riscv64-linux-gnu-gcc`:

```sh
./build.sh /path/to/linux-5.10.4
```

The vermagic must be `5.10.4-tag- preempt mod_unload riscv64`.
