/** @file
 *
 * VBox storage devices:
 * Host base drive access driver
 */

/*
 * Copyright (C) 2006 InnoTek Systemberatung GmbH
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation,
 * in version 2 as it comes in the "COPYING" file of the VirtualBox OSE
 * distribution. VirtualBox OSE is distributed in the hope that it will
 * be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * If you received this file as part of a commercial VirtualBox
 * distribution, then only the terms of your commercial VirtualBox
 * license agreement apply instead of the previous paragraph.
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_DRV_HOST_BASE
#ifdef __LINUX__
# include <sys/ioctl.h>
# include <sys/fcntl.h>
# include <errno.h>

#elif defined(__WIN__)
# define WIN32_NO_STATUS
# include <Windows.h>
# include <dbt.h>
# undef WIN32_NO_STATUS
# include <ntstatus.h>

/* from ntdef.h */
typedef LONG NTSTATUS;

/* from ntddk.h */
typedef struct _IO_STATUS_BLOCK {
    union {
        NTSTATUS Status;
        PVOID Pointer;
    };
    ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;


/* from ntinternals.com */
typedef enum _FS_INFORMATION_CLASS {
    FileFsVolumeInformation=1,
    FileFsLabelInformation,
    FileFsSizeInformation,
    FileFsDeviceInformation,
    FileFsAttributeInformation,
    FileFsControlInformation,
    FileFsFullSizeInformation,
    FileFsObjectIdInformation,
    FileFsMaximumInformation
} FS_INFORMATION_CLASS, *PFS_INFORMATION_CLASS;

typedef struct _FILE_FS_SIZE_INFORMATION {
    LARGE_INTEGER   TotalAllocationUnits;
    LARGE_INTEGER   AvailableAllocationUnits;
    ULONG           SectorsPerAllocationUnit;
    ULONG           BytesPerSector;
} FILE_FS_SIZE_INFORMATION, *PFILE_FS_SIZE_INFORMATION;

extern "C"
NTSTATUS __stdcall NtQueryVolumeInformationFile(
        /*IN*/ HANDLE               FileHandle,
        /*OUT*/ PIO_STATUS_BLOCK    IoStatusBlock,
        /*OUT*/ PVOID               FileSystemInformation,
        /*IN*/ ULONG                Length,
        /*IN*/ FS_INFORMATION_CLASS FileSystemInformationClass );

#elif defined(__L4ENV__)

#else /* !__WIN__ nor __LINUX__ nor __L4ENV__ */
# error "Unsupported Platform."
#endif /* !__WIN__ nor __LINUX__ nor __L4ENV__ */

#include <VBox/pdm.h>
#include <VBox/cfgm.h>
#include <VBox/mm.h>
#include <VBox/err.h>

#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/file.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/semaphore.h>
#include <iprt/uuid.h>
#include <iprt/asm.h>
#include <iprt/critsect.h>

#include "DrvHostBase.h"




/* -=-=-=-=- IBlock -=-=-=-=- */

/** @copydoc PDMIBLOCK::pfnRead */
static DECLCALLBACK(int) drvHostBaseRead(PPDMIBLOCK pInterface, uint64_t off, void *pvBuf, size_t cbRead)
{
    PDRVHOSTBASE pThis = PDMIBLOCK_2_DRVHOSTBASE(pInterface);
    LogFlow(("%s-%d: drvHostBaseRead: off=%#llx pvBuf=%p cbRead=%#x (%s)\n",
             pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance, off, pvBuf, cbRead, pThis->pszDevice));
    RTCritSectEnter(&pThis->CritSect);

    /*
     * Check the state.
     */
    int rc;
    if (pThis->fMediaPresent)
    {
        /*
         * Seek and read.
         */
        rc = RTFileSeek(pThis->FileDevice, off, RTFILE_SEEK_BEGIN, NULL);
        if (VBOX_SUCCESS(rc))
        {
            rc = RTFileRead(pThis->FileDevice, pvBuf, cbRead, NULL);
            if (VBOX_SUCCESS(rc))
            {
                Log2(("%s-%d: drvHostBaseRead: off=%#llx cbRead=%#x\n"
                      "%16.*Vhxd\n",
                      pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance, off, cbRead, cbRead, pvBuf));
            }
            else
                Log(("%s-%d: drvHostBaseRead: RTFileRead(%d, %p, %#x) -> %Vrc (off=%#llx '%s')\n",
                     pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance, pThis->FileDevice,
                     pvBuf, cbRead, rc, off, pThis->pszDevice));
        }
        else
            Log(("%s-%d: drvHostBaseRead: RTFileSeek(%d,%#llx,) -> %Vrc\n", pThis->pDrvIns->pDrvReg->szDriverName,
                 pThis->pDrvIns->iInstance, pThis->FileDevice, off, rc));
    }
    else
        rc = VERR_MEDIA_NOT_PRESENT;

    RTCritSectLeave(&pThis->CritSect);
    LogFlow(("%s-%d: drvHostBaseRead: returns %Vrc\n", pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance, rc));
    return rc;
}


/** @copydoc PDMIBLOCK::pfnWrite */
static DECLCALLBACK(int) drvHostBaseWrite(PPDMIBLOCK pInterface, uint64_t off, const void *pvBuf, size_t cbWrite)
{
    PDRVHOSTBASE pThis = PDMIBLOCK_2_DRVHOSTBASE(pInterface);
    LogFlow(("%s-%d: drvHostBaseWrite: off=%#llx pvBuf=%p cbWrite=%#x (%s)\n",
             pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance, off, pvBuf, cbWrite, pThis->pszDevice));
    Log2(("%s-%d: drvHostBaseWrite: off=%#llx cbWrite=%#x\n"
          "%16.*Vhxd\n",
          pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance, off, cbWrite, cbWrite, pvBuf));
    RTCritSectEnter(&pThis->CritSect);

    /*
     * Check the state.
     */
    int rc;
    if (!pThis->fReadOnly)
    {
        if (pThis->fMediaPresent)
        {
            /*
             * Seek and write.
             */
            rc = RTFileSeek(pThis->FileDevice, off, RTFILE_SEEK_BEGIN, NULL);
            if (VBOX_SUCCESS(rc))
            {
                rc = RTFileWrite(pThis->FileDevice, pvBuf, cbWrite, NULL);
                if (VBOX_FAILURE(rc))
                    Log(("%s-%d: drvHostBaseWrite: RTFileWrite(%d, %p, %#x) -> %Vrc (off=%#llx '%s')\n",
                         pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance, pThis->FileDevice,
                         pvBuf, cbWrite, rc, off, pThis->pszDevice));
            }
            else
                Log(("%s-%d: drvHostBaseWrite: RTFileSeek(%d,%#llx,) -> %Vrc\n",
                     pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance, pThis->FileDevice, off, rc));
        }
        else
            rc = VERR_MEDIA_NOT_PRESENT;
    }
    else
        rc = VERR_WRITE_PROTECT;

    RTCritSectLeave(&pThis->CritSect);
    LogFlow(("%s-%d: drvHostBaseWrite: returns %Vrc\n", pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance, rc));
    return rc;
}


/** @copydoc PDMIBLOCK::pfnFlush */
static DECLCALLBACK(int) drvHostBaseFlush(PPDMIBLOCK pInterface)
{
    int rc;
    PDRVHOSTBASE pThis = PDMIBLOCK_2_DRVHOSTBASE(pInterface);
    LogFlow(("%s-%d: drvHostBaseFlush: (%s)\n",
             pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance, pThis->pszDevice));
    RTCritSectEnter(&pThis->CritSect);

    if (pThis->fMediaPresent)
    {
        rc = RTFileFlush(pThis->FileDevice);
    }
    else
        rc = VERR_MEDIA_NOT_PRESENT;

    RTCritSectLeave(&pThis->CritSect);
    LogFlow(("%s-%d: drvHostBaseFlush: returns %Vrc\n", pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance, rc));
    return rc;
}


/** @copydoc PDMIBLOCK::pfnIsReadOnly */
static DECLCALLBACK(bool) drvHostBaseIsReadOnly(PPDMIBLOCK pInterface)
{
    PDRVHOSTBASE pThis = PDMIBLOCK_2_DRVHOSTBASE(pInterface);
    return pThis->fReadOnly;
}


/** @copydoc PDMIBLOCK::pfnGetSize */
static DECLCALLBACK(uint64_t) drvHostBaseGetSize(PPDMIBLOCK pInterface)
{
    PDRVHOSTBASE pThis = PDMIBLOCK_2_DRVHOSTBASE(pInterface);
    RTCritSectEnter(&pThis->CritSect);

    uint64_t cb = 0;
    if (pThis->fMediaPresent)
        cb = pThis->cbSize;

    RTCritSectLeave(&pThis->CritSect);
    LogFlow(("%s-%d: drvHostBaseGetSize: returns %llu\n", pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance, cb));
    return cb;
}


/** @copydoc PDMIBLOCK::pfnGetType */
static DECLCALLBACK(PDMBLOCKTYPE) drvHostBaseGetType(PPDMIBLOCK pInterface)
{
    PDRVHOSTBASE pThis = PDMIBLOCK_2_DRVHOSTBASE(pInterface);
    LogFlow(("%s-%d: drvHostBaseGetType: returns %d\n", pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance, pThis->enmType));
    return pThis->enmType;
}


/** @copydoc PDMIBLOCK::pfnGetUuid */
static DECLCALLBACK(int) drvHostBaseGetUuid(PPDMIBLOCK pInterface, PRTUUID pUuid)
{
    PDRVHOSTBASE pThis = PDMIBLOCK_2_DRVHOSTBASE(pInterface);

    *pUuid = pThis->Uuid;

    LogFlow(("%s-%d: drvHostBaseGetUuid: returns VINF_SUCCESS *pUuid=%Vuuid\n", pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance, pUuid));
    return VINF_SUCCESS;
}


/* -=-=-=-=- IBlockBios -=-=-=-=- */

/** Makes a PDRVHOSTBASE out of a PPDMIBLOCKBIOS. */
#define PDMIBLOCKBIOS_2_DRVHOSTBASE(pInterface)    ( (PDRVHOSTBASE((uintptr_t)pInterface - RT_OFFSETOF(DRVHOSTBASE, IBlockBios))) )


/** @copydoc PDMIBLOCKBIOS::pfnGetGeometry */
static DECLCALLBACK(int) drvHostBaseGetGeometry(PPDMIBLOCKBIOS pInterface, uint32_t *pcCylinders, uint32_t *pcHeads, uint32_t *pcSectors)
{
    PDRVHOSTBASE pThis =  PDMIBLOCKBIOS_2_DRVHOSTBASE(pInterface);
    RTCritSectEnter(&pThis->CritSect);

    int rc = VINF_SUCCESS;
    if (pThis->fMediaPresent)
    {
        if (    pThis->cCylinders > 0
            &&  pThis->cHeads > 0
            &&  pThis->cSectors > 0)
        {
            *pcCylinders = pThis->cCylinders;
            *pcHeads = pThis->cHeads;
            *pcSectors = pThis->cSectors;
        }
        else
            rc = VERR_PDM_GEOMETRY_NOT_SET;
    }
    else
        rc = VERR_PDM_MEDIA_NOT_MOUNTED;

    RTCritSectLeave(&pThis->CritSect);
    LogFlow(("%s-%d: drvHostBaseGetGeometry: returns %Vrc CHS={%d,%d,%d}\n",
             pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance, rc, *pcCylinders, *pcHeads, *pcSectors));
    return rc;
}


/** @copydoc PDMIBLOCKBIOS::pfnSetGeometry */
static DECLCALLBACK(int) drvHostBaseSetGeometry(PPDMIBLOCKBIOS pInterface, uint32_t cCylinders, uint32_t cHeads, uint32_t cSectors)
{
    PDRVHOSTBASE pThis = PDMIBLOCKBIOS_2_DRVHOSTBASE(pInterface);
    LogFlow(("%s-%d: drvHostBaseSetGeometry: cCylinders=%d cHeads=%d cSectors=%d\n",
             pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance, cCylinders, cHeads, cSectors));
    RTCritSectEnter(&pThis->CritSect);

    int rc = VINF_SUCCESS;
    if (pThis->fMediaPresent)
    {
        pThis->cCylinders = cCylinders;
        pThis->cHeads     = cHeads;
        pThis->cSectors   = cSectors;
    }
    else
    {
        AssertMsgFailed(("Invalid state! Not mounted!\n"));
        rc = VERR_PDM_MEDIA_NOT_MOUNTED;
    }

    RTCritSectLeave(&pThis->CritSect);
    return rc;
}


/** @copydoc PDMIBLOCKBIOS::pfnGetTranslation */
static DECLCALLBACK(int) drvHostBaseGetTranslation(PPDMIBLOCKBIOS pInterface, PPDMBIOSTRANSLATION penmTranslation)
{
    PDRVHOSTBASE pThis = PDMIBLOCKBIOS_2_DRVHOSTBASE(pInterface);
    RTCritSectEnter(&pThis->CritSect);

    int rc = VINF_SUCCESS;
    if (pThis->fMediaPresent)
    {
        if (pThis->fTranslationSet)
            *penmTranslation = pThis->enmTranslation;
        else
            rc = VERR_PDM_TRANSLATION_NOT_SET;
    }
    else
        rc = VERR_PDM_MEDIA_NOT_MOUNTED;

    RTCritSectLeave(&pThis->CritSect);
    LogFlow(("%s-%d: drvHostBaseGetTranslation: returns %Vrc *penmTranslation=%d\n",
             pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance, rc, *penmTranslation));
    return rc;
}


/** @copydoc PDMIBLOCKBIOS::pfnSetTranslation */
static DECLCALLBACK(int) drvHostBaseSetTranslation(PPDMIBLOCKBIOS pInterface, PDMBIOSTRANSLATION enmTranslation)
{
    PDRVHOSTBASE pThis = PDMIBLOCKBIOS_2_DRVHOSTBASE(pInterface);
    LogFlow(("%s-%d: drvHostBaseSetTranslation: enmTranslation=%d\n",
             pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance, enmTranslation));
    RTCritSectEnter(&pThis->CritSect);

    int rc = VINF_SUCCESS;
    if (pThis->fMediaPresent)
    {
        pThis->fTranslationSet = true;
        pThis->enmTranslation = enmTranslation;
    }
    else
    {
        AssertMsgFailed(("Invalid state! Not mounted!\n"));
        rc = VERR_PDM_MEDIA_NOT_MOUNTED;
    }

    RTCritSectLeave(&pThis->CritSect);
    return rc;
}


/** @copydoc PDMIBLOCKBIOS::pfnIsVisible */
static DECLCALLBACK(bool) drvHostBaseIsVisible(PPDMIBLOCKBIOS pInterface)
{
    PDRVHOSTBASE pThis = PDMIBLOCKBIOS_2_DRVHOSTBASE(pInterface);
    return pThis->fBiosVisible;
}


/** @copydoc PDMIBLOCKBIOS::pfnGetType */
static DECLCALLBACK(PDMBLOCKTYPE) drvHostBaseBiosGetType(PPDMIBLOCKBIOS pInterface)
{
    PDRVHOSTBASE pThis = PDMIBLOCKBIOS_2_DRVHOSTBASE(pInterface);
    return pThis->enmType;
}



/* -=-=-=-=- IMount -=-=-=-=- */

/** @copydoc PDMIMOUNT::pfnMount */
static DECLCALLBACK(int) drvHostBaseMount(PPDMIMOUNT pInterface, const char *pszFilename, const char *pszCoreDriver)
{
    /* We're not mountable. */
    AssertMsgFailed(("drvHostBaseMount: This shouldn't be called!\n"));
    return VERR_PDM_MEDIA_MOUNTED;
}


/** @copydoc PDMIMOUNT::pfnUnmount */
static DECLCALLBACK(int) drvHostBaseUnmount(PPDMIMOUNT pInterface)
{
     LogFlow(("drvHostBaseUnmount: returns VERR_NOT_SUPPORTED\n"));
     return VERR_NOT_SUPPORTED;
}


/** @copydoc PDMIMOUNT::pfnIsMounted */
static DECLCALLBACK(bool) drvHostBaseIsMounted(PPDMIMOUNT pInterface)
{
    PDRVHOSTBASE pThis = PDMIMOUNT_2_DRVHOSTBASE(pInterface);
    RTCritSectEnter(&pThis->CritSect);

    bool fRc = pThis->fMediaPresent;

    RTCritSectLeave(&pThis->CritSect);
    return fRc;
}


/** @copydoc PDMIMOUNT::pfnIsLocked */
static DECLCALLBACK(int) drvHostBaseLock(PPDMIMOUNT pInterface)
{
    PDRVHOSTBASE pThis = PDMIMOUNT_2_DRVHOSTBASE(pInterface);
    RTCritSectEnter(&pThis->CritSect);

    int rc = VINF_SUCCESS;
    if (!pThis->fLocked)
    {
        if (pThis->pfnDoLock)
            rc = pThis->pfnDoLock(pThis, true);
        if (VBOX_SUCCESS(rc))
            pThis->fLocked = true;
    }
    else
        LogFlow(("%s-%d: drvHostBaseLock: already locked\n", pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance));

    RTCritSectLeave(&pThis->CritSect);
    LogFlow(("%s-%d: drvHostBaseLock: returns %Vrc\n", pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance, rc));
    return rc;
}


/** @copydoc PDMIMOUNT::pfnIsLocked */
static DECLCALLBACK(int) drvHostBaseUnlock(PPDMIMOUNT pInterface)
{
    PDRVHOSTBASE pThis = PDMIMOUNT_2_DRVHOSTBASE(pInterface);
    RTCritSectEnter(&pThis->CritSect);

    int rc = VINF_SUCCESS;
    if (pThis->fLocked)
    {
        if (pThis->pfnDoLock)
            rc = pThis->pfnDoLock(pThis, false);
        if (VBOX_SUCCESS(rc))
            pThis->fLocked = false;
    }
    else
        LogFlow(("%s-%d: drvHostBaseUnlock: not locked\n", pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance));

    RTCritSectLeave(&pThis->CritSect);
    LogFlow(("%s-%d: drvHostBaseUnlock: returns %Vrc\n", pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance, rc));
    return rc;
}


/** @copydoc PDMIMOUNT::pfnIsLocked */
static DECLCALLBACK(bool) drvHostBaseIsLocked(PPDMIMOUNT pInterface)
{
    PDRVHOSTBASE pThis = PDMIMOUNT_2_DRVHOSTBASE(pInterface);
    RTCritSectEnter(&pThis->CritSect);

    bool fRc = pThis->fLocked;

    RTCritSectLeave(&pThis->CritSect);
    return fRc;
}


/* -=-=-=-=- IBase -=-=-=-=- */

/** @copydoc PDMIBASE::pfnQueryInterface. */
static DECLCALLBACK(void *)  drvHostBaseQueryInterface(PPDMIBASE pInterface, PDMINTERFACE enmInterface)
{
    PPDMDRVINS  pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVHOSTBASE   pThis = PDMINS2DATA(pDrvIns, PDRVHOSTBASE);
    switch (enmInterface)
    {
        case PDMINTERFACE_BASE:
            return &pDrvIns->IBase;
        case PDMINTERFACE_BLOCK:
            return &pThis->IBlock;
        case PDMINTERFACE_BLOCK_BIOS:
            return pThis->fBiosVisible ? &pThis->IBlockBios : NULL;
        case PDMINTERFACE_MOUNT:
            return &pThis->IMount;
        default:
            return NULL;
    }
}


/* -=-=-=-=- poller thread -=-=-=-=- */

/**
 * Wrapper for open / RTFileOpen.
 */
static int drvHostBaseOpen(PDRVHOSTBASE pThis, PRTFILE pFileDevice, bool fReadOnly)
{
#ifdef __LINUX__
    int FileDevice = open(pThis->pszDeviceOpen, (pThis->fReadOnlyConfig ? O_RDONLY : O_RDWR) | O_NONBLOCK);
    if (FileDevice < 0)
        return RTErrConvertFromErrno(errno);
    *pFileDevice = FileDevice;
    return VINF_SUCCESS;
#else
    return RTFileOpen(pFileDevice, pThis->pszDeviceOpen,
                      (fReadOnly ? RTFILE_O_READ : RTFILE_O_READWRITE) | RTFILE_O_OPEN | RTFILE_O_DENY_NONE);
#endif
}

/**
 * (Re)opens the device.
 *
 * @returns VBOX status code.
 * @param   pThis       Instance data.
 */
static int drvHostBaseReopen(PDRVHOSTBASE pThis)
{
    LogFlow(("%s-%d: drvHostBaseReopen: '%s'\n", pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance, pThis->pszDeviceOpen));

    /*
     * Reopen the device to kill any cached data which for some peculiar reason stays on some OSes (linux)...
     */
    RTFILE FileDevice;
    int rc = drvHostBaseOpen(pThis, &FileDevice, pThis->fReadOnlyConfig);
    if (VBOX_FAILURE(rc))
    {
        if (!pThis->fReadOnlyConfig)
        {
            LogFlow(("%s-%d: drvHostBaseReopen: '%s' - retry readonly (%Vrc)\n", pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance, pThis->pszDeviceOpen, rc));
            rc = drvHostBaseOpen(pThis, &FileDevice, false);
        }
        if (VBOX_FAILURE(rc))
        {
            LogFlow(("%s-%d: failed to open device '%s', rc=%Vrc\n",
                     pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance, pThis->pszDevice, rc));
            return rc;
        }
        pThis->fReadOnly = true;
    }
    else
        pThis->fReadOnly = pThis->fReadOnlyConfig;

    if (pThis->FileDevice != NIL_RTFILE)
        RTFileClose(pThis->FileDevice);
    pThis->FileDevice = FileDevice;
    return VINF_SUCCESS;
}


/**
 * Queries the media size.
 *
 * @returns VBox status code.
 * @param   pThis       Pointer to the instance data.
 * @param   pcb         Where to store the media size in bytes.
 */
static int drvHostBaseGetMediaSize(PDRVHOSTBASE pThis, uint64_t *pcb)
{
#ifdef __WIN__
    /* use NT api, retry a few times if the media is being verified. */
    IO_STATUS_BLOCK             IoStatusBlock = {0};
    FILE_FS_SIZE_INFORMATION    FsSize= {0};
    NTSTATUS rcNt = NtQueryVolumeInformationFile((HANDLE)pThis->FileDevice,  &IoStatusBlock,
                                                 &FsSize, sizeof(FsSize), FileFsSizeInformation);
    int cRetries = 5;
    while (rcNt == STATUS_VERIFY_REQUIRED && cRetries-- > 0)
    {
        RTThreadSleep(10);
        rcNt = NtQueryVolumeInformationFile((HANDLE)pThis->FileDevice,  &IoStatusBlock,
                                            &FsSize, sizeof(FsSize), FileFsSizeInformation);
    }
    if (rcNt >= 0)
    {
        *pcb = FsSize.TotalAllocationUnits.QuadPart * FsSize.BytesPerSector;
        return VINF_SUCCESS;
    }

    /* convert nt status code to VBox status code. */
    /** @todo Make convertion function!. */
    int rc = VERR_GENERAL_FAILURE;
    switch (rcNt)
    {
        case STATUS_NO_MEDIA_IN_DEVICE:     rc = VERR_MEDIA_NOT_PRESENT; break;
        case STATUS_VERIFY_REQUIRED:        rc = VERR_TRY_AGAIN; break;
    }
    LogFlow(("drvHostBaseGetMediaSize: NtQueryVolumeInformationFile -> %#lx\n", rcNt, rc));
    return rc;
#else
    return RTFileSeek(pThis->FileDevice, 0, RTFILE_SEEK_END, pcb);
#endif
}


/**
 * Media present.
 * Query the size and notify the above driver / device.
 *
 * @param   pThis   The instance data.
 */
int DRVHostBaseMediaPresent(PDRVHOSTBASE pThis)
{
    /*
     * Open the drive.
     */
    int rc = drvHostBaseReopen(pThis);
    if (VBOX_FAILURE(rc))
        return rc;

    /*
     * Determin the size.
     */
    uint64_t cb;
    rc = pThis->pfnGetMediaSize(pThis, &cb);
    if (VBOX_FAILURE(rc))
    {
        LogFlow(("%s-%d: failed to figure media size of %s, rc=%Vrc\n",
                 pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance, pThis->pszDevice, rc));
        return rc;
    }

    /*
     * Update the data and inform the unit.
     */
    pThis->cbSize = cb;
    pThis->fMediaPresent = true;
    if (pThis->pDrvMountNotify)
        pThis->pDrvMountNotify->pfnMountNotify(pThis->pDrvMountNotify);
    LogFlow(("%s-%d: drvHostBaseMediaPresent: cbSize=%lld (%#llx)\n",
             pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance, pThis->cbSize, pThis->cbSize));
    return VINF_SUCCESS;
}


/**
 * Media no longer present.
 * @param   pThis   The instance data.
 */
void DRVHostBaseMediaNotPresent(PDRVHOSTBASE pThis)
{
    pThis->fMediaPresent = false;
    pThis->fLocked = false;
    pThis->fTranslationSet = false;
    pThis->cSectors = 0;
    if (pThis->pDrvMountNotify)
        pThis->pDrvMountNotify->pfnUnmountNotify(pThis->pDrvMountNotify);
}


#ifdef __WIN__

/**
 * Window procedure for the invisible window used to catch the WM_DEVICECHANGE broadcasts.
 */
static LRESULT CALLBACK DeviceChangeWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    Log2(("DeviceChangeWindowProc: hwnd=%08x uMsg=%08x\n", hwnd, uMsg));
    if (uMsg == WM_DESTROY)
    {
        PDRVHOSTBASE pThis = (PDRVHOSTBASE)GetWindowLong(hwnd, GWLP_USERDATA);
        if (pThis)
            ASMAtomicXchgSize(&pThis->hwndDeviceChange, NULL);
        PostQuitMessage(0);
    }

    if (uMsg != WM_DEVICECHANGE)
        return DefWindowProc(hwnd, uMsg, wParam, lParam);

    PDEV_BROADCAST_HDR  lpdb = (PDEV_BROADCAST_HDR)lParam;
    PDRVHOSTBASE        pThis = (PDRVHOSTBASE)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    Assert(pThis);
    if (pThis == NULL)
        return 0;

    switch (wParam)
    {
        case DBT_DEVICEARRIVAL:
        case DBT_DEVICEREMOVECOMPLETE:
            // Check whether a CD or DVD was inserted into or removed from a drive.
            if (lpdb->dbch_devicetype == DBT_DEVTYP_VOLUME)
            {
                PDEV_BROADCAST_VOLUME lpdbv = (PDEV_BROADCAST_VOLUME)lpdb;
                if (    (lpdbv->dbcv_flags & DBTF_MEDIA)
                    &&  (pThis->fUnitMask & lpdbv->dbcv_unitmask))
                {
                    RTCritSectEnter(&pThis->CritSect);
                    if (wParam == DBT_DEVICEARRIVAL)
                    {
                        int cRetries = 10;
                        int rc = DRVHostBaseMediaPresent(pThis);
                        while (VBOX_FAILURE(rc) && cRetries-- > 0)
                        {
                            RTThreadSleep(50);
                            rc = DRVHostBaseMediaPresent(pThis);
                        }
                    }
                    else
                        DRVHostBaseMediaNotPresent(pThis);
                    RTCritSectLeave(&pThis->CritSect);
                }
            }
            break;
    }
    return TRUE;
}

