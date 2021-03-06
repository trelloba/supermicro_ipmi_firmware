/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2005 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#include "qla_def.h"

#include <linux/delay.h>
#include <asm/uaccess.h>

static uint16_t qla2x00_nvram_request(scsi_qla_host_t *, uint32_t);
static void qla2x00_nv_deselect(scsi_qla_host_t *);
static void qla2x00_nv_write(scsi_qla_host_t *, uint16_t);

/*
 * NVRAM support routines
 */

/**
 * qla2x00_lock_nvram_access() -
 * @ha: HA context
 */
void
qla2x00_lock_nvram_access(scsi_qla_host_t *ha)
{
	uint16_t data;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	if (!IS_QLA2100(ha) && !IS_QLA2200(ha) && !IS_QLA2300(ha)) {
		data = RD_REG_WORD(&reg->nvram);
		while (data & NVR_BUSY) {
			udelay(100);
			data = RD_REG_WORD(&reg->nvram);
		}

		/* Lock resource */
		WRT_REG_WORD(&reg->u.isp2300.host_semaphore, 0x1);
		RD_REG_WORD(&reg->u.isp2300.host_semaphore);
		udelay(5);
		data = RD_REG_WORD(&reg->u.isp2300.host_semaphore);
		while ((data & BIT_0) == 0) {
			/* Lock failed */
			udelay(100);
			WRT_REG_WORD(&reg->u.isp2300.host_semaphore, 0x1);
			RD_REG_WORD(&reg->u.isp2300.host_semaphore);
			udelay(5);
			data = RD_REG_WORD(&reg->u.isp2300.host_semaphore);
		}
	}
}

/**
 * qla2x00_unlock_nvram_access() -
 * @ha: HA context
 */
void
qla2x00_unlock_nvram_access(scsi_qla_host_t *ha)
{
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	if (!IS_QLA2100(ha) && !IS_QLA2200(ha) && !IS_QLA2300(ha)) {
		WRT_REG_WORD(&reg->u.isp2300.host_semaphore, 0);
		RD_REG_WORD(&reg->u.isp2300.host_semaphore);
	}
}

/**
 * qla2x00_get_nvram_word() - Calculates word position in NVRAM and calls the
 *	request routine to get the word from NVRAM.
 * @ha: HA context
 * @addr: Address in NVRAM to read
 *
 * Returns the word read from nvram @addr.
 */
uint16_t
qla2x00_get_nvram_word(scsi_qla_host_t *ha, uint32_t addr)
{
	uint16_t	data;
	uint32_t	nv_cmd;

	nv_cmd = addr << 16;
	nv_cmd |= NV_READ_OP;
	data = qla2x00_nvram_request(ha, nv_cmd);

	return (data);
}

/**
 * qla2x00_write_nvram_word() - Write NVRAM data.
 * @ha: HA context
 * @addr: Address in NVRAM to write
 * @data: word to program
 */
void
qla2x00_write_nvram_word(scsi_qla_host_t *ha, uint32_t addr, uint16_t data)
{
	int count;
	uint16_t word;
	uint32_t nv_cmd;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	qla2x00_nv_write(ha, NVR_DATA_OUT);
	qla2x00_nv_write(ha, 0);
	qla2x00_nv_write(ha, 0);

	for (word = 0; word < 8; word++)
		qla2x00_nv_write(ha, NVR_DATA_OUT);

	qla2x00_nv_deselect(ha);

	/* Write data */
	nv_cmd = (addr << 16) | NV_WRITE_OP;
	nv_cmd |= data;
	nv_cmd <<= 5;
	for (count = 0; count < 27; count++) {
		if (nv_cmd & BIT_31)
			qla2x00_nv_write(ha, NVR_DATA_OUT);
		else
			qla2x00_nv_write(ha, 0);

		nv_cmd <<= 1;
	}

	qla2x00_nv_deselect(ha);

	/* Wait for NVRAM to become ready */
	WRT_REG_WORD(&reg->nvram, NVR_SELECT);
	RD_REG_WORD(&reg->nvram);		/* PCI Posting. */
	do {
		NVRAM_DELAY();
		word = RD_REG_WORD(&reg->nvram);
	} while ((word & NVR_DATA_IN) == 0);

	qla2x00_nv_deselect(ha);

	/* Disable writes */
	qla2x00_nv_write(ha, NVR_DATA_OUT);
	for (count = 0; count < 10; count++)
		qla2x00_nv_write(ha, 0);

	qla2x00_nv_deselect(ha);
}

static int
qla2x00_write_nvram_word_tmo(scsi_qla_host_t *ha, uint32_t addr, uint16_t data,
    uint32_t tmo)
{
	int ret, count;
	uint16_t word;
	uint32_t nv_cmd;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	ret = QLA_SUCCESS;

	qla2x00_nv_write(ha, NVR_DATA_OUT);
	qla2x00_nv_write(ha, 0);
	qla2x00_nv_write(ha, 0);

	for (word = 0; word < 8; word++)
		qla2x00_nv_write(ha, NVR_DATA_OUT);

	qla2x00_nv_deselect(ha);

	/* Write data */
	nv_cmd = (addr << 16) | NV_WRITE_OP;
	nv_cmd |= data;
	nv_cmd <<= 5;
	for (count = 0; count < 27; count++) {
		if (nv_cmd & BIT_31)
			qla2x00_nv_write(ha, NVR_DATA_OUT);
		else
			qla2x00_nv_write(ha, 0);

		nv_cmd <<= 1;
	}

	qla2x00_nv_deselect(ha);

	/* Wait for NVRAM to become ready */
	WRT_REG_WORD(&reg->nvram, NVR_SELECT);
	RD_REG_WORD(&reg->nvram);		/* PCI Posting. */
	do {
		NVRAM_DELAY();
		word = RD_REG_WORD(&reg->nvram);
		if (!--tmo) {
			ret = QLA_FUNCTION_FAILED;
			break;
		}
	} while ((word & NVR_DATA_IN) == 0);

	qla2x00_nv_deselect(ha);

	/* Disable writes */
	qla2x00_nv_write(ha, NVR_DATA_OUT);
	for (count = 0; count < 10; count++)
		qla2x00_nv_write(ha, 0);

	qla2x00_nv_deselect(ha);

	return ret;
}

/**
 * qla2x00_nvram_request() - Sends read command to NVRAM and gets data from
 *	NVRAM.
 * @ha: HA context
 * @nv_cmd: NVRAM command
 *
 * Bit definitions for NVRAM command:
 *
 *	Bit 26     = start bit
 *	Bit 25, 24 = opcode
 *	Bit 23-16  = address
 *	Bit 15-0   = write data
 *
 * Returns the word read from nvram @addr.
 */
