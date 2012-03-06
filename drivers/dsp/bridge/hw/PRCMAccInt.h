/*
 * PRCMAccInt.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Copyright (C) 2007 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef _PRCM_ACC_INT_H
#define _PRCM_ACC_INT_H

/* Mappings of level 1 EASI function numbers to function names */

#define EASIL1_PRCMPRCM_CLKCFG_CTRL_VALID_CONFIG_WRITE_CLK_VALID32  \
						(PRCM_BASE_EASIL1 + 349)
#define EASIL1_PRCMCM_FCLKEN1_CORE_READ_REGISTER32	(PRCM_BASE_EASIL1 + 743)
#define EASIL1_PRCMCM_FCLKEN1_COREEN_GPT8_WRITE32	(PRCM_BASE_EASIL1 + 951)
#define EASIL1_PRCMCM_FCLKEN1_COREEN_GPT7_WRITE32	(PRCM_BASE_EASIL1 + 961)
#define EASIL1_PRCMCM_ICLKEN1_CORE_READ_REGISTER32	\
						(PRCM_BASE_EASIL1 + 1087)
#define EASIL1_PRCMCM_ICLKEN1_COREEN_MAILBOXES_WRITE32	\
						(PRCM_BASE_EASIL1 + 1105)
#define EASIL1_PRCMCM_ICLKEN1_COREEN_GPT8_WRITE32	\
						(PRCM_BASE_EASIL1 + 1305)
#define EASIL1_PRCMCM_ICLKEN1_COREEN_GPT7_WRITE32	\
						(PRCM_BASE_EASIL1 + 1315)
#define EASIL1_PRCMCM_CLKSEL1_CORECLKSEL_L3_READ_ISSEL132	\
						(PRCM_BASE_EASIL1 + 2261)
#define EASIL1_PRCMCM_CLKSEL2_CORECLKSEL_GPT8_WRITE32K32	\
						(PRCM_BASE_EASIL1 + 2364)
#define EASIL1_PRCMCM_CLKSEL2_CORECLKSEL_GPT8_WRITE_SYS32	\
						(PRCM_BASE_EASIL1 + 2365)
#define EASIL1_PRCMCM_CLKSEL2_CORECLKSEL_GPT8_WRITE_EXT32	\
						(PRCM_BASE_EASIL1 + 2366)
#define EASIL1_PRCMCM_CLKSEL2_CORECLKSEL_GPT7_WRITE32K32	\
						(PRCM_BASE_EASIL1 + 2380)
#define EASIL1_PRCMCM_CLKSEL2_CORECLKSEL_GPT7_WRITE_SYS32	\
						(PRCM_BASE_EASIL1 + 2381)
#define EASIL1_PRCMCM_CLKSEL2_CORECLKSEL_GPT7_WRITE_EXT32	\
						(PRCM_BASE_EASIL1 + 2382)
#define EASIL1_PRCMCM_CLKSEL2_CORECLKSEL_GPT6_WRITE_SYS32	\
						(PRCM_BASE_EASIL1 + 2397)
#define EASIL1_PRCMCM_CLKSEL2_CORECLKSEL_GPT6_WRITE_EXT32	\
						(PRCM_BASE_EASIL1 + 2398)
#define EASIL1_PRCMCM_CLKSEL2_CORECLKSEL_GPT5_WRITE_SYS32	\
						(PRCM_BASE_EASIL1 + 2413)
#define EASIL1_PRCMCM_CLKSEL2_CORECLKSEL_GPT5_WRITE_EXT32	\
						(PRCM_BASE_EASIL1 + 2414)
#define EASIL1_PRCMCM_CLKSEL1_PLLAPL_LS_CLKIN_READ32	\
						(PRCM_BASE_EASIL1 + 3747)
#define EASIL1_PRCMCM_FCLKEN_DSPEN_DSP_WRITE32	(PRCM_BASE_EASIL1 + 3834)
#define EASIL1_PRCMCM_ICLKEN_DSPEN_DSP_IPI_WRITE32	\
						(PRCM_BASE_EASIL1 + 3846)
#define EASIL1_PRCMCM_IDLEST_DSP_READ_REGISTER32	\
						(PRCM_BASE_EASIL1 + 3850)