#endif /* __WIN__ */


/**
 * This thread will periodically poll the device for media presence.
 *
 * @returns Ignored.
 * @param   ThreadSelf  Handle of this thread. Ignored.
 * @param   pvUser      Pointer to the driver instance structure.
 */
static DECLCALLBACK(int) drvHostBaseMediaThread(RTTHREAD ThreadSelf, void *pvUser)
{
    PDRVHOSTBASE pThis = (PDRVHOSTBASE)pvUser;
    LogFlow(("%s-%d: drvHostBaseMediaThread: ThreadSelf=%p pvUser=%p\n",
             pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance, ThreadSelf, pvUser));
#ifdef __WIN__
    static WNDCLASS s_classDeviceChange = {0};
    static ATOM     s_hAtomDeviceChange = 0;

    /*
     * Register custom window class.
     */
    if (s_hAtomDeviceChange == 0)
    {
        memset(&s_classDeviceChange, 0, sizeof(s_classDeviceChange));
        s_classDeviceChange.lpfnWndProc   = DeviceChangeWindowProc;
        s_classDeviceChange.lpszClassName = "VBOX_DeviceChangeClass";
        s_classDeviceChange.hInstance     = GetModuleHandle("VBOXDD.DLL");
        Assert(s_classDeviceChange.hInstance);
        s_hAtomDeviceChange = RegisterClassA(&s_classDeviceChange);
        Assert(s_hAtomDeviceChange);
    }

    /*
     * Create Window w/ the pThis as user data.
     */
    HWND hwnd = CreateWindow((LPCTSTR)s_hAtomDeviceChange, "", WS_POPUP, 0, 0, 0, 0, 0, 0, s_classDeviceChange.hInstance, 0);
    AssertMsg(hwnd, ("CreateWindow failed with %d\n", GetLastError()));
    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);

    /*
     * Signal the waiting EMT thread that everything went fine.
     */
    ASMAtomicXchgSize(&pThis->hwndDeviceChange, hwnd);
    RTThreadUserSignal(ThreadSelf);
    if (!hwnd)
    {
        LogFlow(("%s-%d: drvHostBaseMediaThread: returns VERR_GENERAL_FAILURE\n", pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance));
        return VERR_GENERAL_FAILURE;
    }
    LogFlow(("%s-%d: drvHostBaseMediaThread: Created hwndDeviceChange=%p\n", pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance, hwnd));

    /*
     * Message pump.
     */
    MSG         Msg;
    BOOL        fRet;
    while ((fRet = GetMessage(&Msg, NULL, 0, 0)) != FALSE)
    {
        if (fRet != -1)
        {
            TranslateMessage(&Msg);
            DispatchMessage(&Msg);
        }
        //else: handle the error and possibly exit
    }
    Assert(!pThis->hwndDeviceChange);

