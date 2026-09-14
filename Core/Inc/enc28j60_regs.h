#ifndef ENC28J60_REGS_H
#define ENC28J60_REGS_H

/* SPI commands. */
#define ENC28J60_OP_RCR              0x00U
#define ENC28J60_OP_RBM              0x3AU
#define ENC28J60_OP_WCR              0x40U
#define ENC28J60_OP_WBM              0x7AU
#define ENC28J60_OP_BFS              0x80U
#define ENC28J60_OP_BFC              0xA0U
#define ENC28J60_OP_SRC              0xFFU

#define ENC28J60_ADDR_MASK           0x1FU
#define ENC28J60_BANK_MASK           0x60U
#define ENC28J60_MAC_MII_FLAG        0x80U

/* Bank 0. */
#define ENC28J60_ERDPTL              0x00U
#define ENC28J60_ERDPTH              0x01U
#define ENC28J60_EWRPTL              0x02U
#define ENC28J60_EWRPTH              0x03U
#define ENC28J60_ETXSTL              0x04U
#define ENC28J60_ETXSTH              0x05U
#define ENC28J60_ETXNDL              0x06U
#define ENC28J60_ETXNDH              0x07U
#define ENC28J60_ERXSTL              0x08U
#define ENC28J60_ERXSTH              0x09U
#define ENC28J60_ERXNDL              0x0AU
#define ENC28J60_ERXNDH              0x0BU
#define ENC28J60_ERXRDPTL            0x0CU
#define ENC28J60_ERXRDPTH            0x0DU

/* Bank 1. */
#define ENC28J60_ERXFCON             0x38U
#define ENC28J60_EPKTCNT             0x39U

/* Bank 2 MAC/MII registers require a dummy byte on read. */
#define ENC28J60_MACON1              0xC0U
#define ENC28J60_MACON3              0xC2U
#define ENC28J60_MACON4              0xC3U
#define ENC28J60_MABBIPG             0xC4U
#define ENC28J60_MAIPGL              0xC6U
#define ENC28J60_MAIPGH              0xC7U
#define ENC28J60_MAMXFLL             0xCAU
#define ENC28J60_MAMXFLH             0xCBU
#define ENC28J60_MICMD               0xD2U
#define ENC28J60_MIREGADR            0xD4U
#define ENC28J60_MIWRL               0xD6U
#define ENC28J60_MIWRH               0xD7U
#define ENC28J60_MIRDL               0xD8U
#define ENC28J60_MIRDH               0xD9U

/* Bank 3. Register addresses are non-linear; MAADR1 is MAC byte 1
   (bits 47:40) and MAADR6 is MAC byte 6 (bits 7:0). */
#define ENC28J60_MAADR5              0xE0U
#define ENC28J60_MAADR6              0xE1U
#define ENC28J60_MAADR3              0xE2U
#define ENC28J60_MAADR4              0xE3U
#define ENC28J60_MAADR1              0xE4U
#define ENC28J60_MAADR2              0xE5U
#define ENC28J60_MISTAT              0xEAU
#define ENC28J60_EREVID              0x72U

/* Registers common to all banks. */
#define ENC28J60_EIE                 0x1BU
#define ENC28J60_EIR                 0x1CU
#define ENC28J60_ESTAT               0x1DU
#define ENC28J60_ECON2               0x1EU
#define ENC28J60_ECON1               0x1FU

/* PHY registers. */
#define ENC28J60_PHCON1              0x00U
#define ENC28J60_PHSTAT1             0x01U
#define ENC28J60_PHCON2              0x10U
#define ENC28J60_PHSTAT2             0x11U
#define ENC28J60_PHIE                0x12U
#define ENC28J60_PHIR                0x13U

/* Bit definitions. */
#define ENC28J60_ECON1_BSEL0         0x01U
#define ENC28J60_ECON1_BSEL1         0x02U
#define ENC28J60_ECON1_RXEN          0x04U
#define ENC28J60_ECON1_TXRTS         0x08U
#define ENC28J60_ECON1_RXRST         0x40U
#define ENC28J60_ECON1_TXRST         0x80U
#define ENC28J60_ECON2_PKTDEC        0x40U
#define ENC28J60_ECON2_AUTOINC       0x80U

#define ENC28J60_ESTAT_CLKRDY        0x01U
#define ENC28J60_ESTAT_TXABRT        0x02U

#define ENC28J60_EIR_RXERIF          0x01U
#define ENC28J60_EIR_TXERIF          0x02U
#define ENC28J60_EIR_TXIF            0x08U
#define ENC28J60_EIR_LINKIF          0x10U
#define ENC28J60_EIR_PKTIF           0x40U

#define ENC28J60_EIE_RXERIE          0x01U
#define ENC28J60_EIE_TXERIE          0x02U
#define ENC28J60_EIE_LINKIE          0x10U
#define ENC28J60_EIE_PKTIE           0x40U
#define ENC28J60_EIE_INTIE           0x80U

#define ENC28J60_ERXFCON_BCEN        0x01U
#define ENC28J60_ERXFCON_CRCEN       0x20U
#define ENC28J60_ERXFCON_UCEN        0x80U

#define ENC28J60_MACON1_MARXEN       0x01U
#define ENC28J60_MACON1_RXPAUS       0x04U
#define ENC28J60_MACON1_TXPAUS       0x08U
#define ENC28J60_MACON3_FRMLNEN      0x02U
#define ENC28J60_MACON3_TXCRCEN      0x10U
#define ENC28J60_MACON3_PADCFG0      0x20U
#define ENC28J60_MICMD_MIIRD          0x01U
#define ENC28J60_MISTAT_BUSY          0x01U

#define ENC28J60_PHCON1_PDPXMD       0x0100U
#define ENC28J60_PHCON2_HDLDIS       0x0100U
#define ENC28J60_PHSTAT2_LSTAT       0x0400U
#define ENC28J60_PHIE_PGEIE          0x0002U
#define ENC28J60_PHIE_PLNKIE         0x0010U

#define ENC28J60_RX_STATUS_OK        0x0080U

/* 8 KiB Ethernet SRAM layout: low region RX, upper region TX. */
#define ENC28J60_RX_START             0x0000U
#define ENC28J60_RX_END               0x19FFU
#define ENC28J60_TX_START             0x1A00U
#define ENC28J60_TX_END               0x1FFFU
#define ENC28J60_MAX_FRAME_LENGTH     1518U
#define ENC28J60_ETH_CRC_LENGTH       4U
#define ENC28J60_RX_HEADER_LENGTH     6U
#define ENC28J60_TX_STATUS_LENGTH     7U

#define RAW_ETH_TEST_TYPE             0x88B5U

#endif /* ENC28J60_REGS_H */