#define EASIL1_PRCMCM_IDLEST_DSPST_IPI_READ32	(PRCM_BASE_EASIL1 + 3857)
#define EASIL1_PRCMCM_IDLEST_DSPST_DSP_READ32	(PRCM_BASE_EASIL1 + 3863)
#define EASIL1_PRCMCM_AUTOIDLE_DSPAUTO_DSP_IPI_WRITE32	\
						(PRCM_BASE_EASIL1 + 3877)
#define EASIL1_PRCMCM_CLKSEL_DSPSYNC_DSP_WRITE32	\
						(PRCM_BASE_EASIL1 + 3927)
#define EASIL1_PRCMCM_CLKSEL_DSPCLKSEL_DSP_IF_WRITE32	\
						(PRCM_BASE_EASIL1 + 3941)
#define EASIL1_PRCMCM_CLKSEL_DSPCLKSEL_DSP_WRITE32	\
						(PRCM_BASE_EASIL1 + 3965)
#define EASIL1_PRCMCM_CLKSTCTRL_DSP_AUTOSTATE_DSP_READ32	\
						(PRCM_BASE_EASIL1 + 3987)
#define EASIL1_PRCMCM_CLKSTCTRL_DSP_AUTOSTATE_DSP_WRITE32	\
						(PRCM_BASE_EASIL1 + 3993)
#define EASIL1_PRCMRM_RSTCTRL_DSP_READ_REGISTER32	\
						(PRCM_BASE_EASIL1 + 3997)
#define EASIL1_PRCMRM_RSTCTRL_DSPRST1_DSP_WRITE32	\
						(PRCM_BASE_EASIL1 + 4025)
#define EASIL1_PRCMRM_RSTST_DSP_READ_REGISTER32	(PRCM_BASE_EASIL1 + 4029)
#define EASIL1_PRCMRM_RSTST_DSP_WRITE_REGISTER32	\
						(PRCM_BASE_EASIL1 + 4030)
#define EASIL1_PRCMPM_PWSTCTRL_DSP_FORCE_STATE_WRITE32	\
						(PRCM_BASE_EASIL1 + 4165)
#define EASIL1_PRCMPM_PWSTCTRL_DSP_POWER_STATE_WRITE_RET32	\
						(PRCM_BASE_EASIL1 + 4193)
#define EASIL1_PRCMPM_PWSTST_DSP_READ_REGISTER32	\
						(PRCM_BASE_EASIL1 + 4197)
#define EASIL1_PRCMPM_PWSTST_DSP_IN_TRANSITION_READ32	\
						(PRCM_BASE_EASIL1 + 4198)
#define EASIL1_PRCMPM_PWSTST_DSP_POWER_STATE_ST_GET32	\
						(PRCM_BASE_EASIL1 + 4235)
#define EASIL1_CM_FCLKEN_PER_GPT5_WRITE_REGISTER32	\
						(PRCM_BASE_EASIL1 + 4368)
#define EASIL1_CM_ICLKEN_PER_GPT5_WRITE_REGISTER32	\
						(PRCM_BASE_EASIL1 + 4370)
#define EASIL1_CM_CLKSEL_PER_GPT5_WRITE32K32	(PRCM_BASE_EASIL1 + 4372)
#define EASIL1_CM_CLKSEL_PER_GPT6_WRITE32K32	(PRCM_BASE_EASIL1 + 4373)
#define EASIL1_PRCMCM_CLKSTCTRL_IVA2_WRITE_REGISTER32	\
						(PRCM_BASE_EASIL1 + 4374)
#define EASIL1_PRCMPM_PWSTCTRL_IVA2_POWER_STATE_WRITE_ON32	\
						(PRCM_BASE_EASIL1 + 4375)
#define EASIL1_PRCMPM_PWSTCTRL_IVA2_POWER_STATE_WRITE_OFF32	\
						(PRCM_BASE_EASIL1 + 4376)
#define EASIL1_PRCMPM_PWSTST_IVA2_IN_TRANSITION_READ32	\
						(PRCM_BASE_EASIL1 + 4377)
#define EASIL1_PRCMPM_PWSTST_IVA2_POWER_STATE_ST_GET32	\
						(PRCM_BASE_EASIL1 + 4378)
#define EASIL1_PRCMPM_PWSTST_IVA2_READ_REGISTER32	\
						(PRCM_BASE_EASIL1 + 4379)

/* Register offset address definitions */

