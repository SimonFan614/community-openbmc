// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019-2020, Intel Corporation.

#include <common.h>
#include <asm/io.h>

static const int timer_irqs[] = {48, 49, 50, 51, 52, 53, 54, 55};
static void (*timer_callback[ARRAY_SIZE(timer_irqs)]) (void *) = {NULL};
static void *cb_cookie[ARRAY_SIZE(timer_irqs)] = {NULL};
#define AST_TIMER_BASE 0x1e782000
/* offsets from AST_TIMER_BASE for each timer */
static const u32 timer_bases[] = {0, 0x10, 0x20, 0x40, 0x50, 0x60, 0x70, 0x80};
#define TIMER_ENABLE 1
#define TIMER_1MHZ_CLK_SEL 2
#define TIMER_ENABLE_IRQ 4
#define TIMER_RESET_BY_WDT 8
#define TIMER_CONTROL 0x30
#define TIMER_INT_CLR 0x34
#define TIMER_RELOAD 0x04
#define TIMER_CONTROL_CLEAR 0x3c

static void timer_irq_handler(void *cookie)
{
	int timer_nr = (int)cookie;

	writel(1 << timer_nr, AST_TIMER_BASE + TIMER_INT_CLR);

	if (timer_callback[timer_nr])
		timer_callback[timer_nr](cb_cookie[timer_nr]);
}

void timer_disable(int n)
{
	u32 tctrl;

	if (n < 0 || n > 7) {
		return;
	}

	irq_free_handler(timer_irqs[n]);
	timer_callback[n] = NULL;
	cb_cookie[n] = NULL;

	tctrl = 0xf << (n * 4);
	writel(tctrl, AST_TIMER_BASE + TIMER_CONTROL_CLEAR);
}

void timer_enable(uint n, u32 interval_us, interrupt_handler_t *handler,
		  void *cookie)
{
	u32 tctrl;

	if (n < 0 || n > 7 || !interval_us)
		return;

	timer_disable(n);

	writel(interval_us, AST_TIMER_BASE + timer_bases[n] + TIMER_RELOAD);

	tctrl = (TIMER_ENABLE | TIMER_1MHZ_CLK_SEL |
		 TIMER_RESET_BY_WDT) << (n * 4) | TIMER_ENABLE_IRQ << (n * 4);

	if (handler) {
		timer_callback[n] = handler;
		cb_cookie[n] = cookie;
	}

	irq_install_handler(timer_irqs[n], timer_irq_handler, (void *)n);

	writel(readl(AST_TIMER_BASE + TIMER_CONTROL) | tctrl,
				AST_TIMER_BASE + TIMER_CONTROL);
}
