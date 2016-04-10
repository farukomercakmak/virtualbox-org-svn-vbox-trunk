/* $Id$ */
/** @file
 * APIC - Advanced Programmable Interrupt Controller.
 */

/*
 * Copyright (C) 2016 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_APIC
#include <VBox/log.h>
#include "APICInternal.h"
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/ssm.h>
#include <VBox/vmm/vm.h>


#ifndef VBOX_DEVICE_STRUCT_TESTCASE
/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The current APIC saved state version. */
#define APIC_SAVED_STATE_VERSION             4
/** The saved state version used by VirtualBox 5.0 and
 *  earlier.  */
#define APIC_SAVED_STATE_VERSION_VBOX_50     3
/** The saved state version used by VirtualBox v3 and earlier.
 * This does not include the config.  */
#define APIC_SAVED_STATE_VERSION_VBOX_30     2
/** Some ancient version... */
#define APIC_SAVED_STATE_VERSION_ANCIENT     1


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Saved state field descriptors for XAPICPAGE. */
static const SSMFIELD g_aXApicPageFields[] =
{
    SSMFIELD_ENTRY( XAPICPAGE, id.u8ApicId),
    SSMFIELD_ENTRY( XAPICPAGE, version.all.u32Version),
    SSMFIELD_ENTRY( XAPICPAGE, tpr.u8Tpr),
    SSMFIELD_ENTRY( XAPICPAGE, apr.u8Apr),
    SSMFIELD_ENTRY( XAPICPAGE, ppr.u8Ppr),
    SSMFIELD_ENTRY( XAPICPAGE, ldr.all.u32Ldr),
    SSMFIELD_ENTRY( XAPICPAGE, dfr.all.u32Dfr),
    SSMFIELD_ENTRY( XAPICPAGE, svr.all.u32Svr),
    SSMFIELD_ENTRY( XAPICPAGE, isr.u[0].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, isr.u[1].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, isr.u[2].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, isr.u[3].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, isr.u[4].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, isr.u[5].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, isr.u[6].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, isr.u[7].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, tmr.u[0].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, tmr.u[1].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, tmr.u[2].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, tmr.u[3].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, tmr.u[4].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, tmr.u[5].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, tmr.u[6].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, tmr.u[7].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, irr.u[0].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, irr.u[1].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, irr.u[2].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, irr.u[3].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, irr.u[4].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, irr.u[5].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, irr.u[6].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, irr.u[7].u32Reg),
    SSMFIELD_ENTRY( XAPICPAGE, esr.all.u32Errors),
    SSMFIELD_ENTRY( XAPICPAGE, icr_lo.all.u32IcrLo),
    SSMFIELD_ENTRY( XAPICPAGE, icr_hi.all.u32IcrHi),
    SSMFIELD_ENTRY( XAPICPAGE, lvt_timer.all.u32LvtTimer),
    SSMFIELD_ENTRY( XAPICPAGE, lvt_thermal.all.u32LvtThermal),
    SSMFIELD_ENTRY( XAPICPAGE, lvt_perf.all.u32LvtPerf),
    SSMFIELD_ENTRY( XAPICPAGE, lvt_lint0.all.u32LvtLint0),
    SSMFIELD_ENTRY( XAPICPAGE, lvt_lint1.all.u32LvtLint1),
    SSMFIELD_ENTRY( XAPICPAGE, lvt_error.all.u32LvtError),
    SSMFIELD_ENTRY( XAPICPAGE, timer_icr.u32InitialCount),
    SSMFIELD_ENTRY( XAPICPAGE, timer_ccr.u32CurrentCount),
    SSMFIELD_ENTRY( XAPICPAGE, timer_dcr.all.u32DivideValue),
    SSMFIELD_ENTRY_TERM()
};

/** Saved state field descriptors for X2APICPAGE. */
static const SSMFIELD g_aX2ApicPageFields[] =
{
    SSMFIELD_ENTRY(X2APICPAGE, id.u32ApicId),
    SSMFIELD_ENTRY(X2APICPAGE, version.all.u32Version),
    SSMFIELD_ENTRY(X2APICPAGE, tpr.u8Tpr),
    SSMFIELD_ENTRY(X2APICPAGE, ppr.u8Ppr),
    SSMFIELD_ENTRY(X2APICPAGE, ldr.u32LogicalApicId),
    SSMFIELD_ENTRY(X2APICPAGE, svr.all.u32Svr),
    SSMFIELD_ENTRY(X2APICPAGE, isr.u[0].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, isr.u[1].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, isr.u[2].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, isr.u[3].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, isr.u[4].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, isr.u[5].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, isr.u[6].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, isr.u[7].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, tmr.u[0].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, tmr.u[1].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, tmr.u[2].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, tmr.u[3].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, tmr.u[4].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, tmr.u[5].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, tmr.u[6].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, tmr.u[7].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, irr.u[0].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, irr.u[1].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, irr.u[2].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, irr.u[3].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, irr.u[4].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, irr.u[5].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, irr.u[6].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, irr.u[7].u32Reg),
    SSMFIELD_ENTRY(X2APICPAGE, esr.all.u32Errors),
    SSMFIELD_ENTRY(X2APICPAGE, icr_lo.all.u32IcrLo),
    SSMFIELD_ENTRY(X2APICPAGE, icr_hi.u32IcrHi),
    SSMFIELD_ENTRY(X2APICPAGE, lvt_timer.all.u32LvtTimer),
    SSMFIELD_ENTRY(X2APICPAGE, lvt_thermal.all.u32LvtThermal),
    SSMFIELD_ENTRY(X2APICPAGE, lvt_perf.all.u32LvtPerf),
    SSMFIELD_ENTRY(X2APICPAGE, lvt_lint0.all.u32LvtLint0),
    SSMFIELD_ENTRY(X2APICPAGE, lvt_lint1.all.u32LvtLint1),
    SSMFIELD_ENTRY(X2APICPAGE, lvt_error.all.u32LvtError),
    SSMFIELD_ENTRY(X2APICPAGE, timer_icr.u32InitialCount),
    SSMFIELD_ENTRY(X2APICPAGE, timer_ccr.u32CurrentCount),
    SSMFIELD_ENTRY(X2APICPAGE, timer_dcr.all.u32DivideValue),
    SSMFIELD_ENTRY_TERM()
};


/**
 * Gets the descriptive APIC mode.
 *
 * @returns The name.
 * @param   enmMode     The xAPIC mode.
 */
static const char *apicGetModeName(APICMODE enmMode)
{
    switch (enmMode)
    {
        case APICMODE_DISABLED:  return "Disabled";
        case APICMODE_XAPIC:     return "xAPIC";
        case APICMODE_X2APIC:    return "x2APIC";
        default:                 break;
    }
    return "Invalid";
}


/**
 * Gets the descriptive destination format name.
 *
 * @returns The destination format name.
 * @param   enmDestFormat       The destination format.
 */
static const char *apicGetDestFormatName(XAPICDESTFORMAT enmDestFormat)
{
    switch (enmDestFormat)
    {
        case XAPICDESTFORMAT_FLAT:      return "Flat";
        case XAPICDESTFORMAT_CLUSTER:   return "Cluster";
        default:                        break;
    }
    return "Invalid";
}


/**
 * Gets the descriptive delivery mode name.
 *
 * @returns The delivery mode name.
 * @param   enmDeliveryMode     The delivery mode.
 */
static const char *apicGetDeliveryModeName(XAPICDELIVERYMODE enmDeliveryMode)
{
    switch (enmDeliveryMode)
    {
        case XAPICDELIVERYMODE_FIXED:        return "Fixed";
        case XAPICDELIVERYMODE_LOWEST_PRIO:  return "Lowest priority";
        case XAPICDELIVERYMODE_SMI:          return "SMI";
        case XAPICDELIVERYMODE_NMI:          return "NMI";
        case XAPICDELIVERYMODE_INIT:         return "INIT";
        case XAPICDELIVERYMODE_STARTUP:      return "SIPI";
        case XAPICDELIVERYMODE_EXTINT:       return "ExtINT";
        default:                             break;
    }
    return "Invalid";
}


/**
 * Gets the descriptive destination mode name.
 *
 * @returns The destination mode name.
 * @param   enmDestMode     The destination mode.
 */
static const char *apicGetDestModeName(XAPICDESTMODE enmDestMode)
{
    switch (enmDestMode)
    {
        case XAPICDESTMODE_PHYSICAL:  return "Physical";
        case XAPICDESTMODE_LOGICAL:   return "Logical";
        default:                      break;
    }
    return "Invalid";
}


/**
 * Gets the descriptive trigger mode name.
 *
 * @returns The trigger mode name.
 * @param   enmTriggerMode     The trigger mode.
 */