#define PRCM_PRCM_CLKCFG_CTRL_OFFSET        (u32)(0x80)
#define PRCM_CM_FCLKEN1_CORE_OFFSET          (u32)(0x200)
#define PRCM_CM_ICLKEN1_CORE_OFFSET          (u32)(0x210)
#define PRCM_CM_CLKSEL2_CORE_OFFSET          (u32)(0x244)
#define PRCM_CM_CLKSEL1_PLL_OFFSET           (u32)(0x540)
#define PRCM_CM_ICLKEN_DSP_OFFSET            (u32)(0x810)
#define PRCM_CM_IDLEST_DSP_OFFSET            (u32)(0x820)
#define PRCM_CM_AUTOIDLE_DSP_OFFSET          (u32)(0x830)
#define PRCM_CM_CLKSEL_DSP_OFFSET            (u32)(0x840)
#define PRCM_CM_CLKSTCTRL_DSP_OFFSET         (u32)(0x848)
#define PRCM_RM_RSTCTRL_DSP_OFFSET           (u32)(0x050)
#define PRCM_RM_RSTST_DSP_OFFSET             (u32)(0x058)
#define PRCM_PM_PWSTCTRL_DSP_OFFSET          (u32)(0x8e0)
#define PRCM_PM_PWSTST_DSP_OFFSET            (u32)(0x8e4)
#define PRCM_PM_PWSTST_IVA2_OFFSET            (u32)(0xE4)
#define PRCM_PM_PWSTCTRL_IVA2_OFFSET          (u32)(0xE0)
#define PRCM_CM_CLKSTCTRL_IVA2_OFFSET         (u32)(0x48)
#define CM_CLKSEL_PER_OFFSET                            (u32)(0x40)

/* Bitfield mask and offset declarations */

#define PRCM_PRCM_CLKCFG_CTRL_VALID_CONFIG_MASK         (u32)(0x1)
#define PRCM_PRCM_CLKCFG_CTRL_VALID_CONFIG_OFFSET       (u32)(0)

#define PRCM_CM_FCLKEN1_CORE_EN_GPT8_MASK               (u32)(0x400)
#define PRCM_CM_FCLKEN1_CORE_EN_GPT8_OFFSET             (u32)(10)

#define PRCM_CM_FCLKEN1_CORE_EN_GPT7_MASK               (u32)(0x200)
#define PRCM_CM_FCLKEN1_CORE_EN_GPT7_OFFSET             (u32)(9)

#define PRCM_CM_ICLKEN1_CORE_EN_GPT8_MASK               (u32)(0x400)
#define PRCM_CM_ICLKEN1_CORE_EN_GPT8_OFFSET             (u32)(10)

#define PRCM_CM_ICLKEN1_CORE_EN_GPT7_MASK               (u32)(0x200)
#define PRCM_CM_ICLKEN1_CORE_EN_GPT7_OFFSET             (u32)(9)

#define PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT8_MASK           (u32)(0xc000)
#define PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT8_OFFSET         (u32)(14)

#define PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT7_MASK           (u32)(0x3000)
#define PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT7_OFFSET         (u32)(12)

#define PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT6_MASK           (u32)(0xc00)
#define PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT6_OFFSET         (u32)(10)

#define PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT5_MASK           (u32)(0x300)
#define PRCM_CM_CLKSEL2_CORE_CLKSEL_GPT5_OFFSET         (u32)(8)

#define PRCM_CM_CLKSEL1_PLL_APL_LS_CLKIN_MASK            (u32)(0x3800000)
#define PRCM_CM_CLKSEL1_PLL_APL_LS_CLKIN_OFFSET          (u32)(23)

#define PRCM_CM_ICLKEN_DSP_EN_DSP_IPI_MASK              (u32)(0x2)
#define PRCM_CM_ICLKEN_DSP_EN_DSP_IPI_OFFSET            (u32)(1)

#define PRCM_CM_IDLEST_DSP_ST_IPI_MASK                  (u32)(0x2)
#define PRCM_CM_IDLEST_DSP_ST_IPI_OFFSET                (u32)(1)

#define PRCM_CM_AUTOIDLE_DSP_AUTO_DSP_IPI_MASK          (u32)(0x2)
#define PRCM_CM_AUTOIDLE_DSP_AUTO_DSP_IPI_OFFSET        (u32)(1)

