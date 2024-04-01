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


#ifndef __CMEM_H__
#define __CMEM_H__

#define CMEM_DRVNAME     "cmem"
#define CMEM_MODFILE     "cmem"

#define CMEM_DRIVER_SIGNATURE "/dev/"CMEM_MODFILE

#ifdef __KERNEL__

#endif  /*  __KERNEL__  */
/* Maximum number of buffers allocated per API call*/
#define CMEM_MAX_BUF_PER_ALLOC 64

/* Basic information about host buffer accessible through PCIe */
typedef struct
{
    /* PCIe address */
    uint64_t dma_address;
    /* Length of host buffer */
    size_t length;
} cmem_host_buf_entry_t;

/* List of Buffers, to allocate or free */
typedef struct
{
    /* Number of host buffers in the buf_info[] array */
    uint32_t num_buffers;
    cmem_host_buf_entry_t buf_info[CMEM_MAX_BUF_PER_ALLOC];
} cmem_ioctl_host_buf_info_t;

/** Parameters used for calling IOCTL */
typedef struct
{
    cmem_ioctl_host_buf_info_t host_buf_info;
} cmem_ioctl_t;

/* IOCTLs defined for the application as well as driver.
 * CMEM_IOCTL_ALLOC_A64_HOST_BUFFERS allocates buffers for DMA which supports 64-bit addressing.
 * CMEM_IOCTL_ALLOC_A32_HOST_BUFFERS allocates buffers for DMA which only supports 32-bit addressing, and therefore
 * only performs allocation for the first 4 GiB of memory. */
#define CMEM_IOCTL_ALLOC_A64_HOST_BUFFERS  _IOWR('P', 1, cmem_ioctl_t)
#define CMEM_IOCTL_ALLOC_A32_HOST_BUFFERS  _IOWR('P', 2, cmem_ioctl_t)
#define CMEM_IOCTL_FREE_HOST_BUFFERS       _IOWR('P', 3, cmem_ioctl_t)

#endif