static const char *apicGetTriggerModeName(XAPICTRIGGERMODE enmTriggerMode)
{
    switch (enmTriggerMode)
    {
        case XAPICTRIGGERMODE_EDGE:     return "Edge";
        case XAPICTRIGGERMODE_LEVEL:    return "Level";
        default:                        break;
    }
    return "Invalid";
}


/**
 * Gets the destination shorthand name.
 *
 * @returns The destination shorthand name.
 * @param   enmDestShorthand     The destination shorthand.
 */
static const char *apicGetDestShorthandName(XAPICDESTSHORTHAND enmDestShorthand)
{
    switch (enmDestShorthand)
    {
        case XAPICDESTSHORTHAND_NONE:           return "None";
        case XAPICDESTSHORTHAND_SELF:           return "Self";
        case XAPIDDESTSHORTHAND_ALL_INCL_SELF:  return "All including self";
        case XAPICDESTSHORTHAND_ALL_EXCL_SELF:  return "All excluding self";
        default:                                break;
    }
    return "Invalid";
}


/**
 * Gets the timer mode name.
 *
 * @returns The timer mode name.
 * @param   enmTimerMode         The timer mode.
 */
static const char *apicGetTimerModeName(XAPICTIMERMODE enmTimerMode)
{
    switch (enmTimerMode)
    {
        case XAPICTIMERMODE_ONESHOT:        return "One-shot";
        case XAPICTIMERMODE_PERIODIC:       return "Periodic";
        case XAPICTIMERMODE_TSC_DEADLINE:   return "TSC deadline";
        default:                            break;
    }
    return "Invalid";
}


/**
 * Initializes per-VCPU APIC to the state following an INIT reset
 * ("Wait-for-SIPI" state).
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 */
static void apicR3InitIpi(PVMCPU pVCpu)
{
    VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu);
    PXAPICPAGE pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);

    /*
     * See Intel spec. 10.4.7.3 "Local APIC State After an INIT Reset (Wait-for-SIPI State)"
     * and AMD spec 16.3.2 "APIC Registers".
     */
    ASMMemZero32(&pXApicPage->irr,       sizeof(pXApicPage->irr));
    ASMMemZero32(&pXApicPage->isr,       sizeof(pXApicPage->isr));
    ASMMemZero32(&pXApicPage->tmr,       sizeof(pXApicPage->tmr));
    ASMMemZero32(&pXApicPage->icr_hi,    sizeof(pXApicPage->icr_hi));
    ASMMemZero32(&pXApicPage->icr_lo,    sizeof(pXApicPage->icr_lo));
    ASMMemZero32(&pXApicPage->ldr,       sizeof(pXApicPage->ldr));
    ASMMemZero32(&pXApicPage->tpr,       sizeof(pXApicPage->tpr));
    ASMMemZero32(&pXApicPage->timer_icr, sizeof(pXApicPage->timer_icr));
    ASMMemZero32(&pXApicPage->timer_ccr, sizeof(pXApicPage->timer_ccr));
    ASMMemZero32(&pXApicPage->timer_dcr, sizeof(pXApicPage->timer_dcr));

    pXApicPage->dfr.u.u4Model        = XAPICDESTFORMAT_FLAT;
    pXApicPage->dfr.u.u28ReservedMb1 = UINT32_C(0xfffffff);

    /** @todo CMCI. */

    ASMMemZero32(&pXApicPage->lvt_timer, sizeof(pXApicPage->lvt_timer));
    pXApicPage->lvt_timer.u.u1Mask = 1;

#if XAPIC_HARDWARE_VERSION == XAPIC_HARDWARE_VERSION_P4
    ASMMemZero32(&pXApicPage->lvt_thermal, sizeof(pXApicPage->lvt_thermal));
    pXApicPage->lvt_thermal.u.u1Mask = 1;
#endif

    ASMMemZero32(&pXApicPage->lvt_perf, sizeof(pXApicPage->lvt_perf));
    pXApicPage->lvt_perf.u.u1Mask = 1;

    ASMMemZero32(&pXApicPage->lvt_lint0, sizeof(pXApicPage->lvt_lint0));
    pXApicPage->lvt_lint0.u.u1Mask = 1;

    ASMMemZero32(&pXApicPage->lvt_lint1, sizeof(pXApicPage->lvt_lint1));
    pXApicPage->lvt_lint1.u.u1Mask = 1;

    ASMMemZero32(&pXApicPage->lvt_error, sizeof(pXApicPage->lvt_error));
    pXApicPage->lvt_error.u.u1Mask = 1;

    ASMMemZero32(&pXApicPage->svr, sizeof(pXApicPage->svr));
    pXApicPage->svr.u.u8SpuriousVector = 0xff;

    /* The self-IPI register is 0. See Intel spec. 10.12.5.1 "x2APIC States" */
    PX2APICPAGE pX2ApicPage = VMCPU_TO_X2APICPAGE(pVCpu);
    ASMMemZero32(&pX2ApicPage->self_ipi, sizeof(pX2ApicPage->self_ipi));

    /* Clear the posted interrupt bitmaps. */
    PAPICCPU pApicCpu = VMCPU_TO_APICCPU(pVCpu);
    ASMMemZero32(&pApicCpu->ApicPibLevel, sizeof(APICPIB));
    ASMMemZero32(&pApicCpu->pvApicPibR3, sizeof(APICPIB));
}


/**
 * Resets the APIC base MSR.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 */
static void apicR3ResetBaseMsr(PVMCPU pVCpu)
{
    /*
     * Initialize the APIC base MSR. The APIC enable-bit is set upon power-up or reset[1].
     *
     * A Reset (in xAPIC and x2APIC mode) brings up the local APIC in xAPIC mode.
     * An INIT IPI does -not- cause a transition between xAPIC and x2APIC mode[2].
     *
     * [1] See AMD spec. 14.1.3 "Processor Initialization State"
     * [2] See Intel spec. 10.12.5.1 "x2APIC States".
     */
    VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu);
    PAPICCPU pApicCpu = VMCPU_TO_APICCPU(pVCpu);
    pApicCpu->uApicBaseMsr = XAPIC_APICBASE_PHYSADDR
                           | MSR_APICBASE_XAPIC_ENABLE_BIT;
    if (pVCpu->idCpu == 0)
        pApicCpu->uApicBaseMsr |= MSR_APICBASE_BOOTSTRAP_CPU_BIT;
}


/**
 * Initializes per-VCPU APIC to the state following a power-up or hardware
 * reset.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 */
VMMR3_INT_DECL(void) APICR3Reset(PVMCPU pVCpu)
{
    VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu);

#ifdef RT_STRICT
    /* Verify that the initial APIC ID reported via CPUID matches our VMCPU ID assumption. */
    CPUMCPUIDLEAF CpuLeaf;
    int rc = CPUMR3CpuIdGetLeaf(pVCpu->CTX_SUFF(pVM), &CpuLeaf, 1, 0);
    AssertRC(rc);
    Assert(((CpuLeaf.uEbx >> 24) & 0xff) == pVCpu->idCpu);
#endif

    apicR3InitIpi(pVCpu);

    PXAPICPAGE pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);

    /*
     * The APIC version register is read-only, so just initialize it here.
     * It is not clear from the specs, where exactly it is initalized.
     * The version determines the number of LVT entries and size of the APIC ID (8 bits for P4).
     */
#if XAPIC_HARDWARE_VERSION == XAPIC_HARDWARE_VERSION_P4
    pXApicPage->version.u.u8MaxLvtEntry = XAPIC_MAX_LVT_ENTRIES_P4 - 1;
    pXApicPage->version.u.u8Version     = XAPIC_HARDWARE_VERSION_P4;
    AssertCompile(sizeof(pXApicPage->id.u8ApicId) >= XAPIC_APIC_ID_BIT_COUNT_P4 / 8);
#else
# error "Implement Pentium and P6 family APIC architectures"
#endif

    /** @todo It isn't very clear where the default base address is (re)initialized,
     *        atm we do it here in Reset. */
    apicR3ResetBaseMsr(pVCpu);

    /*
     * Initialize the APIC ID register to xAPIC format.
     */
    ASMMemZero32(&pXApicPage->id, sizeof(pXApicPage->id));
    pXApicPage->id.u8ApicId = pVCpu->idCpu;
}


/**
 * Receives an INIT IPI.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 */
VMMR3_INT_DECL(void) APICR3InitIpi(PVMCPU pVCpu)
{
    VMCPU_ASSERT_EMT(pVCpu);
    apicR3InitIpi(pVCpu);
}


/**
 * Helper for dumping an APIC 256-bit sparse register.
 *
 * @param   pApicReg        The APIC 256-bit spare register.
 * @param   pHlp            The debug output helper.
 */