#else /* !__WIN__ */
    bool        fFirst = true;
    int         cRetries = 10;
    while (!pThis->fShutdownPoller)
    {
        /*
         * Perform the polling (unless we've run out of 50ms retries).
         */
        if (    pThis->pfnPoll
            &&  cRetries-- > 0)
        {

            int rc = pThis->pfnPoll(pThis);
            if (VBOX_FAILURE(rc))
            {
                RTSemEventWait(pThis->EventPoller, 50);
                continue;
            }
        }

        /*
         * Signal EMT after the first go.
         */
        if (fFirst)
        {
            RTThreadUserSignal(ThreadSelf);
            fFirst = false;
        }

        /*
         * Sleep.
         */
        int rc = RTSemEventWait(pThis->EventPoller, pThis->cMilliesPoller);
        if (    VBOX_FAILURE(rc)
            &&  rc != VERR_TIMEOUT)
        {
            AssertMsgFailed(("rc=%Vrc\n", rc));
            pThis->ThreadPoller = NIL_RTTHREAD;
            LogFlow(("drvHostBaseMediaThread: returns %Vrc\n", rc));
            return rc;
        }
        cRetries = 10;
    }

#endif /* !__WIN__ */

    /* (Don't clear the thread handle here, the destructor thread is using it to wait.) */
    LogFlow(("%s-%d: drvHostBaseMediaThread: returns VINF_SUCCESS\n", pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance));
    return VINF_SUCCESS;
}