static uint16_t
qla2x00_nvram_request(scsi_qla_host_t *ha, uint32_t nv_cmd)
{
	uint8_t		cnt;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;
	uint16_t	data = 0;
	uint16_t	reg_data;

	/* Send command to NVRAM. */
	nv_cmd <<= 5;
	for (cnt = 0; cnt < 11; cnt++) {
		if (nv_cmd & BIT_31)
			qla2x00_nv_write(ha, NVR_DATA_OUT);
		else
			qla2x00_nv_write(ha, 0);
		nv_cmd <<= 1;
	}

	/* Read data from NVRAM. */
	for (cnt = 0; cnt < 16; cnt++) {
		WRT_REG_WORD(&reg->nvram, NVR_SELECT | NVR_CLOCK);
		RD_REG_WORD(&reg->nvram);	/* PCI Posting. */
		NVRAM_DELAY();
		data <<= 1;
		reg_data = RD_REG_WORD(&reg->nvram);
		if (reg_data & NVR_DATA_IN)
			data |= BIT_0;
		WRT_REG_WORD(&reg->nvram, NVR_SELECT);
		RD_REG_WORD(&reg->nvram);	/* PCI Posting. */
		NVRAM_DELAY();
	}

	/* Deselect chip. */
	WRT_REG_WORD(&reg->nvram, NVR_DESELECT);
	RD_REG_WORD(&reg->nvram);		/* PCI Posting. */
	NVRAM_DELAY();

	return (data);
}

/**
 * qla2x00_nv_write() - Clean NVRAM operations.
 * @ha: HA context
 */
static void
qla2x00_nv_deselect(scsi_qla_host_t *ha)
{
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	WRT_REG_WORD(&reg->nvram, NVR_DESELECT);
	RD_REG_WORD(&reg->nvram);		/* PCI Posting. */
	NVRAM_DELAY();
}

/**
 * qla2x00_nv_write() - Prepare for NVRAM read/write operation.
 * @ha: HA context
 * @data: Serial interface selector
 */
static void
qla2x00_nv_write(scsi_qla_host_t *ha, uint16_t data)
{
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	WRT_REG_WORD(&reg->nvram, data | NVR_SELECT | NVR_WRT_ENABLE);
	RD_REG_WORD(&reg->nvram);		/* PCI Posting. */
	NVRAM_DELAY();
	WRT_REG_WORD(&reg->nvram, data | NVR_SELECT| NVR_CLOCK |
	    NVR_WRT_ENABLE);
	RD_REG_WORD(&reg->nvram);		/* PCI Posting. */
	NVRAM_DELAY();
	WRT_REG_WORD(&reg->nvram, data | NVR_SELECT | NVR_WRT_ENABLE);
	RD_REG_WORD(&reg->nvram);		/* PCI Posting. */
	NVRAM_DELAY();
}

/**
 * qla2x00_clear_nvram_protection() -
 * @ha: HA context
 */
static int
qla2x00_clear_nvram_protection(scsi_qla_host_t *ha)
{
	int ret, stat;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;
	uint32_t word;
	uint16_t wprot, wprot_old;

	/* Clear NVRAM write protection. */
	ret = QLA_FUNCTION_FAILED;
	wprot_old = cpu_to_le16(qla2x00_get_nvram_word(ha, 0));
	stat = qla2x00_write_nvram_word_tmo(ha, 0,
	    __constant_cpu_to_le16(0x1234), 100000);
	wprot = cpu_to_le16(qla2x00_get_nvram_word(ha, 0));
	if (stat != QLA_SUCCESS || wprot != __constant_cpu_to_le16(0x1234)) {
		/* Write enable. */
		qla2x00_nv_write(ha, NVR_DATA_OUT);
		qla2x00_nv_write(ha, 0);
		qla2x00_nv_write(ha, 0);
		for (word = 0; word < 8; word++)
			qla2x00_nv_write(ha, NVR_DATA_OUT);

		qla2x00_nv_deselect(ha);

		/* Enable protection register. */
		qla2x00_nv_write(ha, NVR_PR_ENABLE | NVR_DATA_OUT);
		qla2x00_nv_write(ha, NVR_PR_ENABLE);
		qla2x00_nv_write(ha, NVR_PR_ENABLE);
		for (word = 0; word < 8; word++)
			qla2x00_nv_write(ha, NVR_DATA_OUT | NVR_PR_ENABLE);

		qla2x00_nv_deselect(ha);

		/* Clear protection register (ffff is cleared). */
		qla2x00_nv_write(ha, NVR_PR_ENABLE | NVR_DATA_OUT);
		qla2x00_nv_write(ha, NVR_PR_ENABLE | NVR_DATA_OUT);
		qla2x00_nv_write(ha, NVR_PR_ENABLE | NVR_DATA_OUT);
		for (word = 0; word < 8; word++)
			qla2x00_nv_write(ha, NVR_DATA_OUT | NVR_PR_ENABLE);

		qla2x00_nv_deselect(ha);

		/* Wait for NVRAM to become ready. */
		WRT_REG_WORD(&reg->nvram, NVR_SELECT);
		RD_REG_WORD(&reg->nvram);	/* PCI Posting. */
		do {
			NVRAM_DELAY();
			word = RD_REG_WORD(&reg->nvram);
		} while ((word & NVR_DATA_IN) == 0);

		ret = QLA_SUCCESS;
	} else
		qla2x00_write_nvram_word(ha, 0, wprot_old);

	return ret;
}

static void
qla2x00_set_nvram_protection(scsi_qla_host_t *ha, int stat)
{
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;
	uint32_t word;

	if (stat != QLA_SUCCESS)
		return;

	/* Set NVRAM write protection. */
	/* Write enable. */
	qla2x00_nv_write(ha, NVR_DATA_OUT);
	qla2x00_nv_write(ha, 0);
	qla2x00_nv_write(ha, 0);
	for (word = 0; word < 8; word++)
		qla2x00_nv_write(ha, NVR_DATA_OUT);

	qla2x00_nv_deselect(ha);

	/* Enable protection register. */
	qla2x00_nv_write(ha, NVR_PR_ENABLE | NVR_DATA_OUT);
	qla2x00_nv_write(ha, NVR_PR_ENABLE);
	qla2x00_nv_write(ha, NVR_PR_ENABLE);
	for (word = 0; word < 8; word++)
		qla2x00_nv_write(ha, NVR_DATA_OUT | NVR_PR_ENABLE);

	qla2x00_nv_deselect(ha);

	/* Enable protection register. */
	qla2x00_nv_write(ha, NVR_PR_ENABLE | NVR_DATA_OUT);
	qla2x00_nv_write(ha, NVR_PR_ENABLE);
	qla2x00_nv_write(ha, NVR_PR_ENABLE | NVR_DATA_OUT);
	for (word = 0; word < 8; word++)
		qla2x00_nv_write(ha, NVR_PR_ENABLE);

	qla2x00_nv_deselect(ha);

	/* Wait for NVRAM to become ready. */
	WRT_REG_WORD(&reg->nvram, NVR_SELECT);
	RD_REG_WORD(&reg->nvram);		/* PCI Posting. */
	do {
		NVRAM_DELAY();
		word = RD_REG_WORD(&reg->nvram);
	} while ((word & NVR_DATA_IN) == 0);
}


/*****************************************************************************/
/* Flash Manipulation Routines                                               */
/*****************************************************************************/

static inline uint32_t
flash_conf_to_access_addr(uint32_t faddr)
{
	return FARX_ACCESS_FLASH_CONF | faddr;
}

static inline uint32_t
flash_data_to_access_addr(uint32_t faddr)
{
	return FARX_ACCESS_FLASH_DATA | faddr;
}