static void apicR3DbgInfo256BitReg(volatile const XAPIC256BITREG *pApicReg, PCDBGFINFOHLP pHlp)
{
    ssize_t const  cFragments = RT_ELEMENTS(pApicReg->u);
    unsigned const cBitsPerFragment = sizeof(pApicReg->u[0].u32Reg) * 8;
    XAPIC256BITREG ApicReg;
    RT_ZERO(ApicReg);

    pHlp->pfnPrintf(pHlp, "    ");
    for (ssize_t i = cFragments - 1; i >= 0; i--)
    {
        uint32_t const uFragment = pApicReg->u[i].u32Reg;
        ApicReg.u[i].u32Reg = uFragment;
        pHlp->pfnPrintf(pHlp, "%08x", uFragment);
    }
    pHlp->pfnPrintf(pHlp, "\n");

    size_t cPending = 0;
    pHlp->pfnPrintf(pHlp, "  Pending\n");
    pHlp->pfnPrintf(pHlp, "   ");
    for (ssize_t i = cFragments - 1; i >= 0; i--)
    {
        uint32_t uFragment = ApicReg.u[i].u32Reg;
        if (uFragment)
        {
            do
            {
                unsigned idxSetBit = ASMBitLastSetU32(uFragment);
                --idxSetBit;
                ASMBitClear(&uFragment, idxSetBit);

                idxSetBit += (i * cBitsPerFragment);
                pHlp->pfnPrintf(pHlp, " %02x", idxSetBit);
                ++cPending;
            } while (uFragment);
        }
    }
    if (!cPending)
        pHlp->pfnPrintf(pHlp, " None");
    pHlp->pfnPrintf(pHlp, "\n");
}


/**
 * Dumps basic APIC state.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   pHlp    The debug output helper.
 */
static void apicR3DbgInfoBasic(PVMCPU pVCpu, PCDBGFINFOHLP pHlp)
{
    PCAPICCPU    pApicCpu    = VMCPU_TO_APICCPU(pVCpu);
    PCXAPICPAGE  pXApicPage  = VMCPU_TO_CXAPICPAGE(pVCpu);
    PCX2APICPAGE pX2ApicPage = VMCPU_TO_CX2APICPAGE(pVCpu);
    bool const   fX2ApicMode = XAPIC_IN_X2APIC_MODE(pVCpu);

    pHlp->pfnPrintf(pHlp, "VCPU[%u] APIC at %#RGp (%s mode):\n", pVCpu->idCpu, MSR_APICBASE_GET_PHYSADDR(pApicCpu->uApicBaseMsr),
                                                                 fX2ApicMode ? "x2APIC" : "xAPIC");
    if (fX2ApicMode)
    {
        pHlp->pfnPrintf(pHlp, "  APIC ID                       = %u (%#x)\n", pX2ApicPage->id.u32ApicId,
                                                                              pX2ApicPage->id.u32ApicId);
    }
    else
        pHlp->pfnPrintf(pHlp, "  APIC ID                       = %u (%#x)\n", pXApicPage->id.u8ApicId, pXApicPage->id.u8ApicId);
    pHlp->pfnPrintf(pHlp, "  Version                       = %#x\n",      pXApicPage->version.all.u32Version);
    pHlp->pfnPrintf(pHlp, "    APIC Version                = %#x\n",      pXApicPage->version.u.u8Version);
    pHlp->pfnPrintf(pHlp, "    Max LVT entries             = %u\n",       pXApicPage->version.u.u8MaxLvtEntry);
    pHlp->pfnPrintf(pHlp, "    EOI Broadcast supression    = %RTbool\n",  pXApicPage->version.u.fEoiBroadcastSupression);
    if (!fX2ApicMode)
        pHlp->pfnPrintf(pHlp, "  APR                           = %u (%#x)\n", pXApicPage->apr.u8Apr, pXApicPage->apr.u8Apr);
    pHlp->pfnPrintf(pHlp, "  TPR                           = %u (%#x)\n", pXApicPage->tpr.u8Tpr, pXApicPage->tpr.u8Tpr);
    pHlp->pfnPrintf(pHlp, "    Task-priority class         = %u\n",       XAPIC_TPR_GET_TP(pXApicPage->tpr.u8Tpr));
    pHlp->pfnPrintf(pHlp, "    Task-priority subclass      = %u\n",       XAPIC_TPR_GET_TP_SUBCLASS(pXApicPage->tpr.u8Tpr));
    pHlp->pfnPrintf(pHlp, "  PPR                           = %u (%#x)\n", pXApicPage->ppr.u8Ppr, pXApicPage->ppr.u8Ppr);
    pHlp->pfnPrintf(pHlp, "    Processor-priority class    = %u\n",       XAPIC_PPR_GET_PP(pXApicPage->ppr.u8Ppr));
    pHlp->pfnPrintf(pHlp, "    Processor-priority subclass = %u\n",       XAPIC_PPR_GET_PP_SUBCLASS(pXApicPage->ppr.u8Ppr));
    if (!fX2ApicMode)
        pHlp->pfnPrintf(pHlp, "  RRD                           = %u (%#x)\n", pXApicPage->rrd.u32Rrd, pXApicPage->rrd.u32Rrd);
    pHlp->pfnPrintf(pHlp, "  LDR                           = %u (%#x)\n", pXApicPage->ldr.all.u32Ldr, pXApicPage->ldr.all.u32Ldr);
    pHlp->pfnPrintf(pHlp, "    Logical APIC ID             = %u\n",       fX2ApicMode ? pX2ApicPage->ldr.u32LogicalApicId
                                                                          : pXApicPage->ldr.u.u8LogicalApicId);
    if (!fX2ApicMode)
    {
        pHlp->pfnPrintf(pHlp, "  DFR                           = %RX32\n", pXApicPage->dfr.all.u32Dfr);
        pHlp->pfnPrintf(pHlp, "    Model                       = %#x (%s)\n", pXApicPage->dfr.u.u4Model,
                        apicGetDestFormatName((XAPICDESTFORMAT)pXApicPage->dfr.u.u4Model));
    }
    pHlp->pfnPrintf(pHlp, "  SVR\n");
    pHlp->pfnPrintf(pHlp, "    Vector                      = %u (%#x)\n", pXApicPage->svr.u.u8SpuriousVector,
                                                                          pXApicPage->svr.u.u8SpuriousVector);
    pHlp->pfnPrintf(pHlp, "    Software Enabled            = %RTbool\n",  RT_BOOL(pXApicPage->svr.u.fApicSoftwareEnable));
    pHlp->pfnPrintf(pHlp, "    Supress EOI broadcast       = %RTbool\n",  RT_BOOL(pXApicPage->svr.u.fSupressEoiBroadcast));
    pHlp->pfnPrintf(pHlp, "  ISR\n");
    apicR3DbgInfo256BitReg(&pXApicPage->isr, pHlp);
    pHlp->pfnPrintf(pHlp, "  TMR\n");
    apicR3DbgInfo256BitReg(&pXApicPage->tmr, pHlp);
    pHlp->pfnPrintf(pHlp, "  IRR\n");
    apicR3DbgInfo256BitReg(&pXApicPage->irr, pHlp);
    pHlp->pfnPrintf(pHlp, "ESR Internal                    = %#x\n",      pApicCpu->uEsrInternal);
    pHlp->pfnPrintf(pHlp, "  ESR                           = %#x\n",      pXApicPage->esr.all.u32Errors);
    pHlp->pfnPrintf(pHlp, "    Redirectable IPI            = %RTbool\n",  pXApicPage->esr.u.fRedirectableIpi);
    pHlp->pfnPrintf(pHlp, "    Send Illegal Vector         = %RTbool\n",  pXApicPage->esr.u.fSendIllegalVector);
    pHlp->pfnPrintf(pHlp, "    Recv Illegal Vector         = %RTbool\n",  pXApicPage->esr.u.fRcvdIllegalVector);
    pHlp->pfnPrintf(pHlp, "    Illegal Register Address    = %RTbool\n",  pXApicPage->esr.u.fIllegalRegAddr);
    pHlp->pfnPrintf(pHlp, "  ICR Low                       = %#x\n",      pXApicPage->icr_lo.all);
    pHlp->pfnPrintf(pHlp, "    Vector                      = %u (%#x)\n", pXApicPage->icr_lo.u.u8Vector,
                                                                          pXApicPage->icr_lo.u.u8Vector);
    pHlp->pfnPrintf(pHlp, "    Delivery Mode               = %#x (%s)\n", pXApicPage->icr_lo.u.u3DeliveryMode,
                    apicGetDeliveryModeName((XAPICDELIVERYMODE)pXApicPage->icr_lo.u.u3DeliveryMode));
    pHlp->pfnPrintf(pHlp, "    Destination Mode            = %#x (%s)\n", pXApicPage->icr_lo.u.u1DestMode,
                    apicGetDestModeName((XAPICDESTMODE)pXApicPage->icr_lo.u.u1DestMode));
    if (!fX2ApicMode)
        pHlp->pfnPrintf(pHlp, "    Delivery Status             = %u\n",       pXApicPage->icr_lo.u.u1DeliveryStatus);
    pHlp->pfnPrintf(pHlp, "    Level                       = %u\n",       pXApicPage->icr_lo.u.u1Level);
    pHlp->pfnPrintf(pHlp, "    Trigger Mode                = %u (%s)\n",  pXApicPage->icr_lo.u.u1TriggerMode,
                    apicGetTriggerModeName((XAPICTRIGGERMODE)pXApicPage->icr_lo.u.u1TriggerMode));
    pHlp->pfnPrintf(pHlp, "    Destination shorthand       = %#x (%s)\n", pXApicPage->icr_lo.u.u2DestShorthand,
                    apicGetDestShorthandName((XAPICDESTSHORTHAND)pXApicPage->icr_lo.u.u2DestShorthand));
    pHlp->pfnPrintf(pHlp, "  ICR High                      = %#x\n",      pXApicPage->icr_hi.all.u32IcrHi);
    pHlp->pfnPrintf(pHlp, "    Destination field/mask      = %#x\n",      fX2ApicMode ? pX2ApicPage->icr_hi.u32IcrHi
                                                                          : pXApicPage->icr_hi.u.u8Dest);
}


