// SPDX-License-Identifier: GPL-2.0+
/*
 * Based on the OpenBouffalo BL808 serial driver.
 * Copyright (C) 2022 Samuel Holland <samuel@sholland.org>
 */

#include <debug_uart.h>
#include <dm.h>
#include <serial.h>
#include <asm/io.h>
#include <linux/bitfield.h>
#include <linux/errno.h>
#include <linux/kernel.h>

#define UART_TX_CFG		0x00
#define UART_RX_CFG		0x04
#define UART_BIT_PRD		0x08
#define UART_DATA_CFG		0x0c
#define UART_SW_MODE		0x1c
#define UART_INT_MASK		0x24
#define UART_INT_CLEAR		0x28
#define UART_INT_ENABLE		0x2c
#define UART_FIFO_CFG0		0x80
#define UART_FIFO_CFG1		0x84
#define UART_FIFO_WDATA		0x88
#define UART_FIFO_RDATA		0x8c

#define UART_ENABLE		BIT(0)
#define UART_TX_FREE_RUN		BIT(2)
#define UART_DATA_BITS		GENMASK(10, 8)
#define UART_TX_STOP_BITS	GENMASK(12, 11)
#define UART_TX_FIFO_COUNT	GENMASK(5, 0)
#define UART_RX_FIFO_COUNT	GENMASK(13, 8)
#define UART_FIFO_SIZE		32

struct bflb_serial_plat {
	void __iomem *base;
	u32 clock;
};

static int bflb_rx_avail(void __iomem *base)
{
	return FIELD_GET(UART_RX_FIFO_COUNT, readl(base + UART_FIFO_CFG1));
}

static int bflb_tx_avail(void __iomem *base)
{
	return FIELD_GET(UART_TX_FIFO_COUNT, readl(base + UART_FIFO_CFG1));
}

static int bflb_setbrg(void __iomem *base, ulong clock, int baud)
{
	ulong divisor;
	u32 period;

	if (!clock || baud <= 0)
		return -EINVAL;

	divisor = DIV_ROUND_CLOSEST(clock, (ulong)baud);
	if (!divisor || divisor > 0x10000)
		return -EINVAL;

	period = divisor - 1;
	writel(period | (period << 16), base + UART_BIT_PRD);
	return 0;
}

static void bflb_init(void __iomem *base)
{
	writel(0, base + UART_TX_CFG);
	writel(0, base + UART_RX_CFG);
	writel(0, base + UART_DATA_CFG);
	writel(0, base + UART_SW_MODE);
	writel(0xfff, base + UART_INT_MASK);
	writel(0, base + UART_INT_ENABLE);
	writel(0xf33, base + UART_INT_CLEAR);
	writel(BIT(2) | BIT(3), base + UART_FIFO_CFG0);
	writel(0, base + UART_FIFO_CFG1);
	writel(FIELD_PREP(UART_DATA_BITS, 7) |
	       FIELD_PREP(UART_TX_STOP_BITS, 1) |
	       UART_TX_FREE_RUN | UART_ENABLE, base + UART_TX_CFG);
	writel(FIELD_PREP(UART_DATA_BITS, 7) | UART_ENABLE, base + UART_RX_CFG);
}

static int bflb_serial_setbrg(struct udevice *dev, int baud)
{
	struct bflb_serial_plat *plat = dev_get_plat(dev);

	return bflb_setbrg(plat->base, plat->clock, baud);
}

static int bflb_serial_getc(struct udevice *dev)
{
	struct bflb_serial_plat *plat = dev_get_plat(dev);

	if (!bflb_rx_avail(plat->base))
		return -EAGAIN;

	return readb(plat->base + UART_FIFO_RDATA);
}

static int bflb_serial_putc(struct udevice *dev, const char ch)
{
	struct bflb_serial_plat *plat = dev_get_plat(dev);

	if (!bflb_tx_avail(plat->base))
		return -EAGAIN;

	writeb(ch, plat->base + UART_FIFO_WDATA);
	return 0;
}

static int bflb_serial_pending(struct udevice *dev, bool input)
{
	struct bflb_serial_plat *plat = dev_get_plat(dev);

	return input ? bflb_rx_avail(plat->base) :
		       UART_FIFO_SIZE - bflb_tx_avail(plat->base);
}

static int bflb_serial_probe(struct udevice *dev)
{
	struct bflb_serial_plat *plat = dev_get_plat(dev);

	bflb_init(plat->base);
	return 0;
}

static int bflb_serial_of_to_plat(struct udevice *dev)
{
	struct bflb_serial_plat *plat = dev_get_plat(dev);
	int ret;

	plat->base = dev_remap_addr(dev);
	if (!plat->base)
		return -EINVAL;
	ret = dev_read_u32(dev, "clock-frequency", &plat->clock);
	if (ret)
		return ret;
	return plat->clock ? 0 : -EINVAL;
}

static const struct dm_serial_ops bflb_serial_ops = {
	.setbrg = bflb_serial_setbrg,
	.getc = bflb_serial_getc,
	.putc = bflb_serial_putc,
	.pending = bflb_serial_pending,
};

static const struct udevice_id bflb_serial_ids[] = {
	{ .compatible = "bflb,bl808-uart" },
	{ }
};

U_BOOT_DRIVER(bflb_serial) = {
	.name = "bflb_serial",
	.id = UCLASS_SERIAL,
	.of_match = bflb_serial_ids,
	.probe = bflb_serial_probe,
	.of_to_plat = bflb_serial_of_to_plat,
	.plat_auto = sizeof(struct bflb_serial_plat),
	.ops = &bflb_serial_ops,
	.flags = DM_FLAG_PRE_RELOC,
};

#ifdef CONFIG_DEBUG_UART_BFLB
static inline void _debug_uart_init(void)
{
	void __iomem *base = (void __iomem *)CONFIG_VAL(DEBUG_UART_BASE);

	bflb_init(base);
	bflb_setbrg(base, CONFIG_DEBUG_UART_CLOCK, CONFIG_BAUDRATE);
}

static inline void _debug_uart_putc(int ch)
{
	void __iomem *base = (void __iomem *)CONFIG_VAL(DEBUG_UART_BASE);

	while (!bflb_tx_avail(base))
		;
	writeb(ch, base + UART_FIFO_WDATA);
}

DEBUG_UART_FUNCS
#endif
