# maixcam_npu — MaixCAM Pro NPU 频率控制

通过内核模块调用 `clk_set_rate()` 修改 NPU 的 PLL（`clk_tpll`），
使 NPU 时钟 `clk_tpu`（= tpll / 分频）工作在指定频率。
适用于 MaixCAM Pro / LicheeRV Nano（SG2002，NPU = TPU），内核 `5.10.4-tag-`。

## 现状

- NPU 默认 **700 MHz**（`clk_tpll` 1400 MHz ÷ 2），PLL 只在启动时配置，运行期无调频接口；
- 内核没有 cpufreq，MaixCDK / MaixPy 只有 `npu_freq()` 读取接口；
- `clk_tpu` 是 mux + 分频，没有 `CLK_SET_RATE_PARENT`，所以**不能**直接对 `clk_tpu` 改频，
  必须改它的父 PLL `clk_tpll`（本工具的做法）。

## 用法

```sh
# 上传
scp prebuilt/npu_freq.ko root@<device>:/root/npu_tool/

# 设为 750 MHz（模块内部换算：tpll = 750 × 2 = 1500 MHz）
insmod /root/npu_tool/npu_freq.ko freq=750000000 delay_ms=1500
dmesg | tail -10

# 验证
cat /sys/kernel/debug/clk/clk_tpu/clk_rate      # 750000000
cat /sys/kernel/debug/clk/clk_tpll/clk_rate     # 1500000000
devmem 0x0300290c 32                            # TPLL_CSR
devmem 0x03002054 32                            # DIV_CLK_TPU

# 恢复 700 MHz
rmmod npu_freq
insmod /root/npu_tool/npu_freq.ko freq=700000000 delay_ms=1500
rmmod npu_freq
```

参数：

| 参数 | 默认 | 说明 |
|---|---|---|
| `freq` | `750000000` | 目标 **NPU** 频率（Hz），模块按当前 tpll/tpu 分频比换算 PLL 频率 |
| `delay_ms` | `5000` | 加载后延迟多久真正改频 |

## Benchmark

```sh
/usr/bin/model_runner --model /usr/bin/mobilenet_v2_rgb_224_int8.cvimodel --count 20 --enable-timer
```

## 实测（MaixCAM Pro，maixcam-bca0，测试时已停掉 launcher）

| NPU 频率 | `TPLL_CSR` | `clk_tpu` | mobilenet_v2（20 次，ms/次） | fps | yolov5s（10 次，ms/次） | fps |
|---|---|---|---|---|---|---|
| 700 MHz（默认） | `0x07708101` | 700000000 | 4.454 / 4.452 / 4.449 | 224.6 | 9.662 / 9.659 | 103.5 |
| 750 MHz | `0x07788101` | 750000000 | 4.289 / 4.307 / 4.285 | 232.9 | 9.195 / 9.197 | 108.7 |
| 800 MHz | `0x07808101` | 800000000 | 4.149 / 4.149 / 4.151 | 241.0 | 8.801 / 8.811 | 113.6 |

* 相对 700 MHz：mobilenet 提升 **3.7%**（750）/ **7.3%**（800）；yolov5s 提升 **5.0%** / **9.7%**；
* 理论线性提升应为 7.1% / 14.3%，实测低于线性，说明推理还受 DDR 带宽、`clk_tpu_fab`（900 MHz）等限制；
* 三种频率下 `DIV_CLK_TPU` 都是 `0x00020009`，只有 `TPLL_CSR` 随 PLL 变化；
* 测试时负载约 1.8（launcher 已停），同一设置下三次 mobilenet 偏差 < 0.5%，重复性很好；
* 温度 60.2 ～ 62.3 °C，dmesg 无 warn/oops。

## 注意

- **不持久**：重启回到 700 MHz；
- **`rmmod` 不恢复频率**；
- 改频时 NPU 必须空闲（`clk_tpu` 的 `enable_count = 0`），推理中改频可能挂死；
- 只改 `clk_tpll`（只喂 `clk_tpu`），不碰其它 PLL；
- 频率过高可能不稳定，异常时断电重启即可恢复。

## 编译

需要 Linux + 5.10.4 内核树（`EXTRAVERSION=-tag-`）+ 设备 `.config` + `riscv64-linux-gnu-gcc`：

```sh
./build.sh /path/to/linux-5.10.4
```

vermagic 必须是 `5.10.4-tag- preempt mod_unload riscv64`。
