// SPDX-License-Identifier: GPL-2.0+

#include <asm/io.h>
#include <init.h>

/* The D0 loader has already initialized PSRAM, UART clocks and pinmux. */
int board_init(void)
{
	/* Leave the MCU -> PLIC SDH forwarding enabled for the Linux SDH
	 * driver. The source is cleared here; Linux owns the SDHCI status.
	 */
	writel(readl((void *)0x20000058) & ~BIT(17), (void *)0x20000058);
	writel(BIT(17), (void *)0x20000060);
	return 0;
}
