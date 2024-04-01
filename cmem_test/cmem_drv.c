/*
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com/ 
 * 
 * 
 *  Redistribution and use in source and binary forms, with or without 
 *  modification, are permitted provided that the following conditions 
 *  are met:
 *
 *    Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer.
 *
 *    Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the   
 *    distribution.
 *
 *    Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * 
 * 
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <semaphore.h>

#include <linux/ioctl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#include "cmem.h"
#include "cmem_drv.h"

static int32_t dev_desc;

static char* progname = "cmem_drv";

/* Local functions */
/**
 *  @brief Function cmem_drv_open() Open dma mem driver
 *  @param    none 
 *  @retval          0: success, -1 : failure
 *  @pre  
 *  @post 
 */
int32_t cmem_drv_open(void)
{
    char dev_name[128];

    sprintf(dev_name, CMEM_DRIVER_SIGNATURE);

    dev_desc = open(dev_name, O_RDWR);
    if (-1 == dev_desc) {
        fprintf(stderr, "%s: ERROR: DMA MEM Device \"%s\" could not opened\n", progname, dev_name);
        return -1;
    }
#ifdef CMEM_VERBOSE 
    printf("Opened DMA MEM device : %s (dev_desc = 0x%08X)\n",dev_name, dev_desc);
#endif
    return(0);
}
/**
 *  @brief Function cmem_drv_close() Close dma mem driver
 *  @param    none 
 *  @retval           0: success, -1 : failure
 *  @pre  
 *  @post 
 */
int32_t cmem_drv_close(void)
{
    close(dev_desc);
#ifdef CMEM_VERBOSE
    printf("Memory Driver closed \n");
#endif
    return(0);
}


/**
 * @brief Allocate physically contiguous host memory buffers, and map them into the address space of the calling process
 * @pram[in] dma_capability_a64 Determines the type of physical addresses to allocate:
 *                              - When false allocates physical addresses only in the first 4 GiB,
 *                                for devices which can only address 32-bits
 *                              - When true allocates addresses in any part of the physical address spaces,
 *                                for devices which can address 64-bits.
 * @param[in] dma_capability_a64 The number of buffers to allocate
 * @param[in] size_of_buffer The size of each buffer in bytes
 * @param[out] buf_desc The allocated buffers
 * @return Zero indicates success, any other value failure
 */
int32_t cmem_drv_alloc (const bool dma_capability_a64,
                        const uint32_t num_of_buffers, const size_t size_of_buffer,
                        cmem_host_buf_desc_t buf_desc[const num_of_buffers])
{
    const unsigned long command = dma_capability_a64 ? CMEM_IOCTL_ALLOC_A64_HOST_BUFFERS : CMEM_IOCTL_ALLOC_A32_HOST_BUFFERS;
    cmem_ioctl_t cmem_ioctl;
    uint32_t buffer_index = 0;
    uint32_t remaining_num_buffers = num_of_buffers;
    int rc = 0;

    while ((rc == 0) && (remaining_num_buffers > 0))
    {
        const uint32_t alloc_num_buffers = (remaining_num_buffers < CMEM_MAX_BUF_PER_ALLOC) ?
                remaining_num_buffers : CMEM_MAX_BUF_PER_ALLOC;

        /* Allocate physical address buffers */
        memset (&cmem_ioctl, 0, sizeof (cmem_ioctl));
        cmem_ioctl.host_buf_info.num_buffers = alloc_num_buffers;
        for (uint32_t ioctl_index = 0; ioctl_index < alloc_num_buffers; ioctl_index++)
        {
            cmem_ioctl.host_buf_info.buf_info[ioctl_index].length = size_of_buffer;
        }
        rc = ioctl (dev_desc, command, &cmem_ioctl);

        /* Map the buffers into the process address space */
        for (uint32_t ioctl_index = 0; (rc == 0) && (ioctl_index < alloc_num_buffers); ioctl_index++)
        {
#ifdef CMEM_VERBOSE
            printf("Debug: mmap param length 0x%zx, Addr: 0x%lx \n", cmem_ioctl.host_buf_info.buf_info[ioctl_index].length,
                    cmem_ioctl.host_buf_info.buf_info[ioctl_index].dma_address);
#endif
            errno = 0;
            buf_desc[buffer_index].userAddr = mmap (NULL,
                    cmem_ioctl.host_buf_info.buf_info[ioctl_index].length,
                    PROT_READ | PROT_WRITE,
                    MAP_SHARED,
                    dev_desc,
                    (off_t) cmem_ioctl.host_buf_info.buf_info[ioctl_index].dma_address);
            if (buf_desc[buffer_index].userAddr == MAP_FAILED)
            {
                rc = errno;
            }
            buf_desc[buffer_index].physAddr = cmem_ioctl.host_buf_info.buf_info[ioctl_index].dma_address;
            buf_desc[buffer_index].length = cmem_ioctl.host_buf_info.buf_info[ioctl_index].length;
#ifdef CMEM_VERBOSE
            printf("Buff num %d: Phys addr : 0x%lx User Addr: 0x%lx \n", buffer_index, buf_desc[buffer_index].physAddr,
                    (uintptr_t) buf_desc[buffer_index].userAddr);
#endif
            buffer_index++;
        }

        remaining_num_buffers -= alloc_num_buffers;
    }

    return rc;
}


/**
 * @brief Free contiguous DMA host buffers
 * @details This unmaps the host buffers from the process address space, and then free the physical address allocations.
 * @param[in] num_of_buffers The number of buffers to free
 * @param[in] buf_desc The array of buffers to free
 * @return Zero indicates success, any other value failure
 */
int32_t cmem_drv_free (const uint32_t num_of_buffers, const cmem_host_buf_desc_t buf_desc[const num_of_buffers])
{
    cmem_ioctl_t cmem_ioctl;
    uint32_t buffer_index = 0;
    uint32_t remaining_num_buffers = num_of_buffers;
    int rc = 0;

    while ((rc == 0) && (remaining_num_buffers > 0))
    {
        const uint32_t free_num_buffers = (remaining_num_buffers < CMEM_MAX_BUF_PER_ALLOC) ?
                remaining_num_buffers : CMEM_MAX_BUF_PER_ALLOC;

        memset (&cmem_ioctl, 0, sizeof (cmem_ioctl));
        cmem_ioctl.host_buf_info.num_buffers = free_num_buffers;
        for (uint32_t ioctl_index = 0; (rc == 0) && (ioctl_index < free_num_buffers); ioctl_index++)
        {
            cmem_ioctl.host_buf_info.buf_info[ioctl_index].dma_address = buf_desc[buffer_index].physAddr;
            cmem_ioctl.host_buf_info.buf_info[ioctl_index].length = buf_desc[buffer_index].length;
            rc = munmap ((void *)buf_desc[buffer_index].physAddr, buf_desc[buffer_index].length);
            buffer_index++;
        }
        rc = ioctl (dev_desc, CMEM_IOCTL_FREE_HOST_BUFFERS, &cmem_ioctl);

        remaining_num_buffers -= free_num_buffers;
    }

    return rc;
}

