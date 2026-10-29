// SPDX-License-Identifier: GPL-2.0
/*
 * npu_freq.c - NPU (TPU) frequency control for Sophgo CV181x / SG2002
 *
 * Approach A: change the TPU PLL (clk_tpll) through the kernel clock
 * framework, so that the NPU clock clk_tpu (= clk_tpll / divider) follows.
 *
 * Clock tree on MaixCAM Pro:
 *
 *     osc 25 MHz -> clk_tpll (TPLL, default 1400 MHz)
 *                       |
 *                       +-> clk_tpu (mux + divider, default /2 = 700 MHz) -> NPU
 *
 * clk_tpu has no CLK_SET_RATE_PARENT, so clk_set_rate() on clk_tpu only changes
 * its divider.  To get a fine-grained frequency the PLL itself has to be tuned;
 * clk_tpu then follows through its divider.
 *
 * The module reads the current tpll/tpu ratio at run time, so the requested
 * value is always the *NPU* frequency:
 *
 *     tpll_target = freq * (clk_tpll_rate / clk_tpu_rate)
 *
 * Usage:
 *     insmod npu_freq.ko freq=750000000 delay_ms=3000
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/workqueue.h>

#define CLK_REG_BASE		0x03002000
#define CLK_REG_SIZE		0x1000
#define TPLL_CSR_OFF		0x90C
#define DIV_CLK_TPU_OFF		0x054

static unsigned long freq = 750000000;
module_param(freq, ulong, 0644);
MODULE_PARM_DESC(freq, "target NPU (clk_tpu) rate in Hz (default 750000000)");

static unsigned int delay_ms = 5000;
module_param(delay_ms, uint, 0644);
MODULE_PARM_DESC(delay_ms, "delay before touching the PLL, in ms (default 5000)");

static struct clk *tpll;
static struct clk *tpu;
static struct delayed_work npu_work;
static void __iomem *regs;

static void npu_work_fn(struct work_struct *w)
{
	unsigned long tpll_before, tpu_before, tpll_after, tpu_after;
	unsigned long pll_target;
	unsigned int div;
	u32 tpll_csr_b = 0, tpll_csr_a = 0, div_reg_b = 0, div_reg_a = 0;
	int ret;

	tpll_before = clk_get_rate(tpll);
	tpu_before = clk_get_rate(tpu);

	div = (tpu_before && tpll_before >= tpu_before) ?
	      (unsigned int)(tpll_before / tpu_before) : 2;
	if (!div)
		div = 1;
	pll_target = freq * div;

	if (regs) {
		tpll_csr_b = readl(regs + TPLL_CSR_OFF);
		div_reg_b = readl(regs + DIV_CLK_TPU_OFF);
		pr_info("npu_freq: TPLL_CSR before = 0x%08x, DIV_CLK_TPU before = 0x%08x\n",
			tpll_csr_b, div_reg_b);
	}

	pr_info("npu_freq: tpll = %lu Hz, tpu = %lu Hz, divider = %u\n",
		tpll_before, tpu_before, div);
	pr_info("npu_freq: requesting NPU %lu Hz -> tpll %lu Hz\n",
		freq, pll_target);

	/* the real change: vendor tpll PLL driver recomputes div and ictrl */
	ret = clk_set_rate(tpll, pll_target);

	tpll_after = clk_get_rate(tpll);
	tpu_after = clk_get_rate(tpu);

	if (regs) {
		tpll_csr_a = readl(regs + TPLL_CSR_OFF);
		div_reg_a = readl(regs + DIV_CLK_TPU_OFF);
		pr_info("npu_freq: TPLL_CSR after  = 0x%08x, DIV_CLK_TPU after  = 0x%08x\n",
			tpll_csr_a, div_reg_a);
	}
	pr_info("npu_freq: clk_set_rate(tpll) = %d, tpll = %lu Hz, tpu = %lu Hz\n",
		ret, tpll_after, tpu_after);
}

static int __init npu_init(void)
{
	tpll = clk_get(NULL, "clk_tpll");
	if (IS_ERR(tpll)) {
		pr_err("npu_freq: clk_get(clk_tpll) failed: %ld\n", PTR_ERR(tpll));
		return PTR_ERR(tpll);
	}
	tpu = clk_get(NULL, "clk_tpu");
	if (IS_ERR(tpu)) {
		pr_err("npu_freq: clk_get(clk_tpu) failed: %ld\n", PTR_ERR(tpu));
		clk_put(tpll);
		return PTR_ERR(tpu);
	}

	regs = ioremap(CLK_REG_BASE, CLK_REG_SIZE);
	if (!regs)
		pr_warn("npu_freq: ioremap(0x%x) failed, register dump disabled\n",
			CLK_REG_BASE);

	pr_info("npu_freq: loaded, tpll = %lu Hz, tpu = %lu Hz, retune in %u ms\n",
		clk_get_rate(tpll), clk_get_rate(tpu), delay_ms);

	INIT_DELAYED_WORK(&npu_work, npu_work_fn);
	schedule_delayed_work(&npu_work, msecs_to_jiffies(delay_ms));
	return 0;
}

static void __exit npu_exit(void)
{
	cancel_delayed_work_sync(&npu_work);
	if (regs)
		iounmap(regs);
	if (!IS_ERR_OR_NULL(tpu))
		clk_put(tpu);
	if (!IS_ERR_OR_NULL(tpll))
		clk_put(tpll);
	pr_info("npu_freq: unloaded (frequency is left as-is)\n");
}

module_init(npu_init);
module_exit(npu_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("maixcam_npu");
MODULE_DESCRIPTION("Set NPU frequency through clk_tpll on CV181x/SG2002");