/**
 * Helper for dumping the LVT timer.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   pHlp    The debug output helper.
 */
static void apicR3DbgInfoLvtTimer(PVMCPU pVCpu, PCDBGFINFOHLP pHlp)
{
    PCXAPICPAGE pXApicPage = VMCPU_TO_CXAPICPAGE(pVCpu);
    uint32_t const uLvtTimer = pXApicPage->lvt_timer.all.u32LvtTimer;
    pHlp->pfnPrintf(pHlp, "LVT Timer          = %#RX32\n",   uLvtTimer);
    pHlp->pfnPrintf(pHlp, "  Vector           = %u (%#x)\n", pXApicPage->lvt_timer.u.u8Vector);
    pHlp->pfnPrintf(pHlp, "  Delivery status  = %u\n",       pXApicPage->lvt_timer.u.u1DeliveryStatus);
    pHlp->pfnPrintf(pHlp, "  Masked           = %RTbool\n",  XAPIC_LVT_IS_MASKED(uLvtTimer));
    pHlp->pfnPrintf(pHlp, "  Timer Mode       = %#x (%s)\n", pXApicPage->lvt_timer.u.u2TimerMode,
                    apicGetTimerModeName((XAPICTIMERMODE)pXApicPage->lvt_timer.u.u2TimerMode));
    pHlp->pfnPrintf(pHlp, "\n");
}


/**
 * Dumps APIC Local Vector Table (LVT) state.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   pHlp    The debug output helper.
 */
static void apicR3DbgInfoLvt(PVMCPU pVCpu, PCDBGFINFOHLP pHlp)
{
    PCXAPICPAGE pXApicPage = VMCPU_TO_CXAPICPAGE(pVCpu);

    apicR3DbgInfoLvtTimer(pVCpu, pHlp);

#if XAPIC_HARDWARE_VERSION == XAPIC_HARDWARE_VERSION_P4
    uint32_t const uLvtThermal = pXApicPage->lvt_thermal.all.u32LvtThermal;
    pHlp->pfnPrintf(pHlp, "LVT Thermal        = %#RX32)\n",  uLvtThermal);
    pHlp->pfnPrintf(pHlp, "  Vector           = %u (%#x)\n", pXApicPage->lvt_thermal.u.u8Vector, pXApicPage->lvt_thermal.u.u8Vector);
    pHlp->pfnPrintf(pHlp, "  Delivery Mode    = %#x (%s)\n", pXApicPage->lvt_thermal.u.u3DeliveryMode,
                    apicGetDeliveryModeName((XAPICDELIVERYMODE)pXApicPage->lvt_thermal.u.u3DeliveryMode));
    pHlp->pfnPrintf(pHlp, "  Delivery status  = %u\n",       pXApicPage->lvt_thermal.u.u1DeliveryStatus);
    pHlp->pfnPrintf(pHlp, "  Masked           = %RTbool\n",  XAPIC_LVT_IS_MASKED(uLvtThermal));
    pHlp->pfnPrintf(pHlp, "\n");
#endif

    uint32_t const uLvtPerf = pXApicPage->lvt_perf.all.u32LvtPerf;
    pHlp->pfnPrintf(pHlp, "LVT Perf           = %#RX32\n",   uLvtPerf);
    pHlp->pfnPrintf(pHlp, "  Vector           = %u (%#x)\n", pXApicPage->lvt_perf.u.u8Vector, pXApicPage->lvt_perf.u.u8Vector);
    pHlp->pfnPrintf(pHlp, "  Delivery Mode    = %#x (%s)\n", pXApicPage->lvt_perf.u.u3DeliveryMode,
                    apicGetDeliveryModeName((XAPICDELIVERYMODE)pXApicPage->lvt_perf.u.u3DeliveryMode));
    pHlp->pfnPrintf(pHlp, "  Delivery status  = %u\n",       pXApicPage->lvt_perf.u.u1DeliveryStatus);
    pHlp->pfnPrintf(pHlp, "  Masked           = %RTbool\n",  XAPIC_LVT_IS_MASKED(uLvtPerf));
    pHlp->pfnPrintf(pHlp, "\n");

    uint32_t const uLvtLint0 = pXApicPage->lvt_lint0.all.u32LvtLint0;
    pHlp->pfnPrintf(pHlp, "LVT LINT0          = %#RX32\n",   uLvtLint0);
    pHlp->pfnPrintf(pHlp, "  Vector           = %u (%#x)\n", pXApicPage->lvt_lint0.u.u8Vector, pXApicPage->lvt_lint0.u.u8Vector);
    pHlp->pfnPrintf(pHlp, "  Delivery Mode    = %#x (%s)\n", pXApicPage->lvt_lint0.u.u3DeliveryMode,
                    apicGetDeliveryModeName((XAPICDELIVERYMODE)pXApicPage->lvt_lint0.u.u3DeliveryMode));
    pHlp->pfnPrintf(pHlp, "  Delivery status  = %u\n",       pXApicPage->lvt_lint0.u.u1DeliveryStatus);
    pHlp->pfnPrintf(pHlp, "  Pin polarity     = %u\n",       pXApicPage->lvt_lint0.u.u1IntrPolarity);
    pHlp->pfnPrintf(pHlp, "  Remote IRR       = %u\n",       pXApicPage->lvt_lint0.u.u1RemoteIrr);
    pHlp->pfnPrintf(pHlp, "  Trigger Mode     = %u (%s)\n",  pXApicPage->lvt_lint0.u.u1TriggerMode,
                    apicGetTriggerModeName((XAPICTRIGGERMODE)pXApicPage->lvt_lint0.u.u1TriggerMode));
    pHlp->pfnPrintf(pHlp, "  Masked           = %RTbool\n",  XAPIC_LVT_IS_MASKED(uLvtLint0));

    uint32_t const uLvtLint1 = pXApicPage->lvt_lint1.all.u32LvtLint1;
    pHlp->pfnPrintf(pHlp, "LVT LINT1          = %#RX32\n",   uLvtLint1);
    pHlp->pfnPrintf(pHlp, "  Vector           = %u (%#x)\n", pXApicPage->lvt_lint1.u.u8Vector, pXApicPage->lvt_lint1.u.u8Vector);
    pHlp->pfnPrintf(pHlp, "  Delivery Mode    = %#x (%s)\n", pXApicPage->lvt_lint1.u.u3DeliveryMode,
                    apicGetDeliveryModeName((XAPICDELIVERYMODE)pXApicPage->lvt_lint1.u.u3DeliveryMode));
    pHlp->pfnPrintf(pHlp, "  Delivery status  = %u\n",       pXApicPage->lvt_lint1.u.u1DeliveryStatus);
    pHlp->pfnPrintf(pHlp, "  Pin polarity     = %u\n",       pXApicPage->lvt_lint1.u.u1IntrPolarity);
    pHlp->pfnPrintf(pHlp, "  Remote IRR       = %u\n",       pXApicPage->lvt_lint1.u.u1RemoteIrr);
    pHlp->pfnPrintf(pHlp, "  Trigger Mode     = %u (%s)\n",  pXApicPage->lvt_lint1.u.u1TriggerMode,
                    apicGetTriggerModeName((XAPICTRIGGERMODE)pXApicPage->lvt_lint1.u.u1TriggerMode));
    pHlp->pfnPrintf(pHlp, "  Masked           = %RTbool\n",  XAPIC_LVT_IS_MASKED(uLvtLint1));
    pHlp->pfnPrintf(pHlp, "\n");

    uint32_t const uLvtError = pXApicPage->lvt_error.all.u32LvtError;
    pHlp->pfnPrintf(pHlp, "LVT Perf           = %#RX32\n",   uLvtError);
    pHlp->pfnPrintf(pHlp, "  Vector           = %u (%#x)\n", pXApicPage->lvt_error.u.u8Vector, pXApicPage->lvt_error.u.u8Vector);
    pHlp->pfnPrintf(pHlp, "  Delivery status  = %u\n",       pXApicPage->lvt_error.u.u1DeliveryStatus);
    pHlp->pfnPrintf(pHlp, "  Masked           = %RTbool\n",  XAPIC_LVT_IS_MASKED(uLvtError));
    pHlp->pfnPrintf(pHlp, "\n");
}