#define PRCM_CM_CLKSEL_DSP_SYNC_DSP_MASK                (u32)(0x80)
#define PRCM_CM_CLKSEL_DSP_SYNC_DSP_OFFSET              (u32)(7)

#define PRCM_CM_CLKSEL_DSP_CLKSEL_DSP_IF_MASK           (u32)(0x60)
#define PRCM_CM_CLKSEL_DSP_CLKSEL_DSP_IF_OFFSET         (u32)(5)

#define PRCM_CM_CLKSEL_DSP_CLKSEL_DSP_MASK              (u32)(0x1f)
#define PRCM_CM_CLKSEL_DSP_CLKSEL_DSP_OFFSET            (u32)(0)

#define PRCM_CM_CLKSTCTRL_DSP_AUTOSTATE_DSP_MASK        (u32)(0x1)
#define PRCM_CM_CLKSTCTRL_DSP_AUTOSTATE_DSP_OFFSET      (u32)(0)

#define PRCM_PM_PWSTCTRL_DSP_FORCE_STATE_MASK            (u32)(0x40000)
#define PRCM_PM_PWSTCTRL_DSP_FORCE_STATE_OFFSET          (u32)(18)

#define PRCM_PM_PWSTCTRL_DSP_POWER_STATE_MASK            (u32)(0x3)
#define PRCM_PM_PWSTCTRL_DSP_POWER_STATE_OFFSET          (u32)(0)

#define PRCM_PM_PWSTCTRL_IVA2_POWER_STATE_MASK            (u32)(0x3)
#define PRCM_PM_PWSTCTRL_IVA2_POWER_STATE_OFFSET          (u32)(0)

#define PRCM_PM_PWSTST_DSP_IN_TRANSITION_MASK            (u32)(0x100000)
#define PRCM_PM_PWSTST_DSP_IN_TRANSITION_OFFSET          (u32)(20)

#define PRCM_PM_PWSTST_IVA2_IN_TRANSITION_MASK            (u32)(0x100000)
#define PRCM_PM_PWSTST_IVA2_IN_TRANSITION_OFFSET          (u32)(20)

#define PRCM_PM_PWSTST_DSP_POWER_STATE_ST_MASK            (u32)(0x3)
#define PRCM_PM_PWSTST_DSP_POWER_STATE_ST_OFFSET          (u32)(0)

#define PRCM_PM_PWSTST_IVA2_POWER_STATE_ST_MASK            (u32)(0x3)
#define PRCM_PM_PWSTST_IVA2_POWER_STATE_ST_OFFSET          (u32)(0)

#define CM_FCLKEN_PER_OFFSET		(u32)(0x0)
#define CM_FCLKEN_PER_GPT5_OFFSET         (u32)(6)
#define CM_FCLKEN_PER_GPT5_MASK     (u32)(0x40)

#define CM_FCLKEN_PER_GPT6_OFFSET   (u32)(7)
#define CM_FCLKEN_PER_GPT6_MASK      (u32)(0x80)

#define CM_ICLKEN_PER_OFFSET		(u32)(0x10)
#define CM_ICLKEN_PER_GPT5_OFFSET  (u32)(6)
#define CM_ICLKEN_PER_GPT5_MASK     (u32)(0x40)

#define CM_ICLKEN_PER_GPT6_OFFSET  (u32)(7)
#define CM_ICLKEN_PER_GPT6_MASK     (u32)(0x80)

#define CM_CLKSEL_PER_GPT5_OFFSET   (u32)(3)
#define CM_CLKSEL_PER_GPT5_MASK      (u32)(0x8)

#define CM_CLKSEL_PER_GPT6_OFFSET   (u32)(4)
#define CM_CLKSEL_PER_GPT6_MASK       (u32)(0x10)

#define CM_FCLKEN_IVA2_OFFSET		(u32)(0x0)
#define CM_FCLKEN_IVA2_EN_MASK	(u32)(0x1)
#define CM_FCLKEN_IVA2_EN_OFFSET	(u32)(0x0)

#define CM_IDLEST_IVA2_OFFSET 		(u32)(0x20)
#define CM_IDLEST_IVA2_ST_IVA2_MASK (u32) (0x01)
#define CM_IDLEST_IVA2_ST_IVA2_OFFSET (u32) (0x00)

#define CM_FCLKEN1_CORE_OFFSET 	(u32)(0xA00)