/* -=-=-=-=- driver interface -=-=-=-=- */


/**
 * Done state load operation.
 *
 * @returns VBox load code.
 * @param   pDrvIns         Driver instance of the driver which registered the data unit.
 * @param   pSSM            SSM operation handle.
 */
static DECLCALLBACK(int) drvHostBaseLoadDone(PPDMDRVINS pDrvIns, PSSMHANDLE pSSM)
{
    PDRVHOSTBASE pThis = PDMINS2DATA(pDrvIns, PDRVHOSTBASE);
    LogFlow(("%s-%d: drvHostBaseMediaThread:\n", pThis->pDrvIns->pDrvReg->szDriverName, pThis->pDrvIns->iInstance));
    RTCritSectEnter(&pThis->CritSect);

    /*
     * Tell the device/driver above us that the media status is uncertain.
     */
    if (pThis->pDrvMountNotify)
    {
        pThis->pDrvMountNotify->pfnUnmountNotify(pThis->pDrvMountNotify);
        if (pThis->fMediaPresent)
            pThis->pDrvMountNotify->pfnMountNotify(pThis->pDrvMountNotify);
    }

    RTCritSectLeave(&pThis->CritSect);
    return VINF_SUCCESS;
}


/** @copydoc FNPDMDRVDESTRUCT */
DECLCALLBACK(void) DRVHostBaseDestruct(PPDMDRVINS pDrvIns)
{
    PDRVHOSTBASE pThis = PDMINS2DATA(pDrvIns, PDRVHOSTBASE);
    LogFlow(("%s-%d: drvHostBaseDestruct: iInstance=%d\n", pDrvIns->pDrvReg->szDriverName, pDrvIns->iInstance, pDrvIns->iInstance));

    /*
     * Terminate the thread.
     */
    if (pThis->ThreadPoller != NIL_RTTHREAD)
    {
        pThis->fShutdownPoller = true;
        int rc;
        int cTimes = 50;
        do
        {
#ifdef __WIN__
            if (pThis->hwndDeviceChange)
                PostMessage(pThis->hwndDeviceChange, WM_CLOSE, 0, 0); /* default win proc will destroy the window */
#else
            RTSemEventSignal(pThis->EventPoller);
#endif
            rc = RTThreadWait(pThis->ThreadPoller, 100, NULL);
        } while (cTimes-- > 0 && rc == VERR_TIMEOUT);

        if (!rc)
            pThis->ThreadPoller = NIL_RTTHREAD;
    }

    /*
     * Unlock the drive if we've locked it.
     */
    if (    pThis->fLocked
        &&  pThis->FileDevice != NIL_RTFILE
        &&  pThis->pfnDoLock)
    {
        int rc = pThis->pfnDoLock(pThis, false);
        if (VBOX_SUCCESS(rc))
            pThis->fLocked = false;
    }

    /*
     * Cleanup the other resources.
     */
#ifdef __WIN__
    if (pThis->hwndDeviceChange)
    {
        if (SetWindowLongPtr(pThis->hwndDeviceChange, GWLP_USERDATA, 0) == (LONG_PTR)pThis)
            PostMessage(pThis->hwndDeviceChange, WM_CLOSE, 0, 0); /* default win proc will destroy the window */
        pThis->hwndDeviceChange = NULL;
    }
#else
    if (pThis->EventPoller != NULL)
    {
        RTSemEventDestroy(pThis->EventPoller);
        pThis->EventPoller = NULL;
    }
#endif

    if (pThis->FileDevice != NIL_RTFILE)
    {
        int rc = RTFileClose(pThis->FileDevice);
        AssertRC(rc);
        pThis->FileDevice = NIL_RTFILE;
    }

    if (pThis->pszDevice)
    {
        MMR3HeapFree(pThis->pszDevice);
        pThis->pszDevice = NULL;
    }

    if (pThis->pszDeviceOpen)
    {
        RTStrFree(pThis->pszDeviceOpen);
        pThis->pszDeviceOpen = NULL;
    }

    if (RTCritSectIsInitialized(&pThis->CritSect))
        RTCritSectDelete(&pThis->CritSect);
}