/**
 * Dumps APIC Timer state.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   pHlp    The debug output helper.
 */
static void apicR3DbgInfoTimer(PVMCPU pVCpu, PCDBGFINFOHLP pHlp)
{
    PCXAPICPAGE pXApicPage = VMCPU_TO_CXAPICPAGE(pVCpu);
    PCAPICCPU   pApicCpu   = VMCPU_TO_APICCPU(pVCpu);

    pHlp->pfnPrintf(pHlp, "Local APIC timer:\n");
    pHlp->pfnPrintf(pHlp, "  ICR              = %#RX32\n", pXApicPage->timer_icr.u32InitialCount);
    pHlp->pfnPrintf(pHlp, "  CCR              = %#RX32\n", pXApicPage->timer_ccr.u32CurrentCount);
    pHlp->pfnPrintf(pHlp, "  DCR              = %#RX32\n", pXApicPage->timer_dcr.all.u32DivideValue);
    pHlp->pfnPrintf(pHlp, "    Timer shift    = %#x\n",    apicGetTimerShift(pXApicPage));
    pHlp->pfnPrintf(pHlp, "  Timer initial TS = %#RU64", pApicCpu->u64TimerInitial);
    pHlp->pfnPrintf(pHlp, "\n");

    apicR3DbgInfoLvtTimer(pVCpu, pHlp);
}


/**
 * @callback_method_impl{FNDBGFHANDLERDEV,
 *      Dumps the APIC state according to given argument for debugging purposes.}
 */
static DECLCALLBACK(void) apicR3DbgInfo(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PVM       pVM   = PDMDevHlpGetVM(pDevIns);
    PVMCPU    pVCpu = VMMGetCpu(pVM);
    Assert(pVCpu);

    if (pszArgs == NULL || !*pszArgs || !strcmp(pszArgs, "basic"))
        apicR3DbgInfoBasic(pVCpu, pHlp);
    else if (!strcmp(pszArgs, "lvt"))
        apicR3DbgInfoLvt(pVCpu, pHlp);
    else if (!strcmp(pszArgs, "timer"))
        apicR3DbgInfoTimer(pVCpu, pHlp);
    else
        pHlp->pfnPrintf(pHlp, "Invalid argument. Recognized arguments are 'basic', 'lvt', 'timer'\n");
}


/**
 * Converts legacy PDMAPICMODE to the new APICMODE enum.
 *
 * @returns The new APIC mode.
 * @param   enmLegacyMode       The legacy mode to convert.
 */
static APICMODE apicR3ConvertFromLegacyApicMode(PDMAPICMODE enmLegacyMode)
{
    switch (enmLegacyMode)
    {
        case PDMAPICMODE_NONE:      return APICMODE_DISABLED;
        case PDMAPICMODE_APIC:      return APICMODE_XAPIC;
        case PDMAPICMODE_X2APIC:    return APICMODE_X2APIC;
        case PDMAPICMODE_INVALID:   return APICMODE_INVALID;
        default:                    break;
    }
    return (APICMODE)enmLegacyMode;
}


/**
 * Converts the new APICMODE enum to the legacy PDMAPICMODE enum.
 *
 * @returns The legacy APIC mode.
 * @param   enmMode       The APIC mode to convert.
 */
static PDMAPICMODE apicR3ConvertToLegacyApicMode(APICMODE enmMode)
{
    switch (enmMode)
    {
        case APICMODE_DISABLED:  return PDMAPICMODE_NONE;
        case APICMODE_XAPIC:     return PDMAPICMODE_APIC;
        case APICMODE_X2APIC:    return PDMAPICMODE_X2APIC;
        case APICMODE_INVALID:   return PDMAPICMODE_INVALID;
        default:                 break;
    }
    return (PDMAPICMODE)enmMode;
}


/**
 * Worker for saving per-VM APIC data.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 * @param   pSSM    The SSM handle.
 */
static int apicR3SaveVMData(PVM pVM, PSSMHANDLE pSSM)
{
    PAPIC pApic = VM_TO_APIC(pVM);
    SSMR3PutU32(pSSM,  pVM->cCpus);
    SSMR3PutBool(pSSM, pApic->fIoApicPresent);
    return SSMR3PutU32(pSSM, apicR3ConvertToLegacyApicMode(pApic->enmOriginalMode));
}


/**
 * @copydoc FNSSMDEVLIVEEXEC
 */
static DECLCALLBACK(int) apicR3LiveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uPass)
{
    PAPICDEV pApicDev = PDMINS_2_DATA(pDevIns, PAPICDEV);
    PVM      pVM      = PDMDevHlpGetVM(pApicDev->pDevInsR3);

    int rc = apicR3SaveVMData(pVM, pSSM);
    AssertRCReturn(rc, rc);
    return VINF_SSM_DONT_CALL_AGAIN;
}


/**
 * @copydoc FNSSMDEVSAVEEXEC
 */
static DECLCALLBACK(int) apicR3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PAPICDEV pApicDev = PDMINS_2_DATA(pDevIns, PAPICDEV);
    PVM      pVM      = PDMDevHlpGetVM(pDevIns);
    PAPIC    pApic    = VM_TO_APIC(pVM);
    AssertReturn(pVM, VERR_INVALID_VM_HANDLE);

    /* Save per-VM data. */
    int rc = apicR3SaveVMData(pVM, pSSM);
    AssertRCReturn(rc, rc);

    /* Save per-VCPU data.*/
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = &pVM->aCpus[idCpu];
        PCAPICCPU pApicCpu = VMCPU_TO_APICCPU(pVCpu);

        SSMR3PutU64(pSSM, pApicCpu->uApicBaseMsr);

        /** @todo  */
    }

    return rc;
}


/**
 * @copydoc FNSSMDEVLOADEXEC
 */
static DECLCALLBACK(int) apicR3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PAPICDEV pApicDev = PDMINS_2_DATA(pDevIns, PAPICDEV);
    PVM      pVM      = PDMDevHlpGetVM(pDevIns);
    PAPIC    pApic    = VM_TO_APIC(pVM);
    AssertReturn(pVM, VERR_INVALID_VM_HANDLE);

    /* Weed out invalid versions. */
    if (    uVersion != APIC_SAVED_STATE_VERSION
        &&  uVersion != APIC_SAVED_STATE_VERSION_VBOX_30
        &&  uVersion != APIC_SAVED_STATE_VERSION_ANCIENT)
    {
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    }

    if (uVersion > APIC_SAVED_STATE_VERSION_VBOX_30)
    {
        /* Verify number of CPUs. */
        uint32_t cCpus;
        int rc = SSMR3GetU32(pSSM, &cCpus);
        AssertRCReturn(rc, rc);
        if (cCpus != pVM->cCpus)
            return SSMR3SetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - cCpus: saved=%u config=%u"), cCpus, pVM->cCpus);

        /* Verify I/O APIC presence. */
        bool fIoApicPresent;
        rc = SSMR3GetBool(pSSM, &fIoApicPresent);
        AssertRCReturn(rc, rc);
        if (fIoApicPresent != pApic->fIoApicPresent)
            return SSMR3SetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - fIoApicPresent: saved=%RTbool config=%RTbool"),
                                    fIoApicPresent, pApic->fIoApicPresent);

        /* Verify configured APIC mode. */
        uint32_t uLegacyApicMode;
        rc = SSMR3GetU32(pSSM, &uLegacyApicMode);
        AssertRCReturn(rc, rc);
        APICMODE const enmApicMode = apicR3ConvertFromLegacyApicMode((PDMAPICMODE)uLegacyApicMode);
        if (enmApicMode != pApic->enmOriginalMode)
            return SSMR3SetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - uApicMode: saved=%#x(%#x) config=%#x(%#x)"),
                                    uLegacyApicMode, enmApicMode, apicR3ConvertToLegacyApicMode(pApic->enmOriginalMode),
                                    pApic->enmOriginalMode);

        if (uVersion == APIC_SAVED_STATE_VERSION)
        {
            /** @todo load any new additional per-VM data. */
        }
    }

    if (uPass != SSM_PASS_FINAL)
        return VINF_SUCCESS;

    int rc = VINF_SUCCESS;
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU   pVCpu    = &pVM->aCpus[idCpu];
        PAPICCPU pApicCpu = VMCPU_TO_APICCPU(pVCpu);

        if (uVersion == APIC_SAVED_STATE_VERSION)
        {
            /** @todo load new per-VCPU data. */
        }
        else
        {
            /** @todo load & translate old per-VCPU data to new APIC code. */
            uint32_t uApicBaseMsrLo;
            SSMR3GetU32(pSSM, &uApicBaseMsrLo);
            pApicCpu->uApicBaseMsr = uApicBaseMsrLo;
        }
    }

    return rc;
}


