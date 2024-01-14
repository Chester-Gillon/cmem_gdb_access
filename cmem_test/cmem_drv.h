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



#ifndef _CMEM_DRV_H
#define _CMEM_DRV_H
#include <stdint.h>
#include "inc/buffdesc.h"

#define CMEM_VERBOSE

/* There are two types of allocation supported  */
/* Persistent : Always get teh same physical memory. This is useful
                during development, when host process exists and restarts
                and not neccessary to reset and re-download DSP, especially
                if DSP is using Host memory as Global shared memory across 
                all DSPs */
#define HOST_BUF_TYPE_PERSISTENT 0
/* Dynamic : Application has to make free up calls to free memory when exiting 
*/
#define HOST_BUF_TYPE_DYNAMIC    1

/**
 *  @brief Function cmem_drv_open() Open the cmem device registered by 
 *                                  Kernel driver
 *  @param[in, out]     none
 *  @retval        0 for success, -1 for failure
 *  @pre  
 *  @post 
 */
int32_t cmem_drv_open(void);
/**
 *  @brief Function cmem_drv_close() Closes the cmem device
 *  @param[in, out]     none
 *  @retval        0 for success, -1 for failure
 *  @pre  
 *  @post 
 */
int32_t cmem_drv_close(void);

/**
 *  @brief Function cmem_drv_alloc() Allocate contiguous host
           memory; Any other contiguous memory allocation scheme can be used by
           applications.
 *  @param[in]     num_of_buffers     Number of buffers
 *  @param[in]     num_dsps           Number of dsp devices 
 *  @param[in]     size of buffer     Size of buffer
 *  @param[in]     host_buf_type      Host Buffer type static or dynamic
 *  @param[out]    buf_desc	      Array of allocated buffers
 *  @retval        0: for success, -1 for failure 
 *  @pre  
 *  @post 
 */
int32_t cmem_drv_alloc(uint32_t num_of_buffers, size_t size_of_buffer, uint16_t type,  cmem_host_buf_desc_t buf_desc[]);
/**
 *  @brief Function cmem_drv_free() Free contiguous dma host
 *         memory; 
 *  @param[in]     num_of_buffers                  Number of buffers
 *  @param[in]     num_dsps                        Number of dsp devices 
 *  @param[in]     size of buffer                  Size of buffer
 *  @param[in]     host_buf_type                   Host Buffer type static or dynamic
 *  @param[out]    buf_desc			   Array of allocated buffers
 *  @retval        0: for success, -1 for failure 
 *  @pre  
 *  @post 
 */
int32_t cmem_drv_free(uint32_t num_of_buffers, uint16_t type,  cmem_host_buf_desc_t buf_desc[]);

#endif /* _CMEM_DRV_H */