#define CM_ICLKEN1_CORE_OFFSET  	(u32)(0xA10)
#define CM_ICLKEN1_CORE_EN_MAILBOXES_MASK  (u32)(0x00000080)	/* bit 7 */
#define CM_ICLKEN1_CORE_EN_MAILBOXES_OFFSET	(u32)(7)

#define CM_CLKSTCTRL_IVA2_OFFSET (u32)(0x0)
#define CM_CLKSTCTRL_IVA2_MASK    (u32)(0x3)

#define PRM_RSTCTRL_IVA2_OFFSET  	(u32)(0x50)
#define PRM_RSTCTRL_IVA2_RST1_MASK	(u32)(0x1)
#define PRM_RSTCTRL_IVA2_RST1_OFFSET	(u32)(0x0)
#define PRM_RSTCTRL_IVA2_RST2_MASK	(u32)(0x2)
#define PRM_RSTCTRL_IVA2_RST2_OFFSET	(u32)(0x1)
#define PRM_RSTCTRL_IVA2_RST3_MASK	(u32)(0x4)
#define PRM_RSTCTRL_IVA2_RST3_OFFSET	(u32)(0x2)

/* The following represent the enumerated values for each bitfield */

enum prcmprcm_clkcfg_ctrl_valid_config_e {
	PRCMPRCM_CLKCFG_CTRL_VALID_CONFIG_UPDATED = 0x0000,
	PRCMPRCM_CLKCFG_CTRL_VALID_CONFIG_CLK_VALID = 0x0001
};

enum prcmcm_clksel2_coreclksel_gpt8e {
	PRCMCM_CLKSEL2_CORECLKSEL_GPT832K = 0x0000,
	PRCMCM_CLKSEL2_CORECLKSEL_GPT8_SYS = 0x0001,
	PRCMCM_CLKSEL2_CORECLKSEL_GPT8_EXT = 0x0002,
	PRCMCM_CLKSEL2_CORECLKSEL_GPT8_RESERVED = 0x0003
};

enum prcmcm_clksel2_coreclksel_gpt7e {
	PRCMCM_CLKSEL2_CORECLKSEL_GPT732K = 0x0000,
	PRCMCM_CLKSEL2_CORECLKSEL_GPT7_SYS = 0x0001,
	PRCMCM_CLKSEL2_CORECLKSEL_GPT7_EXT = 0x0002,
	PRCMCM_CLKSEL2_CORECLKSEL_GPT7_RESERVED = 0x0003
};

enum prcmcm_clksel2_coreclksel_gpt6e {
	PRCMCM_CLKSEL2_CORECLKSEL_GPT632K = 0x0000,
	PRCMCM_CLKSEL2_CORECLKSEL_GPT6_SYS = 0x0001,
	PRCMCM_CLKSEL2_CORECLKSEL_GPT6_EXT = 0x0002,
	PRCMCM_CLKSEL2_CORECLKSEL_GPT6_RESERVED = 0x0003
};

enum prcmcm_clksel2_coreclksel_gpt5e {
	PRCMCM_CLKSEL2_CORECLKSEL_GPT532K = 0x0000,
	PRCMCM_CLKSEL2_CORECLKSEL_GPT5_SYS = 0x0001,
	PRCMCM_CLKSEL2_CORECLKSEL_GPT5_EXT = 0x0002,
	PRCMCM_CLKSEL2_CORECLKSEL_GPT5_RESERVED = 0x0003
};

enum prcmpm_pwstctrl_dsp_power_state_e {
	PRCMPM_PWSTCTRL_DSP_POWER_STATE_ON = 0x0000,
	PRCMPM_PWSTCTRL_DSP_POWER_STATE_RET = 0x0001,
	PRCMPM_PWSTCTRL_DSP_POWER_STATE_RESERVED = 0x0002,
	PRCMPM_PWSTCTRL_DSP_POWER_STATE_OFF = 0x0003
};

enum prcmpm_pwstctrl_iva2_power_state_e {
	PRCMPM_PWSTCTRL_IVA2_POWER_STATE_ON = 0x0003,
	PRCMPM_PWSTCTRL_IVA2_POWER_STATE_RET = 0x0001,
	PRCMPM_PWSTCTRL_IVA2_POWER_STATE_RESERVED = 0x0002,
	PRCMPM_PWSTCTRL_IVA2_POWER_STATE_OFF = 0x0000
};

#endif /* _PRCM_ACC_INT_H */