/**
 * The timer callback.
 *
 * @param   pDevIns      The device instance.
 * @param   pTimer       The timer handle.
 * @param   pvUser       Opaque pointer to the VMCPU.
 *
 * @thread  Any.
 * @remarks Currently this function is invoked on the last EMT, see @c
 *          idTimerCpu in tmR3TimerCallback().  However, the code does -not-
 *          rely on this and is designed to work with being invoked on any
 *          thread.
 */
static DECLCALLBACK(void) apicR3TimerCallback(PPDMDEVINS pDevIns, PTMTIMER pTimer, void *pvUser)
{
    PVMCPU pVCpu = (PVMCPU)pvUser;
    Assert(TMTimerIsLockOwner(pTimer));
    Assert(pVCpu);

    PXAPICPAGE     pXApicPage = VMCPU_TO_XAPICPAGE(pVCpu);
    uint32_t const uLvtTimer  = pXApicPage->lvt_timer.all.u32LvtTimer;
    if (!XAPIC_LVT_IS_MASKED(uLvtTimer))
    {
        uint8_t uVector = XAPIC_LVT_GET_VECTOR(uLvtTimer);
        APICPostInterrupt(pVCpu, uVector, XAPICTRIGGERMODE_EDGE);
    }

    PAPICCPU       pApicCpu     = VMCPU_TO_APICCPU(pVCpu);
    XAPICTIMERMODE enmTimerMode = XAPIC_LVT_GET_TIMER_MODE(uLvtTimer);
    switch (enmTimerMode)
    {
        case XAPICTIMERMODE_PERIODIC:
        {
            /* The initial-count register determines if the periodic timer is re-armed. */
            uint32_t const uInitialCount = pXApicPage->timer_icr.u32InitialCount;
            pXApicPage->timer_ccr.u32CurrentCount = uInitialCount;
            if (uInitialCount)
                APICStartTimer(pApicCpu, uInitialCount);
            break;
        }

        case XAPICTIMERMODE_ONESHOT:
        {
            pXApicPage->timer_ccr.u32CurrentCount = 0;
            break;
        }

        case XAPICTIMERMODE_TSC_DEADLINE:
        {
            /** @todo implement TSC deadline. */
            AssertMsgFailed(("APIC: TSC deadline mode unimplemented\n"));
            break;
        }
    }
}


/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static DECLCALLBACK(void) apicR3Reset(PPDMDEVINS pDevIns)
{
    PAPICDEV pApicDev = PDMINS_2_DATA(pDevIns, PAPICDEV);
    PVM      pVM      = PDMDevHlpGetVM(pDevIns);
    VM_ASSERT_EMT0(pVM);
    VM_ASSERT_IS_NOT_RUNNING(pVM);

    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU   pVCpuDest = &pVM->aCpus[idCpu];
        PAPICCPU pApicCpu  = VMCPU_TO_APICCPU(pVCpuDest);

        int rc = TMTimerLock(pApicCpu->pTimerR3, VERR_IGNORED);
        Assert(rc == VINF_SUCCESS); NOREF(rc);
        TMTimerStop(pApicCpu->pTimerR3);
        TMTimerUnlock(pApicCpu->pTimerR3);

        APICR3Reset(pVCpuDest);

        /* Clear the interrupt pending force flag. */
        APICClearInterruptFF(pVCpuDest, PDMAPICIRQ_HARDWARE);
    }
}


/**
 * @interface_method_impl{PDMDEVREG,pfnRelocate}
 */
static DECLCALLBACK(void) apicR3Relocate(PPDMDEVINS pDevIns, RTGCINTPTR offDelta)
{
    PVM      pVM      = PDMDevHlpGetVM(pDevIns);
    PAPIC    pApic    = VM_TO_APIC(pVM);
    PAPICDEV pApicDev = PDMINS_2_DATA(pDevIns, PAPICDEV);

    LogFlow(("APIC: apicR3Relocate: pDevIns=%p offDelta=%RGi\n", pDevIns, offDelta));

    pApicDev->pDevInsRC   = PDMDEVINS_2_RCPTR(pDevIns);
    pApicDev->pApicHlpRC  = pApicDev->pApicHlpR3->pfnGetRCHelpers(pDevIns);
    pApicDev->pCritSectRC = pApicDev->pApicHlpR3->pfnGetRCCritSect(pDevIns);

    pApic->pApicDevRC     = PDMINS_2_DATA_RCPTR(pDevIns);
    if (pApic->pvApicPibRC)
        pApic->pvApicPibRC = MMHyperR3ToRC(pVM, (void *)pApic->pvApicPibR3);

    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU   pVCpu         = &pVM->aCpus[idCpu];
        PAPICCPU pApicCpu      = VMCPU_TO_APICCPU(pVCpu);
        pApicCpu->pTimerRC     = TMTimerRCPtr(pApicCpu->pTimerR3);

        if (pApicCpu->pvApicPageRC)
            pApicCpu->pvApicPageRC = MMHyperR3ToRC(pVM, (void *)pApicCpu->pvApicPageR3);
        if (pApicCpu->pvApicPibRC)
            pApicCpu->pvApicPibRC  = MMHyperR3ToRC(pVM, (void *)pApicCpu->pvApicPibR3);
    }
}


/**
 * Terminates the APIC state.
 *
 * @param   pVM     The cross context VM structure.
 */
static void apicR3TermState(PVM pVM)
{
    PAPIC pApic = VM_TO_APIC(pVM);
    if (pApic->pvApicPibR3)
    {
        size_t const cPages = pApic->cbApicPib >> PAGE_SHIFT;
        if (cPages == 1)
            SUPR3PageFreeEx((void *)pApic->pvApicPibR3, cPages);
        else
            SUPR3ContFree((void *)pApic->pvApicPibR3, cPages);
        pApic->pvApicPibR3 = NULL;
    }

    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU   pVCpu      = &pVM->aCpus[idCpu];
        PAPICCPU pApicCpu   = VMCPU_TO_APICCPU(pVCpu);
        if (pApicCpu->pvApicPageR3)
        {
            SUPR3PageFreeEx((void *)pApicCpu->pvApicPageR3, 1 /* cPages */);
            pApicCpu->pvApicPageR3 = NULL;
        }
    }
}


/**
 * Initializes the APIC state.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 */
