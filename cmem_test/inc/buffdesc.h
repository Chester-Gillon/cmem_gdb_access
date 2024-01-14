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



#ifndef _BUFFDESC_H
#define _BUFFDESC_H
#include <stdint.h>
typedef struct   _cmem_host_buf_desc_t {
    uint64_t physAddr;            /* physical address ; also visible in the
                                     pci address space from root complex*/
    uint8_t *userAddr;            /* Host user space Virtual address */
    size_t length;              /* Length of host buffer */
    void    *buffInfoHandle;      /* Handle to identify each buffer descriptor */
} cmem_host_buf_desc_t;
/**
 *  Frame descriptor describes the data contained in a number of Buffer descriptors 
 *  For example a data can be present in a list of 3 buffer descriptors.     
 * 
 *       Buffer 1         Buffer 2         Buffer 3   numBuffers = 3
 *      -------------       ---------       ----------
 *     |        |////|     |/////////|     |//////|   |     
 * ----|        |////|-----|/////////|-----|//////|   |
 *     |        |////|     |/////////|     |//////|   |
 *      -------------       ----------      ----------
 *     |<------>|<------------------------------->|
 * frameStartOffset        frameSize
 * 
**/

typedef struct   _cmem_host_frame_desc_t {
    cmem_host_buf_desc_t  *bufDescP;   /* Buffer descriptor list */
    uint32_t numBuffers;              /* Number of buffers */
    uint32_t frameStartOffset;        /* Start offset of data in buffers  */
    uint32_t frameSize;               /* Length of data in buffers */
} cmem_host_frame_desc_t;

#endif /* _BUFFDESC_H */