static inline uint32_t
nvram_conf_to_access_addr(uint32_t naddr)
{
	return FARX_ACCESS_NVRAM_CONF | naddr;
}

static inline uint32_t
nvram_data_to_access_addr(uint32_t naddr)
{
	return FARX_ACCESS_NVRAM_DATA | naddr;
}

uint32_t
qla24xx_read_flash_dword(scsi_qla_host_t *ha, uint32_t addr)
{
	int rval;
	uint32_t cnt, data;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;

	WRT_REG_DWORD(&reg->flash_addr, addr & ~FARX_DATA_FLAG);
	/* Wait for READ cycle to complete. */
	rval = QLA_SUCCESS;
	for (cnt = 3000;
	    (RD_REG_DWORD(&reg->flash_addr) & FARX_DATA_FLAG) == 0 &&
	    rval == QLA_SUCCESS; cnt--) {
		if (cnt)
			udelay(10);
		else
			rval = QLA_FUNCTION_TIMEOUT;
	}

	/* TODO: What happens if we time out? */
	data = 0xDEADDEAD;
	if (rval == QLA_SUCCESS)
		data = RD_REG_DWORD(&reg->flash_data);

	return data;
}

uint32_t *
qla24xx_read_flash_data(scsi_qla_host_t *ha, uint32_t *dwptr, uint32_t faddr,
    uint32_t dwords)
{
	uint32_t i;

	/* Dword reads to flash. */
	for (i = 0; i < dwords; i++, faddr++)
		dwptr[i] = cpu_to_le32(qla24xx_read_flash_dword(ha,
		    flash_data_to_access_addr(faddr)));

	return dwptr;
}

int
qla24xx_write_flash_dword(scsi_qla_host_t *ha, uint32_t addr, uint32_t data)
{
	int rval;
	uint32_t cnt;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;

	WRT_REG_DWORD(&reg->flash_data, data);
	RD_REG_DWORD(&reg->flash_data);		/* PCI Posting. */
	WRT_REG_DWORD(&reg->flash_addr, addr | FARX_DATA_FLAG);
	/* Wait for Write cycle to complete. */
	rval = QLA_SUCCESS;
	for (cnt = 500000; (RD_REG_DWORD(&reg->flash_addr) & FARX_DATA_FLAG) &&
	    rval == QLA_SUCCESS; cnt--) {
		if (cnt)
			udelay(10);
		else
			rval = QLA_FUNCTION_TIMEOUT;
	}
	return rval;
}

void
qla24xx_get_flash_manufacturer(scsi_qla_host_t *ha, uint8_t *man_id,
    uint8_t *flash_id)
{
	uint32_t ids;

	ids = qla24xx_read_flash_dword(ha, flash_data_to_access_addr(0xd03ab));
	*man_id = LSB(ids);
	*flash_id = MSB(ids);
}

int
qla24xx_write_flash_data(scsi_qla_host_t *ha, uint32_t *dwptr, uint32_t faddr,
    uint32_t dwords)
{
	int ret;
	uint32_t liter;
	uint32_t sec_mask, rest_addr, conf_addr;
	uint32_t fdata;
	uint8_t	man_id, flash_id;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;

	ret = QLA_SUCCESS;

	qla24xx_get_flash_manufacturer(ha, &man_id, &flash_id);
	DEBUG9(printk("%s(%ld): Flash man_id=%d flash_id=%d\n", __func__,
	    ha->host_no, man_id, flash_id));

	conf_addr = flash_conf_to_access_addr(0x03d8);
	switch (man_id) {
	case 0xbf: /* STT flash. */
		rest_addr = 0x1fff;
		sec_mask = 0x3e000;
		if (flash_id == 0x80)
			conf_addr = flash_conf_to_access_addr(0x0352);
		break;
	case 0x13: /* ST M25P80. */
		rest_addr = 0x3fff;
		sec_mask = 0x3c000;
		break;
	default:
		/* Default to 64 kb sector size. */
		rest_addr = 0x3fff;
		sec_mask = 0x3c000;
		break;
	}

	/* Enable flash write. */
	WRT_REG_DWORD(&reg->ctrl_status,
	    RD_REG_DWORD(&reg->ctrl_status) | CSRX_FLASH_ENABLE);
	RD_REG_DWORD(&reg->ctrl_status);	/* PCI Posting. */

	/* Disable flash write-protection. */
	qla24xx_write_flash_dword(ha, flash_conf_to_access_addr(0x101), 0);

	do {    /* Loop once to provide quick error exit. */
		for (liter = 0; liter < dwords; liter++, faddr++, dwptr++) {
			/* Are we at the beginning of a sector? */
			if ((faddr & rest_addr) == 0) {
				fdata = (faddr & sec_mask) << 2;
				ret = qla24xx_write_flash_dword(ha, conf_addr,
				    (fdata & 0xff00) |((fdata << 16) &
				    0xff0000) | ((fdata >> 16) & 0xff));
				if (ret != QLA_SUCCESS) {
					DEBUG9(printk("%s(%ld) Unable to flash "
					    "sector: address=%x.\n", __func__,
					    ha->host_no, faddr));
					break;
				}
			}
			ret = qla24xx_write_flash_dword(ha,
			    flash_data_to_access_addr(faddr),
			    cpu_to_le32(*dwptr));
			if (ret != QLA_SUCCESS) {
				DEBUG9(printk("%s(%ld) Unable to program flash "
				    "address=%x data=%x.\n", __func__,
				    ha->host_no, faddr, *dwptr));
				break;
			}
		}
	} while (0);

	/* Enable flash write-protection. */
	qla24xx_write_flash_dword(ha, flash_conf_to_access_addr(0x101), 0x9c);

	/* Disable flash write. */
	WRT_REG_DWORD(&reg->ctrl_status,
	    RD_REG_DWORD(&reg->ctrl_status) & ~CSRX_FLASH_ENABLE);
	RD_REG_DWORD(&reg->ctrl_status);	/* PCI Posting. */

	return ret;
}

uint8_t *
qla2x00_read_nvram_data(scsi_qla_host_t *ha, uint8_t *buf, uint32_t naddr,
    uint32_t bytes)
{
	uint32_t i;
	uint16_t *wptr;

	/* Word reads to NVRAM via registers. */
	wptr = (uint16_t *)buf;
	qla2x00_lock_nvram_access(ha);
	for (i = 0; i < bytes >> 1; i++, naddr++)
		wptr[i] = cpu_to_le16(qla2x00_get_nvram_word(ha,
		    naddr));
	qla2x00_unlock_nvram_access(ha);

	return buf;
}

uint8_t *
qla24xx_read_nvram_data(scsi_qla_host_t *ha, uint8_t *buf, uint32_t naddr,
    uint32_t bytes)
{
	uint32_t i;
	uint32_t *dwptr;

	/* Dword reads to flash. */
	dwptr = (uint32_t *)buf;
	for (i = 0; i < bytes >> 2; i++, naddr++)
		dwptr[i] = cpu_to_le32(qla24xx_read_flash_dword(ha,
		    nvram_data_to_access_addr(naddr)));

	return buf;
}