static int apicR3InitState(PVM pVM)
{
    PAPIC pApic = VM_TO_APIC(pVM);
    bool const fNeedGCMapping = !HMIsEnabled(pVM);

    /*
     * Allocate and map the pending-interrupt bitmap (PIB).
     *
     * We allocate all the VCPUs' PIBs contiguously in order to save space as
     * physically contiguous allocations are rounded to a multiple of page size.
     */
    pApic->cbApicPib    = RT_ALIGN_Z(pVM->cCpus * sizeof(APICPIB), PAGE_SIZE);
    size_t const cPages = pApic->cbApicPib >> PAGE_SHIFT;
    Assert(!pApic->pvApicPibR3);
    if (cPages == 1)
    {
        SUPPAGE SupApicPib;
        RT_ZERO(SupApicPib);
        SupApicPib.Phys = NIL_RTHCPHYS;
        int rc = SUPR3PageAllocEx(cPages, 0 /* fFlags */, (void **)&pApic->pvApicPibR3, &pApic->pvApicPibR0, &SupApicPib);
        if (RT_SUCCESS(rc))
        {
            pApic->HCPhysApicPib = SupApicPib.Phys;
            Assert(pApic->HCPhysApicPib != NIL_RTHCPHYS);
            Assert(pApic->pvApicPibR3);
        }
        else
        {
            LogRel(("APIC: Failed to allocate %u bytes for the pending-interrupt bitmap, rc=%Rrc\n", pApic->cbApicPib, rc));
            return rc;
        }
    }
    else
        pApic->pvApicPibR3 = SUPR3ContAlloc(cPages, &pApic->pvApicPibR0, &pApic->HCPhysApicPib);

    if (pApic->pvApicPibR3)
    {
        /* Map the pending-interrupt bitmap (PIB) into GC.  */
        if (fNeedGCMapping)
        {
            int rc = MMR3HyperMapHCPhys(pVM, (void *)pApic->pvApicPibR3, NIL_RTR0PTR, pApic->HCPhysApicPib, pApic->cbApicPib,
                                        "APIC PIB", (PRTGCPTR)&pApic->pvApicPibRC);
            if (RT_FAILURE(rc))
            {
                LogRel(("APIC: Failed to map %u bytes for the pending-interrupt bitmap to GC, rc=%Rrc\n", pApic->cbApicPib, rc));
                apicR3TermState(pVM);
                return rc;
            }
        }

        for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
        {
            PVMCPU   pVCpu    = &pVM->aCpus[idCpu];
            PAPICCPU pApicCpu = VMCPU_TO_APICCPU(pVCpu);

            /* Allocate and map the virtual-APIC page. */
            SUPPAGE SupApicPage;
            RT_ZERO(SupApicPage);
            SupApicPage.Phys = NIL_RTHCPHYS;

            pApicCpu->cbApicPage = sizeof(XAPICPAGE);
            AssertCompile(sizeof(XAPICPAGE) == PAGE_SIZE);

            Assert(!pApicCpu->pvApicPageR3);
            int rc = SUPR3PageAllocEx(1 /* cPages */, 0 /* fFlags */, (void **)&pApicCpu->pvApicPageR3, &pApicCpu->pvApicPageR0,
                                      &SupApicPage);
            if (RT_SUCCESS(rc))
            {
                pApicCpu->HCPhysApicPage = SupApicPage.Phys;
                Assert(pApicCpu->HCPhysApicPage != NIL_RTHCPHYS);

                /* Map the virtual-APIC page into GC. */
                if (fNeedGCMapping)
                {
                    rc = MMR3HyperMapHCPhys(pVM, (void *)pApicCpu->pvApicPageR3, NIL_RTR0PTR, pApicCpu->HCPhysApicPage,
                                            pApicCpu->cbApicPage, "APIC", (PRTGCPTR)&pApicCpu->pvApicPageRC);
                    if (RT_FAILURE(rc))
                    {
                        LogRel(("APIC%u: Failed to map %u bytes for the virtual-APIC page to GC, rc=%Rrc", idCpu,
                                pApicCpu->cbApicPage, rc));
                        apicR3TermState(pVM);
                        return rc;
                    }
                }

                /* Associate the per-VCPU PIB pointers to the per-VM PIB mapping. */
                size_t const offApicPib    = idCpu * sizeof(APICPIB);
                pApicCpu->pvApicPibR0      = (RTR0PTR)((const uint8_t *)pApic->pvApicPibR0 + offApicPib);
                pApicCpu->pvApicPibR3      = (RTR3PTR)((const uint8_t *)pApic->pvApicPibR3 + offApicPib);
                if (fNeedGCMapping)
                    pApicCpu->pvApicPibRC += offApicPib;

                /* Initialize the virtual-APIC state. */
                memset((void *)pApicCpu->pvApicPageR3, 0, pApicCpu->cbApicPage);
                APICR3Reset(pVCpu);
            }
            else
            {
                LogRel(("APIC%u: Failed to allocate %u bytes for the virtual-APIC page\n", pApicCpu->cbApicPage));
                apicR3TermState(pVM);
                return VERR_NO_MEMORY;
            }
        }

        return VINF_SUCCESS;
    }

    LogRel(("APIC: Failed to allocate %u bytes of contiguous low-memory for the pending-interrupt bitmap\n", pApic->cbApicPib));
    return VERR_NO_MEMORY;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
static DECLCALLBACK(int) apicR3Destruct(PPDMDEVINS pDevIns)
{
    PVM pVM = PDMDevHlpGetVM(pDevIns);
    apicR3TermState(pVM);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnInitComplete}
 */
static DECLCALLBACK(int) apicR3InitComplete(PPDMDEVINS pDevIns)
{
    PVM   pVM   = PDMDevHlpGetVM(pDevIns);
    PAPIC pApic = VM_TO_APIC(pVM);

    CPUMCPUIDLEAF CpuLeaf;
    int rc = CPUMR3CpuIdGetLeaf(pVM, &CpuLeaf, 1, 0);
    AssertRCReturn(rc, rc);
    pApic->fSupportsTscDeadline = RT_BOOL(CpuLeaf.uEcx & X86_CPUID_FEATURE_ECX_TSCDEADL);

    pApic->fPostedIntrsEnabled  = HMR3IsPostedIntrsEnabled(pVM->pUVM);
    pApic->fVirtApicRegsEnabled = HMR3IsVirtApicRegsEnabled(pVM->pUVM);

    LogRel(("APIC: fPostedIntrsEnabled=%RTbool fVirtApicRegsEnabled=%RTbool fSupportsTscDeadline=%RTbool\n",
            pApic->fPostedIntrsEnabled, pApic->fVirtApicRegsEnabled, pApic->fSupportsTscDeadline));
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) apicR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    /*
     * Validate inputs.
     */
    Assert(iInstance == 0);
    Assert(pDevIns);

    PAPICDEV pApicDev = PDMINS_2_DATA(pDevIns, PAPICDEV);
    PVM      pVM      = PDMDevHlpGetVM(pDevIns);
    PAPIC    pApic    = VM_TO_APIC(pVM);

    /*
     * Validate APIC settings.
     */
    int rc = CFGMR3ValidateConfig(pCfg, "/APIC/",
                                  "RZEnabled"
                                  "|Mode"
                                  "|IOAPIC"
                                  "|NumCPUs",
                                  "" /* pszValidNodes */, "APIC" /* pszWho */, 0 /* uInstance */);
    if (RT_FAILURE(rc))
        return rc;

    rc = CFGMR3QueryBoolDef(pCfg, "RZEnabled", &pApic->fRZEnabled, true);
    AssertLogRelRCReturn(rc, rc);

    rc = CFGMR3QueryBoolDef(pCfg, "IOAPIC", &pApic->fIoApicPresent, true);
    AssertLogRelRCReturn(rc, rc);

    uint8_t uOriginalMode;
    rc = CFGMR3QueryU8Def(pCfg, "Mode", &uOriginalMode, APICMODE_XAPIC);
    AssertLogRelRCReturn(rc, rc);
    /* Validate APIC modes. */
    switch (uOriginalMode)
    {
        case APICMODE_DISABLED:
        case APICMODE_X2APIC:
        case APICMODE_XAPIC:
            pApic->enmOriginalMode = (APICMODE)uOriginalMode;
            break;
        default:
            return VMR3SetError(pVM->pUVM, VERR_INVALID_STATE, RT_SRC_POS, "APIC mode %#x unknown.", uOriginalMode);
    }

    /*
     * Initialize the APIC state.
     */
    pApicDev->pDevInsR3 = pDevIns;
    pApicDev->pDevInsR0 = PDMDEVINS_2_R0PTR(pDevIns);
    pApicDev->pDevInsRC = PDMDEVINS_2_RCPTR(pDevIns);

    pApic->pApicDevR0   = PDMINS_2_DATA_R0PTR(pDevIns);
    pApic->pApicDevR3   = (PAPICDEV)PDMINS_2_DATA_R3PTR(pDevIns);
    pApic->pApicDevRC   = PDMINS_2_DATA_RCPTR(pDevIns);

    rc = apicR3InitState(pVM);
    AssertRCReturn(rc, rc);

    /*
     * Disable automatic PDM locking for this device.
     */
    rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    /*
     * Register the APIC.
     */
    PDMAPICREG ApicReg;
    RT_ZERO(ApicReg);
    ApicReg.u32Version              = PDM_APICREG_VERSION;
    ApicReg.pfnGetInterruptR3       = APICGetInterrupt;
    ApicReg.pfnHasPendingIrqR3      = APICHasPendingIrq;
    ApicReg.pfnSetBaseMsrR3         = APICSetBaseMsr;
    ApicReg.pfnGetBaseMsrR3         = APICGetBaseMsr;
    ApicReg.pfnSetTprR3             = APICSetTpr;
    ApicReg.pfnGetTprR3             = APICGetTpr;
    ApicReg.pfnWriteMsrR3           = APICWriteMsr;
    ApicReg.pfnReadMsrR3            = APICReadMsr;
    ApicReg.pfnBusDeliverR3         = APICBusDeliver;
    ApicReg.pfnLocalInterruptR3     = APICLocalInterrupt;
    ApicReg.pfnGetTimerFreqR3       = APICGetTimerFreq;
    if (pApic->fRZEnabled)
    {
        ApicReg.pszGetInterruptRC   = "APICGetInterrupt";
        ApicReg.pszHasPendingIrqRC  = "APICHasPendingIrq";
        ApicReg.pszSetBaseMsrRC     = "APICSetBaseMsr";
        ApicReg.pszGetBaseMsrRC     = "APICGetBaseMsr";
        ApicReg.pszSetTprRC         = "APICSetTpr";
        ApicReg.pszGetTprRC         = "APICGetTpr";
        ApicReg.pszWriteMsrRC       = "APICWriteMsr";
        ApicReg.pszReadMsrRC        = "APICReadMsr";
        ApicReg.pszBusDeliverRC     = "APICBusDeliver";
        ApicReg.pszLocalInterruptRC = "APICLocalInterrupt";
        ApicReg.pszGetTimerFreqRC   = "APICGetTimerFreq";

        ApicReg.pszGetInterruptR0   = "APICGetInterrupt";
        ApicReg.pszHasPendingIrqR0  = "APICHasPendingIrq";
        ApicReg.pszSetBaseMsrR0     = "APICSetBaseMsr";
        ApicReg.pszGetBaseMsrR0     = "APICGetBaseMsr";
        ApicReg.pszSetTprR0         = "APICSetTpr";
        ApicReg.pszGetTprR0         = "APICGetTpr";
        ApicReg.pszWriteMsrR0       = "APICWriteMsr";
        ApicReg.pszReadMsrR0        = "APICReadMsr";
        ApicReg.pszBusDeliverR0     = "APICBusDeliver";
        ApicReg.pszLocalInterruptR0 = "APICLocalInterrupt";
        ApicReg.pszGetTimerFreqR0   = "APICGetTimerFreq";
    }

    rc = PDMDevHlpAPICRegister(pDevIns, &ApicReg, &pApicDev->pApicHlpR3);
    AssertLogRelRCReturn(rc, rc);
    pApicDev->pCritSectR3 = pApicDev->pApicHlpR3->pfnGetR3CritSect(pDevIns);

    /*
     * Update the CPUID bits.
     */
    APICUpdateCpuIdForMode(pVM, pApic->enmOriginalMode);
    LogRel(("APIC: Switched mode to %s\n", apicGetModeName(pApic->enmOriginalMode)));

    /*
     * Register the MMIO range.
     */
    PAPICCPU pApicCpu0 = VMCPU_TO_APICCPU(&pVM->aCpus[0]);
    RTGCPHYS GCPhysApicBase = MSR_APICBASE_GET_PHYSADDR(pApicCpu0->uApicBaseMsr);
    rc = PDMDevHlpMMIORegister(pDevIns, GCPhysApicBase, sizeof(XAPICPAGE), pVM,
                               IOMMMIO_FLAGS_READ_DWORD | IOMMMIO_FLAGS_WRITE_DWORD_ZEROED,
                               APICWriteMmio, APICReadMmio, "APIC");
    if (RT_FAILURE(rc))
        return rc;

    if (pApic->fRZEnabled)
    {
        pApicDev->pApicHlpRC  = pApicDev->pApicHlpR3->pfnGetRCHelpers(pDevIns);
        pApicDev->pCritSectRC = pApicDev->pApicHlpR3->pfnGetRCCritSect(pDevIns);
        rc = PDMDevHlpMMIORegisterRC(pDevIns, GCPhysApicBase, sizeof(XAPICPAGE), NIL_RTRCPTR /*pvUser*/,
                                     "APICWriteMmio", "APICReadMmio");
        if (RT_FAILURE(rc))
            return rc;

        pApicDev->pApicHlpR0  = pApicDev->pApicHlpR3->pfnGetR0Helpers(pDevIns);
        pApicDev->pCritSectR0 = pApicDev->pApicHlpR3->pfnGetR0CritSect(pDevIns);
        rc = PDMDevHlpMMIORegisterR0(pDevIns, GCPhysApicBase, sizeof(XAPICPAGE), NIL_RTR0PTR /*pvUser*/,
                                     "APICWriteMmio", "APICReadMmio");
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Create the APIC timers.
     */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU   pVCpu    = &pVM->aCpus[idCpu];
        PAPICCPU pApicCpu = VMCPU_TO_APICCPU(pVCpu);
        RTStrPrintf(&pApicCpu->szTimerDesc[0], sizeof(pApicCpu->szTimerDesc), "APIC Timer %u", pVCpu->idCpu);
        rc = PDMDevHlpTMTimerCreate(pDevIns, TMCLOCK_VIRTUAL_SYNC, apicR3TimerCallback, pVCpu, TMTIMER_FLAGS_NO_CRIT_SECT,
                                    pApicCpu->szTimerDesc, &pApicCpu->pTimerR3);
        if (RT_SUCCESS(rc))
        {
            pApicCpu->pTimerR0 = TMTimerR0Ptr(pApicCpu->pTimerR3);
            pApicCpu->pTimerRC = TMTimerRCPtr(pApicCpu->pTimerR3);

            rc = PDMR3CritSectInit(pVM, &pApicCpu->TimerCritSect, RT_SRC_POS, pApicCpu->szTimerDesc);
            if (RT_SUCCESS(rc))
                TMR3TimerSetCritSect(pApicCpu->pTimerR3, &pApicCpu->TimerCritSect);
            else
                return rc;
        }
        else
            return rc;
    }

    /*
     * Register saved state callbacks.
     */
    rc = PDMDevHlpSSMRegister3(pDevIns, APIC_SAVED_STATE_VERSION, sizeof(*pApicDev), NULL /*pfnLiveExec*/, apicR3SaveExec,
                               apicR3LoadExec);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Register debugger info callback.
     */
    PDMDevHlpDBGFInfoRegister(pDevIns, "apic", "Display Local APIC state for current CPU. "
                              "Recognizes 'basic', 'lvt', 'timer' as arguments, defaulting to 'basic'.", apicR3DbgInfo);

#ifdef VBOX_WITH_STATISTICS
    /*
     * Statistics.
     */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU   pVCpu    = &pVM->aCpus[idCpu];
        PAPICCPU pApicCpu = VMCPU_TO_APICCPU(pVCpu);
        PDMDevHlpSTAMRegisterF(pDevIns, &pApicCpu->StatMmioReadGC,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                               "Number of APIC MMIO reads in GC.",  "/Devices/APIC/%u/MmioReadGC",  idCpu);
        PDMDevHlpSTAMRegisterF(pDevIns, &pApicCpu->StatMmioReadHC,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                               "Number of APIC MMIO reads in HC.",  "/Devices/APIC/%u/MmioReadHC",  idCpu);

        PDMDevHlpSTAMRegisterF(pDevIns, &pApicCpu->StatMmioWriteGC, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                               "Number of APIC MMIO writes in GC.", "/Devices/APIC/%u/MmioWriteGC", idCpu);
        PDMDevHlpSTAMRegisterF(pDevIns, &pApicCpu->StatMmioWriteHC, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                               "Number of APIC MMIO writes in HC.", "/Devices/APIC/%u/MmioWriteHC", idCpu);

        PDMDevHlpSTAMRegisterF(pDevIns, &pApicCpu->StatMsrWrite,    STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                               "Number of APIC MSR writes.", "/Devices/APIC/%u/MsrWrite", idCpu);
        PDMDevHlpSTAMRegisterF(pDevIns, &pApicCpu->StatMsrRead,     STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,
                               "Number of APIC MSR reads.",  "/Devices/APIC/%u/MsrRead",  idCpu);
    }