/**
 * Initializes the instance data (init part 1).
 *
 * The driver which derives from this base driver will override function pointers after
 * calling this method, and complete the construction by calling DRVHostBaseInitFinish().
 *
 * On failure call DRVHostBaseDestruct().
 *
 * @returns VBox status code.
 * @param   pDrvIns         Driver instance.
 * @param   pCfgHandle      Configuration handle.
 * @param   enmType         Device type.
 */
int DRVHostBaseInitData(PPDMDRVINS pDrvIns, PCFGMNODE pCfgHandle, PDMBLOCKTYPE enmType)
{
    PDRVHOSTBASE pThis = PDMINS2DATA(pDrvIns, PDRVHOSTBASE);
    LogFlow(("%s-%d: DRVHostBaseInitData: iInstance=%d\n", pDrvIns->pDrvReg->szDriverName, pDrvIns->iInstance, pDrvIns->iInstance));

    /*
     * Initialize most of the data members.
     */
    pThis->pDrvIns                          = pDrvIns;
    pThis->ThreadPoller                     = NIL_RTTHREAD;
    pThis->FileDevice                       = NIL_RTFILE;
    pThis->enmType                          = enmType;

    pThis->pfnGetMediaSize                  = drvHostBaseGetMediaSize;

    /* IBase. */
    pDrvIns->IBase.pfnQueryInterface        = drvHostBaseQueryInterface;

    /* IBlock. */
    pThis->IBlock.pfnRead                   = drvHostBaseRead;
    pThis->IBlock.pfnWrite                  = drvHostBaseWrite;
    pThis->IBlock.pfnFlush                  = drvHostBaseFlush;
    pThis->IBlock.pfnIsReadOnly             = drvHostBaseIsReadOnly;
    pThis->IBlock.pfnGetSize                = drvHostBaseGetSize;
    pThis->IBlock.pfnGetType                = drvHostBaseGetType;
    pThis->IBlock.pfnGetUuid                = drvHostBaseGetUuid;

    /* IBlockBios. */
    pThis->IBlockBios.pfnGetGeometry        = drvHostBaseGetGeometry;
    pThis->IBlockBios.pfnSetGeometry        = drvHostBaseSetGeometry;
    pThis->IBlockBios.pfnGetTranslation     = drvHostBaseGetTranslation;
    pThis->IBlockBios.pfnSetTranslation     = drvHostBaseSetTranslation;
    pThis->IBlockBios.pfnIsVisible          = drvHostBaseIsVisible;
    pThis->IBlockBios.pfnGetType            = drvHostBaseBiosGetType;

    /* IMount. */
    pThis->IMount.pfnMount                  = drvHostBaseMount;
    pThis->IMount.pfnUnmount                = drvHostBaseUnmount;
    pThis->IMount.pfnIsMounted              = drvHostBaseIsMounted;
    pThis->IMount.pfnLock                   = drvHostBaseLock;
    pThis->IMount.pfnUnlock                 = drvHostBaseUnlock;
    pThis->IMount.pfnIsLocked               = drvHostBaseIsLocked;

    /*
     * Get the IBlockPort & IMountNotify interfaces of the above driver/device.
     */
    pThis->pDrvBlockPort = (PPDMIBLOCKPORT)pDrvIns->pUpBase->pfnQueryInterface(pDrvIns->pUpBase, PDMINTERFACE_BLOCK_PORT);
    if (!pThis->pDrvBlockPort)
    {
        AssertMsgFailed(("Configuration error: No block port interface above!\n"));
        return VERR_PDM_MISSING_INTERFACE_ABOVE;
    }
    pThis->pDrvMountNotify = (PPDMIMOUNTNOTIFY)pDrvIns->pUpBase->pfnQueryInterface(pDrvIns->pUpBase, PDMINTERFACE_MOUNT_NOTIFY);

    /*
     * Query configuration.
     */
    /* Device */
    int rc = CFGMR3QueryStringAlloc(pCfgHandle, "Path", &pThis->pszDevice);
    if (VBOX_FAILURE(rc))
    {
        AssertMsgFailed(("Configuration error: query for \"Path\" string returned %Vra.\n", rc));
        return rc;
    }

    /* Mountable */
    uint32_t u32;
    rc = CFGMR3QueryU32(pCfgHandle, "Interval", &u32);
    if (VBOX_SUCCESS(rc))
        pThis->cMilliesPoller = u32;
    else if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        pThis->cMilliesPoller = 1000;
    else if (VBOX_FAILURE(rc))
    {
        AssertMsgFailed(("Configuration error: Query \"Mountable\" resulted in %Vrc.\n", rc));
        return rc;
    }

    /* ReadOnly */
    rc = CFGMR3QueryBool(pCfgHandle, "ReadOnly", &pThis->fReadOnlyConfig);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        pThis->fReadOnlyConfig = enmType == PDMBLOCKTYPE_DVD || enmType == PDMBLOCKTYPE_CDROM ? true : false;
    else if (VBOX_FAILURE(rc))
    {
        AssertMsgFailed(("Configuration error: Query \"ReadOnly\" resulted in %Vrc.\n", rc));
        return rc;
    }

    /* Locked */
    rc = CFGMR3QueryBool(pCfgHandle, "Locked", &pThis->fLocked);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        pThis->fLocked = false;
    else if (VBOX_FAILURE(rc))
    {
        AssertMsgFailed(("Configuration error: Query \"Locked\" resulted in %Vrc.\n", rc));
        return rc;
    }

    /* BIOS visible */
    rc = CFGMR3QueryBool(pCfgHandle, "BIOSVisible", &pThis->fBiosVisible);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        pThis->fBiosVisible = true;
    else if (VBOX_FAILURE(rc))
    {
        AssertMsgFailed(("Configuration error: Query \"BIOSVisible\" resulted in %Vrc.\n", rc));
        return rc;
    }

    /* Uuid */
    char *psz;
    rc = CFGMR3QueryStringAlloc(pCfgHandle, "Uuid", &psz);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        RTUuidClear(&pThis->Uuid);
    else if (VBOX_SUCCESS(rc))
    {
        rc = RTUuidFromStr(&pThis->Uuid, psz);
        if (VBOX_FAILURE(rc))
        {
            AssertMsgFailed(("Configuration error: Uuid from string failed on \"%s\", rc=%Vrc.\n", psz, rc));
            MMR3HeapFree(psz);
            return rc;
        }
        MMR3HeapFree(psz);
    }
    else
    {
        AssertMsgFailed(("Configuration error: Failed to obtain the uuid, rc=%Vrc.\n", rc));
        return rc;
    }

    /* name to open & watch for */
#ifdef __WIN__
    int iBit = toupper(pThis->pszDevice[0]) - 'A';
    if (    iBit > 'Z' - 'A'
        ||  pThis->pszDevice[1] != ':'
        ||  pThis->pszDevice[2])
    {
        AssertMsgFailed(("Configuration error: Invalid drive specification: '%s'\n", pThis->pszDevice));
        return VERR_INVALID_PARAMETER;
    }
    pThis->fUnitMask = 1 << iBit;
    RTStrAPrintf(&pThis->pszDeviceOpen, "\\\\.\\%s", pThis->pszDevice);
#else
    pThis->pszDeviceOpen = RTStrDup(pThis->pszDevice);
#endif
    if (!pThis->pszDeviceOpen)
        return VERR_NO_MEMORY;

    return VINF_SUCCESS;
}