int
qla2x00_write_nvram_data(scsi_qla_host_t *ha, uint8_t *buf, uint32_t naddr,
    uint32_t bytes)
{
	int ret, stat;
	uint32_t i;
	uint16_t *wptr;

	ret = QLA_SUCCESS;

	qla2x00_lock_nvram_access(ha);

	/* Disable NVRAM write-protection. */
	stat = qla2x00_clear_nvram_protection(ha);

	wptr = (uint16_t *)buf;
	for (i = 0; i < bytes >> 1; i++, naddr++) {
		qla2x00_write_nvram_word(ha, naddr,
		    cpu_to_le16(*wptr));
		wptr++;
	}

	/* Enable NVRAM write-protection. */
	qla2x00_set_nvram_protection(ha, stat);

	qla2x00_unlock_nvram_access(ha);

	return ret;
}

int
qla24xx_write_nvram_data(scsi_qla_host_t *ha, uint8_t *buf, uint32_t naddr,
    uint32_t bytes)
{
	int ret;
	uint32_t i;
	uint32_t *dwptr;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;

	ret = QLA_SUCCESS;

	/* Enable flash write. */
	WRT_REG_DWORD(&reg->ctrl_status,
	    RD_REG_DWORD(&reg->ctrl_status) | CSRX_FLASH_ENABLE);
	RD_REG_DWORD(&reg->ctrl_status);	/* PCI Posting. */

	/* Disable NVRAM write-protection. */
	qla24xx_write_flash_dword(ha, nvram_conf_to_access_addr(0x101),
	    0);
	qla24xx_write_flash_dword(ha, nvram_conf_to_access_addr(0x101),
	    0);

	/* Dword writes to flash. */
	dwptr = (uint32_t *)buf;
	for (i = 0; i < bytes >> 2; i++, naddr++, dwptr++) {
		ret = qla24xx_write_flash_dword(ha,
		    nvram_data_to_access_addr(naddr),
		    cpu_to_le32(*dwptr));
		if (ret != QLA_SUCCESS) {
			DEBUG9(printk("%s(%ld) Unable to program "
			    "nvram address=%x data=%x.\n", __func__,
			    ha->host_no, naddr, *dwptr));
			break;
		}
	}

	/* Enable NVRAM write-protection. */
	qla24xx_write_flash_dword(ha, nvram_conf_to_access_addr(0x101),
	    0x8c);

	/* Disable flash write. */
	WRT_REG_DWORD(&reg->ctrl_status,
	    RD_REG_DWORD(&reg->ctrl_status) & ~CSRX_FLASH_ENABLE);
	RD_REG_DWORD(&reg->ctrl_status);	/* PCI Posting. */

	return ret;
}


static inline void
qla2x00_flip_colors(scsi_qla_host_t *ha, uint16_t *pflags)
{
	if (IS_QLA2322(ha)) {
		/* Flip all colors. */
		if (ha->beacon_color_state == QLA_LED_ALL_ON) {
			/* Turn off. */
			ha->beacon_color_state = 0;
			*pflags = GPIO_LED_ALL_OFF;
		} else {
			/* Turn on. */
			ha->beacon_color_state = QLA_LED_ALL_ON;
			*pflags = GPIO_LED_RGA_ON;
		}
	} else {
		/* Flip green led only. */
		if (ha->beacon_color_state == QLA_LED_GRN_ON) {
			/* Turn off. */
			ha->beacon_color_state = 0;
			*pflags = GPIO_LED_GREEN_OFF_AMBER_OFF;
		} else {
			/* Turn on. */
			ha->beacon_color_state = QLA_LED_GRN_ON;
			*pflags = GPIO_LED_GREEN_ON_AMBER_OFF;
		}
	}
}