#endif
    return VINF_SUCCESS;
}


/**
 * APIC device registration structure.
 */
const PDMDEVREG g_DeviceAPIC =
{
    /* u32Version */
    PDM_DEVREG_VERSION,
    /* szName */
    "apic",
    /* szRCMod */
    "VMMRC.rc",
    /* szR0Mod */
    "VMMR0.r0",
    /* pszDescription */
    "Advanced Programmable Interrupt Controller",
    /* fFlags */
      PDM_DEVREG_FLAGS_HOST_BITS_DEFAULT | PDM_DEVREG_FLAGS_GUEST_BITS_32_64 | PDM_DEVREG_FLAGS_PAE36
    | PDM_DEVREG_FLAGS_RC | PDM_DEVREG_FLAGS_R0,
    /* fClass */
    PDM_DEVREG_CLASS_PIC,
    /* cMaxInstances */
    1,
    /* cbInstance */
    sizeof(APICDEV),
    /* pfnConstruct */
    apicR3Construct,
    /* pfnDestruct */
    apicR3Destruct,
    /* pfnRelocate */
    apicR3Relocate,
    /* pfnMemSetup */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    apicR3Reset,
    /* pfnSuspend */
    NULL,
    /* pfnResume */
    NULL,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnQueryInterface. */
    NULL,
    /* pfnInitComplete */
    apicR3InitComplete,
    /* pfnPowerOff */
    NULL,
    /* pfnSoftReset */
    NULL,
    /* u32VersionEnd */
    PDM_DEVREG_VERSION
};

#endif /* !VBOX_DEVICE_STRUCT_TESTCASE */