/**
 * Do the 2nd part of the init after the derived driver has overridden the defaults.
 *
 * On failure call DRVHostBaseDestruct().
 *
 * @returns VBox status code.
 * @param   pThis       Pointer to the instance data.
 */
int DRVHostBaseInitFinish(PDRVHOSTBASE pThis)
{
    PPDMDRVINS pDrvIns = pThis->pDrvIns;

    /* log config summary */
    Log(("%s-%d: pszDevice='%s' (%s) cMilliesPoller=%d fReadOnlyConfig=%d fLocked=%d fBIOSVisible=%d Uuid=%Vuuid\n",
         pDrvIns->pDrvReg->szDriverName, pDrvIns->iInstance, pThis->pszDevice, pThis->pszDeviceOpen, pThis->cMilliesPoller,
         pThis->fReadOnlyConfig, pThis->fLocked, pThis->fBiosVisible, &pThis->Uuid));

    /*
     * Check that there are no drivers below us.
     */
    PPDMIBASE pBase;
    int rc = pDrvIns->pDrvHlp->pfnAttach(pDrvIns, &pBase);
    if (rc != VERR_PDM_NO_ATTACHED_DRIVER)
    {
        AssertMsgFailed(("Configuration error: No attached driver, please! (rc=%Vrc)\n", rc));
        return VERR_PDM_DRVINS_NO_ATTACH;
    }

    /*
     * Register saved state.
     */
    rc = pDrvIns->pDrvHlp->pfnSSMRegister(pDrvIns, pDrvIns->pDrvReg->szDriverName, pDrvIns->iInstance, 1, 0,
                                          NULL, NULL, NULL,
                                          NULL, NULL, drvHostBaseLoadDone);
    if (VBOX_FAILURE(rc))
        return rc;

    /*
     * Verify type.
     */
#ifdef __WIN__
    UINT uDriveType = GetDriveType(pThis->pszDevice);
    switch (pThis->enmType)
    {
        case PDMBLOCKTYPE_FLOPPY_360:
        case PDMBLOCKTYPE_FLOPPY_720:
        case PDMBLOCKTYPE_FLOPPY_1_20:
        case PDMBLOCKTYPE_FLOPPY_1_44:
        case PDMBLOCKTYPE_FLOPPY_2_88:
            if (uDriveType != DRIVE_REMOVABLE)
            {
                AssertMsgFailed(("Configuration error: '%s' is not a floppy (type=%d)\n",
                                 pThis->pszDevice, uDriveType));
                return VERR_INVALID_PARAMETER;
            }
            break;
        case PDMBLOCKTYPE_CDROM:
        case PDMBLOCKTYPE_DVD:
            if (uDriveType != DRIVE_CDROM)
            {
                AssertMsgFailed(("Configuration error: '%s' is not a cdrom (type=%d)\n",
                                 pThis->pszDevice, uDriveType));
                return VERR_INVALID_PARAMETER;
            }
            break;
        case PDMBLOCKTYPE_HARD_DISK:
        default:
            AssertMsgFailed(("enmType=%d\n", pThis->enmType));
            return VERR_INVALID_PARAMETER;
    }
#endif

    /*
     * Open the device.
     */
    rc = drvHostBaseReopen(pThis);
    if (VBOX_FAILURE(rc))
    {
        AssertMsgFailed(("Could not open host device %s, rc=%Vrc\n", pThis->pszDevice, rc));
        pThis->FileDevice = NIL_RTFILE;
        return rc;
    }
#ifdef __WIN__
    DRVHostBaseMediaPresent(pThis);
#endif

    /*
     * Lock the drive if that's required by the configuration.
     */
    if (pThis->fLocked)
    {
        if (pThis->pfnDoLock)
            rc = pThis->pfnDoLock(pThis, true);
        if (VBOX_FAILURE(rc))
        {
            AssertMsgFailed(("Failed to lock the dvd drive. rc=%Vrc\n", rc));
            return rc;
        }
    }

#ifndef __WIN__
    /*
     * Create the event semaphore which the poller thread will wait on.
     */
    rc = RTSemEventCreate(&pThis->EventPoller);
    if (VBOX_FAILURE(rc))
        return rc;
#endif

    /*
     * Initialize the critical section used for serializing the access to the media.
     */
    rc = RTCritSectInit(&pThis->CritSect);
    if (VBOX_FAILURE(rc))
        return rc;

    /*
     * Start the thread which will poll for the media.
     */
    rc = RTThreadCreate(&pThis->ThreadPoller, drvHostBaseMediaThread, pThis, 0,
                        RTTHREADTYPE_INFREQUENT_POLLER, RTTHREADFLAGS_WAITABLE, "DVDMEDIA");
    if (VBOX_FAILURE(rc))
    {
        AssertMsgFailed(("Failed to create poller thread. rc=%Vrc\n", rc));
        return rc;
    }

    /*
     * Wait for the thread to start up (!w32:) and do one detection loop.
     */
    rc = RTThreadUserWait(pThis->ThreadPoller, 10000);
    AssertRC(rc);
#ifdef __WIN__
    if (!pThis->hwndDeviceChange)
        return VERR_GENERAL_FAILURE;
#endif

    return rc;
}