void
qla2x00_beacon_blink(struct scsi_qla_host *ha)
{
	uint16_t gpio_enable;
	uint16_t gpio_data;
	uint16_t led_color = 0;
	unsigned long flags;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	if (ha->pio_address)
		reg = (struct device_reg_2xxx __iomem *)ha->pio_address;

	spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Save the Original GPIOE. */
	if (ha->pio_address) {
		gpio_enable = RD_REG_WORD_PIO(&reg->gpioe);
		gpio_data = RD_REG_WORD_PIO(&reg->gpiod);
	} else {
		gpio_enable = RD_REG_WORD(&reg->gpioe);
		gpio_data = RD_REG_WORD(&reg->gpiod);
	}

	/* Set the modified gpio_enable values */
	gpio_enable |= GPIO_LED_MASK;

	if (ha->pio_address) {
		WRT_REG_WORD_PIO(&reg->gpioe, gpio_enable);
	} else {
		WRT_REG_WORD(&reg->gpioe, gpio_enable);
		RD_REG_WORD(&reg->gpioe);
	}

	qla2x00_flip_colors(ha, &led_color);

	/* Clear out any previously set LED color. */
	gpio_data &= ~GPIO_LED_MASK;

	/* Set the new input LED color to GPIOD. */
	gpio_data |= led_color;

	/* Set the modified gpio_data values */
	if (ha->pio_address) {
		WRT_REG_WORD_PIO(&reg->gpiod, gpio_data);
	} else {
		WRT_REG_WORD(&reg->gpiod, gpio_data);
		RD_REG_WORD(&reg->gpiod);
	}

	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

int
qla2x00_beacon_on(struct scsi_qla_host *ha)
{
	uint16_t gpio_enable;
	uint16_t gpio_data;
	unsigned long flags;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	ha->fw_options[1] &= ~FO1_SET_EMPHASIS_SWING;
	ha->fw_options[1] |= FO1_DISABLE_GPIO6_7;

	if (qla2x00_set_fw_options(ha, ha->fw_options) != QLA_SUCCESS) {
		qla_printk(KERN_WARNING, ha,
		    "Unable to update fw options (beacon on).\n");
		return QLA_FUNCTION_FAILED;
	}

	if (ha->pio_address)
		reg = (struct device_reg_2xxx __iomem *)ha->pio_address;

	/* Turn off LEDs. */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	if (ha->pio_address) {
		gpio_enable = RD_REG_WORD_PIO(&reg->gpioe);
		gpio_data = RD_REG_WORD_PIO(&reg->gpiod);
	} else {
		gpio_enable = RD_REG_WORD(&reg->gpioe);
		gpio_data = RD_REG_WORD(&reg->gpiod);
	}
	gpio_enable |= GPIO_LED_MASK;

	/* Set the modified gpio_enable values. */
	if (ha->pio_address) {
		WRT_REG_WORD_PIO(&reg->gpioe, gpio_enable);
	} else {
		WRT_REG_WORD(&reg->gpioe, gpio_enable);
		RD_REG_WORD(&reg->gpioe);
	}

	/* Clear out previously set LED colour. */
	gpio_data &= ~GPIO_LED_MASK;
	if (ha->pio_address) {
		WRT_REG_WORD_PIO(&reg->gpiod, gpio_data);
	} else {
		WRT_REG_WORD(&reg->gpiod, gpio_data);
		RD_REG_WORD(&reg->gpiod);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	/*
	 * Let the per HBA timer kick off the blinking process based on
	 * the following flags. No need to do anything else now.
	 */
	ha->beacon_blink_led = 1;
	ha->beacon_color_state = 0;

	return QLA_SUCCESS;
}

int
qla2x00_beacon_off(struct scsi_qla_host *ha)
{
	int rval = QLA_SUCCESS;

	ha->beacon_blink_led = 0;

	/* Set the on flag so when it gets flipped it will be off. */
	if (IS_QLA2322(ha))
		ha->beacon_color_state = QLA_LED_ALL_ON;
	else
		ha->beacon_color_state = QLA_LED_GRN_ON;

	ha->isp_ops.beacon_blink(ha);	/* This turns green LED off */

	ha->fw_options[1] &= ~FO1_SET_EMPHASIS_SWING;
	ha->fw_options[1] &= ~FO1_DISABLE_GPIO6_7;

	rval = qla2x00_set_fw_options(ha, ha->fw_options);
	if (rval != QLA_SUCCESS)
		qla_printk(KERN_WARNING, ha,
		    "Unable to update fw options (beacon off).\n");
	return rval;
}


static inline void
qla24xx_flip_colors(scsi_qla_host_t *ha, uint16_t *pflags)
{
	/* Flip all colors. */
	if (ha->beacon_color_state == QLA_LED_ALL_ON) {
		/* Turn off. */
		ha->beacon_color_state = 0;
		*pflags = 0;
	} else {
		/* Turn on. */
		ha->beacon_color_state = QLA_LED_ALL_ON;
		*pflags = GPDX_LED_YELLOW_ON | GPDX_LED_AMBER_ON;
	}
}

void
qla24xx_beacon_blink(struct scsi_qla_host *ha)
{
	uint16_t led_color = 0;
	uint32_t gpio_data;
	unsigned long flags;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;

	/* Save the Original GPIOD. */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	gpio_data = RD_REG_DWORD(&reg->gpiod);

	/* Enable the gpio_data reg for update. */
	gpio_data |= GPDX_LED_UPDATE_MASK;

	WRT_REG_DWORD(&reg->gpiod, gpio_data);
	gpio_data = RD_REG_DWORD(&reg->gpiod);

	/* Set the color bits. */
	qla24xx_flip_colors(ha, &led_color);

	/* Clear out any previously set LED color. */
	gpio_data &= ~GPDX_LED_COLOR_MASK;

	/* Set the new input LED color to GPIOD. */
	gpio_data |= led_color;

	/* Set the modified gpio_data values. */
	WRT_REG_DWORD(&reg->gpiod, gpio_data);
	gpio_data = RD_REG_DWORD(&reg->gpiod);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

int
qla24xx_beacon_on(struct scsi_qla_host *ha)
{
	uint32_t gpio_data;
	unsigned long flags;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;

	if (ha->beacon_blink_led == 0) {
		/* Enable firmware for update */
		ha->fw_options[1] |= ADD_FO1_DISABLE_GPIO_LED_CTRL;

		if (qla2x00_set_fw_options(ha, ha->fw_options) != QLA_SUCCESS)
			return QLA_FUNCTION_FAILED;

		if (qla2x00_get_fw_options(ha, ha->fw_options) !=
		    QLA_SUCCESS) {
			qla_printk(KERN_WARNING, ha,
			    "Unable to update fw options (beacon on).\n");
			return QLA_FUNCTION_FAILED;
		}

		spin_lock_irqsave(&ha->hardware_lock, flags);
		gpio_data = RD_REG_DWORD(&reg->gpiod);

		/* Enable the gpio_data reg for update. */
		gpio_data |= GPDX_LED_UPDATE_MASK;
		WRT_REG_DWORD(&reg->gpiod, gpio_data);
		RD_REG_DWORD(&reg->gpiod);

		spin_unlock_irqrestore(&ha->hardware_lock, flags);
	}

	/* So all colors blink together. */
	ha->beacon_color_state = 0;

	/* Let the per HBA timer kick off the blinking process. */
	ha->beacon_blink_led = 1;

	return QLA_SUCCESS;
}

int
qla24xx_beacon_off(struct scsi_qla_host *ha)
{
	uint32_t gpio_data;
	unsigned long flags;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;

	ha->beacon_blink_led = 0;
	ha->beacon_color_state = QLA_LED_ALL_ON;

	ha->isp_ops.beacon_blink(ha);	/* Will flip to all off. */

	/* Give control back to firmware. */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	gpio_data = RD_REG_DWORD(&reg->gpiod);

	/* Disable the gpio_data reg for update. */
	gpio_data &= ~GPDX_LED_UPDATE_MASK;
	WRT_REG_DWORD(&reg->gpiod, gpio_data);
	RD_REG_DWORD(&reg->gpiod);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	ha->fw_options[1] &= ~ADD_FO1_DISABLE_GPIO_LED_CTRL;

	if (qla2x00_set_fw_options(ha, ha->fw_options) != QLA_SUCCESS) {
		qla_printk(KERN_WARNING, ha,
		    "Unable to update fw options (beacon off).\n");
		return QLA_FUNCTION_FAILED;
	}

	if (qla2x00_get_fw_options(ha, ha->fw_options) != QLA_SUCCESS) {
		qla_printk(KERN_WARNING, ha,
		    "Unable to get fw options (beacon off).\n");
		return QLA_FUNCTION_FAILED;
	}

	return QLA_SUCCESS;
}


/*
 * Flash support routines
 */

/**
 * qla2x00_flash_enable() - Setup flash for reading and writing.
 * @ha: HA context
 */
static void
qla2x00_flash_enable(scsi_qla_host_t *ha)
{
	uint16_t data;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	data = RD_REG_WORD(&reg->ctrl_status);
	data |= CSR_FLASH_ENABLE;
	WRT_REG_WORD(&reg->ctrl_status, data);
	RD_REG_WORD(&reg->ctrl_status);		/* PCI Posting. */
}

/**
 * qla2x00_flash_disable() - Disable flash and allow RISC to run.
 * @ha: HA context
 */
static void
qla2x00_flash_disable(scsi_qla_host_t *ha)
{
	uint16_t data;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	data = RD_REG_WORD(&reg->ctrl_status);
	data &= ~(CSR_FLASH_ENABLE);
	WRT_REG_WORD(&reg->ctrl_status, data);
	RD_REG_WORD(&reg->ctrl_status);		/* PCI Posting. */
}

/**
 * qla2x00_read_flash_byte() - Reads a byte from flash
 * @ha: HA context
 * @addr: Address in flash to read
 *
 * A word is read from the chip, but, only the lower byte is valid.
 *
 * Returns the byte read from flash @addr.
 */
static uint8_t
qla2x00_read_flash_byte(scsi_qla_host_t *ha, uint32_t addr)
{
	uint16_t data;
	uint16_t bank_select;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	bank_select = RD_REG_WORD(&reg->ctrl_status);

	if (IS_QLA2322(ha) || IS_QLA6322(ha)) {
		/* Specify 64K address range: */
		/*  clear out Module Select and Flash Address bits [19:16]. */
		bank_select &= ~0xf8;
		bank_select |= addr >> 12 & 0xf0;
		bank_select |= CSR_FLASH_64K_BANK;
		WRT_REG_WORD(&reg->ctrl_status, bank_select);
		RD_REG_WORD(&reg->ctrl_status);	/* PCI Posting. */

		WRT_REG_WORD(&reg->flash_address, (uint16_t)addr);
		data = RD_REG_WORD(&reg->flash_data);

		return (uint8_t)data;
	}

	/* Setup bit 16 of flash address. */
	if ((addr & BIT_16) && ((bank_select & CSR_FLASH_64K_BANK) == 0)) {
		bank_select |= CSR_FLASH_64K_BANK;
		WRT_REG_WORD(&reg->ctrl_status, bank_select);
		RD_REG_WORD(&reg->ctrl_status);	/* PCI Posting. */
	} else if (((addr & BIT_16) == 0) &&
	    (bank_select & CSR_FLASH_64K_BANK)) {
		bank_select &= ~(CSR_FLASH_64K_BANK);
		WRT_REG_WORD(&reg->ctrl_status, bank_select);
		RD_REG_WORD(&reg->ctrl_status);	/* PCI Posting. */
	}

	/* Always perform IO mapped accesses to the FLASH registers. */
	if (ha->pio_address) {
		uint16_t data2;

		reg = (struct device_reg_2xxx __iomem *)ha->pio_address;
		WRT_REG_WORD_PIO(&reg->flash_address, (uint16_t)addr);
		do {
			data = RD_REG_WORD_PIO(&reg->flash_data);
			barrier();
			cpu_relax();
			data2 = RD_REG_WORD_PIO(&reg->flash_data);
		} while (data != data2);
	} else {
		WRT_REG_WORD(&reg->flash_address, (uint16_t)addr);
		data = qla2x00_debounce_register(&reg->flash_data);
	}

	return (uint8_t)data;
}

/**
 * qla2x00_write_flash_byte() - Write a byte to flash
 * @ha: HA context
 * @addr: Address in flash to write
 * @data: Data to write
 */
static void
qla2x00_write_flash_byte(scsi_qla_host_t *ha, uint32_t addr, uint8_t data)
{
	uint16_t bank_select;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	bank_select = RD_REG_WORD(&reg->ctrl_status);
	if (IS_QLA2322(ha) || IS_QLA6322(ha)) {
		/* Specify 64K address range: */
		/*  clear out Module Select and Flash Address bits [19:16]. */
		bank_select &= ~0xf8;
		bank_select |= addr >> 12 & 0xf0;
		bank_select |= CSR_FLASH_64K_BANK;
		WRT_REG_WORD(&reg->ctrl_status, bank_select);
		RD_REG_WORD(&reg->ctrl_status);	/* PCI Posting. */

		WRT_REG_WORD(&reg->flash_address, (uint16_t)addr);
		RD_REG_WORD(&reg->ctrl_status);		/* PCI Posting. */
		WRT_REG_WORD(&reg->flash_data, (uint16_t)data);
		RD_REG_WORD(&reg->ctrl_status);		/* PCI Posting. */

		return;
	}

	/* Setup bit 16 of flash address. */
	if ((addr & BIT_16) && ((bank_select & CSR_FLASH_64K_BANK) == 0)) {
		bank_select |= CSR_FLASH_64K_BANK;
		WRT_REG_WORD(&reg->ctrl_status, bank_select);
		RD_REG_WORD(&reg->ctrl_status);	/* PCI Posting. */
	} else if (((addr & BIT_16) == 0) &&
	    (bank_select & CSR_FLASH_64K_BANK)) {
		bank_select &= ~(CSR_FLASH_64K_BANK);
		WRT_REG_WORD(&reg->ctrl_status, bank_select);
		RD_REG_WORD(&reg->ctrl_status);	/* PCI Posting. */
	}

	/* Always perform IO mapped accesses to the FLASH registers. */
	if (ha->pio_address) {
		reg = (struct device_reg_2xxx __iomem *)ha->pio_address;
		WRT_REG_WORD_PIO(&reg->flash_address, (uint16_t)addr);
		WRT_REG_WORD_PIO(&reg->flash_data, (uint16_t)data);
	} else {
		WRT_REG_WORD(&reg->flash_address, (uint16_t)addr);
		RD_REG_WORD(&reg->ctrl_status);		/* PCI Posting. */
		WRT_REG_WORD(&reg->flash_data, (uint16_t)data);
		RD_REG_WORD(&reg->ctrl_status);		/* PCI Posting. */
	}
}

/**
 * qla2x00_poll_flash() - Polls flash for completion.
 * @ha: HA context
 * @addr: Address in flash to poll
 * @poll_data: Data to be polled
 * @man_id: Flash manufacturer ID
 * @flash_id: Flash ID
 *
 * This function polls the device until bit 7 of what is read matches data
 * bit 7 or until data bit 5 becomes a 1.  If that hapens, the flash ROM timed
 * out (a fatal error).  The flash book recommeds reading bit 7 again after
 * reading bit 5 as a 1.
 *
 * Returns 0 on success, else non-zero.
 */
static int
qla2x00_poll_flash(scsi_qla_host_t *ha, uint32_t addr, uint8_t poll_data,
    uint8_t man_id, uint8_t flash_id)
{
	int status;
	uint8_t flash_data;
	uint32_t cnt;

	status = 1;

	/* Wait for 30 seconds for command to finish. */
	poll_data &= BIT_7;
	for (cnt = 3000000; cnt; cnt--) {
		flash_data = qla2x00_read_flash_byte(ha, addr);
		if ((flash_data & BIT_7) == poll_data) {
			status = 0;
			break;
		}

		if (man_id != 0x40 && man_id != 0xda) {
			if ((flash_data & BIT_5) && cnt > 2)
				cnt = 2;
		}
		udelay(10);
		barrier();
	}
	return status;
}

/**
 * qla2x00_program_flash_address() - Programs a flash address
 * @ha: HA context
 * @addr: Address in flash to program
 * @data: Data to be written in flash
 * @man_id: Flash manufacturer ID
 * @flash_id: Flash ID
 *
 * Returns 0 on success, else non-zero.
 */
static int
qla2x00_program_flash_address(scsi_qla_host_t *ha, uint32_t addr, uint8_t data,
    uint8_t man_id, uint8_t flash_id)
{
	/* Write Program Command Sequence. */
	if (IS_OEM_001(ha)) {
		qla2x00_write_flash_byte(ha, 0xaaa, 0xaa);
		qla2x00_write_flash_byte(ha, 0x555, 0x55);
		qla2x00_write_flash_byte(ha, 0xaaa, 0xa0);
		qla2x00_write_flash_byte(ha, addr, data);
	} else {
		if (man_id == 0xda && flash_id == 0xc1) {
			qla2x00_write_flash_byte(ha, addr, data);
			if (addr & 0x7e)
				return 0;
		} else {
			qla2x00_write_flash_byte(ha, 0x5555, 0xaa);
			qla2x00_write_flash_byte(ha, 0x2aaa, 0x55);
			qla2x00_write_flash_byte(ha, 0x5555, 0xa0);
			qla2x00_write_flash_byte(ha, addr, data);
		}
	}

	udelay(150);

	/* Wait for write to complete. */
	return qla2x00_poll_flash(ha, addr, data, man_id, flash_id);
}

/**
 * qla2x00_erase_flash() - Erase the flash.
 * @ha: HA context
 * @man_id: Flash manufacturer ID
 * @flash_id: Flash ID
 *
 * Returns 0 on success, else non-zero.
 */
static int
qla2x00_erase_flash(scsi_qla_host_t *ha, uint8_t man_id, uint8_t flash_id)
{
	/* Individual Sector Erase Command Sequence */
	if (IS_OEM_001(ha)) {
		qla2x00_write_flash_byte(ha, 0xaaa, 0xaa);
		qla2x00_write_flash_byte(ha, 0x555, 0x55);
		qla2x00_write_flash_byte(ha, 0xaaa, 0x80);
		qla2x00_write_flash_byte(ha, 0xaaa, 0xaa);
		qla2x00_write_flash_byte(ha, 0x555, 0x55);
		qla2x00_write_flash_byte(ha, 0xaaa, 0x10);
	} else {
		qla2x00_write_flash_byte(ha, 0x5555, 0xaa);
		qla2x00_write_flash_byte(ha, 0x2aaa, 0x55);
		qla2x00_write_flash_byte(ha, 0x5555, 0x80);
		qla2x00_write_flash_byte(ha, 0x5555, 0xaa);
		qla2x00_write_flash_byte(ha, 0x2aaa, 0x55);
		qla2x00_write_flash_byte(ha, 0x5555, 0x10);
	}

	udelay(150);

	/* Wait for erase to complete. */
	return qla2x00_poll_flash(ha, 0x00, 0x80, man_id, flash_id);
}

/**
 * qla2x00_erase_flash_sector() - Erase a flash sector.
 * @ha: HA context
 * @addr: Flash sector to erase
 * @sec_mask: Sector address mask
 * @man_id: Flash manufacturer ID
 * @flash_id: Flash ID
 *
 * Returns 0 on success, else non-zero.
 */
static int
qla2x00_erase_flash_sector(scsi_qla_host_t *ha, uint32_t addr,
    uint32_t sec_mask, uint8_t man_id, uint8_t flash_id)
{
	/* Individual Sector Erase Command Sequence */
	qla2x00_write_flash_byte(ha, 0x5555, 0xaa);
	qla2x00_write_flash_byte(ha, 0x2aaa, 0x55);
	qla2x00_write_flash_byte(ha, 0x5555, 0x80);
	qla2x00_write_flash_byte(ha, 0x5555, 0xaa);
	qla2x00_write_flash_byte(ha, 0x2aaa, 0x55);
	if (man_id == 0x1f && flash_id == 0x13)
		qla2x00_write_flash_byte(ha, addr & sec_mask, 0x10);
	else
		qla2x00_write_flash_byte(ha, addr & sec_mask, 0x30);

	udelay(150);

	/* Wait for erase to complete. */
	return qla2x00_poll_flash(ha, addr, 0x80, man_id, flash_id);
}

/**
 * qla2x00_get_flash_manufacturer() - Read manufacturer ID from flash chip.
 * @man_id: Flash manufacturer ID
 * @flash_id: Flash ID
 */
static void
qla2x00_get_flash_manufacturer(scsi_qla_host_t *ha, uint8_t *man_id,
    uint8_t *flash_id)
{
	qla2x00_write_flash_byte(ha, 0x5555, 0xaa);
	qla2x00_write_flash_byte(ha, 0x2aaa, 0x55);
	qla2x00_write_flash_byte(ha, 0x5555, 0x90);
	*man_id = qla2x00_read_flash_byte(ha, 0x0000);
	*flash_id = qla2x00_read_flash_byte(ha, 0x0001);
	qla2x00_write_flash_byte(ha, 0x5555, 0xaa);
	qla2x00_write_flash_byte(ha, 0x2aaa, 0x55);
	qla2x00_write_flash_byte(ha, 0x5555, 0xf0);
}


static inline void
qla2x00_suspend_hba(struct scsi_qla_host *ha)
{
	int cnt;
	unsigned long flags;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	/* Suspend HBA. */
	scsi_block_requests(ha->host);
	ha->isp_ops.disable_intrs(ha);
	set_bit(MBX_UPDATE_FLASH_ACTIVE, &ha->mbx_cmd_flags);

	/* Pause RISC. */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	WRT_REG_WORD(&reg->hccr, HCCR_PAUSE_RISC);
	RD_REG_WORD(&reg->hccr);
	if (IS_QLA2100(ha) || IS_QLA2200(ha) || IS_QLA2300(ha)) {
		for (cnt = 0; cnt < 30000; cnt++) {
			if ((RD_REG_WORD(&reg->hccr) & HCCR_RISC_PAUSE) != 0)
				break;
			udelay(100);
		}
	} else {
		udelay(10);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

static inline void
qla2x00_resume_hba(struct scsi_qla_host *ha)
{
	/* Resume HBA. */
	clear_bit(MBX_UPDATE_FLASH_ACTIVE, &ha->mbx_cmd_flags);
	set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
	qla2xxx_wake_dpc(ha);
	qla2x00_wait_for_hba_online(ha);
	scsi_unblock_requests(ha->host);
}

uint8_t *
qla2x00_read_optrom_data(struct scsi_qla_host *ha, uint8_t *buf,
    uint32_t offset, uint32_t length)
{
	unsigned long flags;
	uint32_t addr, midpoint;
	uint8_t *data;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	/* Suspend HBA. */
	qla2x00_suspend_hba(ha);

	/* Go with read. */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	midpoint = ha->optrom_size / 2;

	qla2x00_flash_enable(ha);
	WRT_REG_WORD(&reg->nvram, 0);
	RD_REG_WORD(&reg->nvram);		/* PCI Posting. */
	for (addr = offset, data = buf; addr < length; addr++, data++) {
		if (addr == midpoint) {
			WRT_REG_WORD(&reg->nvram, NVR_SELECT);
			RD_REG_WORD(&reg->nvram);	/* PCI Posting. */
		}

		*data = qla2x00_read_flash_byte(ha, addr);
	}
	qla2x00_flash_disable(ha);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	/* Resume HBA. */
	qla2x00_resume_hba(ha);

	return buf;
}

int
qla2x00_write_optrom_data(struct scsi_qla_host *ha, uint8_t *buf,
    uint32_t offset, uint32_t length)
{

	int rval;
	unsigned long flags;
	uint8_t man_id, flash_id, sec_number, data;
	uint16_t wd;
	uint32_t addr, liter, sec_mask, rest_addr;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	/* Suspend HBA. */
	qla2x00_suspend_hba(ha);

	rval = QLA_SUCCESS;
	sec_number = 0;

	/* Reset ISP chip. */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	WRT_REG_WORD(&reg->ctrl_status, CSR_ISP_SOFT_RESET);
	pci_read_config_word(ha->pdev, PCI_COMMAND, &wd);

	/* Go with write. */
	qla2x00_flash_enable(ha);
	do {	/* Loop once to provide quick error exit */
		/* Structure of flash memory based on manufacturer */
		if (IS_OEM_001(ha)) {
			/* OEM variant with special flash part. */
			man_id = flash_id = 0;
			rest_addr = 0xffff;
			sec_mask   = 0x10000;
			goto update_flash;
		}
		qla2x00_get_flash_manufacturer(ha, &man_id, &flash_id);
		switch (man_id) {
		case 0x20: /* ST flash. */
			if (flash_id == 0xd2 || flash_id == 0xe3) {
				/*
				 * ST m29w008at part - 64kb sector size with
				 * 32kb,8kb,8kb,16kb sectors at memory address
				 * 0xf0000.
				 */
				rest_addr = 0xffff;
				sec_mask = 0x10000;
				break;   
			}
			/*
			 * ST m29w010b part - 16kb sector size
			 * Default to 16kb sectors
			 */
			rest_addr = 0x3fff;
			sec_mask = 0x1c000;
			break;
		case 0x40: /* Mostel flash. */
			/* Mostel v29c51001 part - 512 byte sector size. */
			rest_addr = 0x1ff;
			sec_mask = 0x1fe00;
			break;
		case 0xbf: /* SST flash. */
			/* SST39sf10 part - 4kb sector size. */
			rest_addr = 0xfff;
			sec_mask = 0x1f000;
			break;
		case 0xda: /* Winbond flash. */
			/* Winbond W29EE011 part - 256 byte sector size. */
			rest_addr = 0x7f;
			sec_mask = 0x1ff80;
			break;
		case 0xc2: /* Macronix flash. */
			/* 64k sector size. */
			if (flash_id == 0x38 || flash_id == 0x4f) {
				rest_addr = 0xffff;
				sec_mask = 0x10000;
				break;
			}
			/* Fall through... */

		case 0x1f: /* Atmel flash. */
			/* 512k sector size. */
			if (flash_id == 0x13) {
				rest_addr = 0x7fffffff;
				sec_mask =   0x80000000;
				break;
			}
			/* Fall through... */

		case 0x01: /* AMD flash. */
			if (flash_id == 0x38 || flash_id == 0x40 ||
			    flash_id == 0x4f) {
				/* Am29LV081 part - 64kb sector size. */
				/* Am29LV002BT part - 64kb sector size. */
				rest_addr = 0xffff;
				sec_mask = 0x10000;
				break;
			} else if (flash_id == 0x3e) {
				/*
				 * Am29LV008b part - 64kb sector size with
				 * 32kb,8kb,8kb,16kb sector at memory address
				 * h0xf0000.
				 */
				rest_addr = 0xffff;
				sec_mask = 0x10000;
				break;
			} else if (flash_id == 0x20 || flash_id == 0x6e) {
				/*
				 * Am29LV010 part or AM29f010 - 16kb sector
				 * size.
				 */
				rest_addr = 0x3fff;
				sec_mask = 0x1c000;
				break;
			} else if (flash_id == 0x6d) {
				/* Am29LV001 part - 8kb sector size. */
				rest_addr = 0x1fff;
				sec_mask = 0x1e000;
				break;
			}
		default:
			/* Default to 16 kb sector size. */
			rest_addr = 0x3fff;
			sec_mask = 0x1c000;
			break;
		}

update_flash:
		if (IS_QLA2322(ha) || IS_QLA6322(ha)) {
			if (qla2x00_erase_flash(ha, man_id, flash_id)) {
				rval = QLA_FUNCTION_FAILED;
				break;
			}
		}

		for (addr = offset, liter = 0; liter < length; liter++,
		    addr++) {
			data = buf[liter];
			/* Are we at the beginning of a sector? */
			if ((addr & rest_addr) == 0) {
				if (IS_QLA2322(ha) || IS_QLA6322(ha)) {
					if (addr >= 0x10000UL) {
						if (((addr >> 12) & 0xf0) &&
						    ((man_id == 0x01 &&
							flash_id == 0x3e) ||
						     (man_id == 0x20 &&
							 flash_id == 0xd2))) {
							sec_number++;
							if (sec_number == 1) {
								rest_addr =
								    0x7fff;
								sec_mask =
								    0x18000;
							} else if (
							    sec_number == 2 ||
							    sec_number == 3) {
								rest_addr =
								    0x1fff;
								sec_mask =
								    0x1e000;
							} else if (
							    sec_number == 4) {
								rest_addr =
								    0x3fff;
								sec_mask =
								    0x1c000;
							}
						}
					}
				} else if (addr == ha->optrom_size / 2) {
					WRT_REG_WORD(&reg->nvram, NVR_SELECT);
					RD_REG_WORD(&reg->nvram);
				}

				if (flash_id == 0xda && man_id == 0xc1) {
					qla2x00_write_flash_byte(ha, 0x5555,
					    0xaa);
					qla2x00_write_flash_byte(ha, 0x2aaa,
					    0x55);
					qla2x00_write_flash_byte(ha, 0x5555,
					    0xa0);
				} else if (!IS_QLA2322(ha) && !IS_QLA6322(ha)) {
					/* Then erase it */
					if (qla2x00_erase_flash_sector(ha,
					    addr, sec_mask, man_id,
					    flash_id)) {
						rval = QLA_FUNCTION_FAILED;
						break;
					}
					if (man_id == 0x01 && flash_id == 0x6d)
						sec_number++;
				}
			}

			if (man_id == 0x01 && flash_id == 0x6d) {
				if (sec_number == 1 &&
				    addr == (rest_addr - 1)) {
					rest_addr = 0x0fff;
					sec_mask   = 0x1f000;
				} else if (sec_number == 3 && (addr & 0x7ffe)) {
					rest_addr = 0x3fff;
					sec_mask   = 0x1c000;
				}
			}

			if (qla2x00_program_flash_address(ha, addr, data,
			    man_id, flash_id)) {
				rval = QLA_FUNCTION_FAILED;
				break;
			}
		}
	} while (0);
	qla2x00_flash_disable(ha);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	/* Resume HBA. */
	qla2x00_resume_hba(ha);

	return rval;
}

uint8_t *
qla24xx_read_optrom_data(struct scsi_qla_host *ha, uint8_t *buf,
    uint32_t offset, uint32_t length)
{
	/* Suspend HBA. */
	scsi_block_requests(ha->host);
	ha->isp_ops.disable_intrs(ha);
	set_bit(MBX_UPDATE_FLASH_ACTIVE, &ha->mbx_cmd_flags);

	/* Go with read. */
	qla24xx_read_flash_data(ha, (uint32_t *)buf, offset >> 2, length >> 2);

	/* Resume HBA. */
	clear_bit(MBX_UPDATE_FLASH_ACTIVE, &ha->mbx_cmd_flags);
	ha->isp_ops.enable_intrs(ha);
	scsi_unblock_requests(ha->host);

	return buf;
}

int
qla24xx_write_optrom_data(struct scsi_qla_host *ha, uint8_t *buf,
    uint32_t offset, uint32_t length)
{
	int rval;

	/* Suspend HBA. */
	scsi_block_requests(ha->host);
	ha->isp_ops.disable_intrs(ha);
	set_bit(MBX_UPDATE_FLASH_ACTIVE, &ha->mbx_cmd_flags);

	/* Go with write. */
	rval = qla24xx_write_flash_data(ha, (uint32_t *)buf, offset >> 2,
	    length >> 2);

	/* Resume HBA -- RISC reset needed. */
	clear_bit(MBX_UPDATE_FLASH_ACTIVE, &ha->mbx_cmd_flags);
	set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
	qla2xxx_wake_dpc(ha);
	qla2x00_wait_for_hba_online(ha);
	scsi_unblock_requests(ha->host);

	return rval;
}
