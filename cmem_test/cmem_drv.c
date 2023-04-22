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
static cmem_ioctl_t cmem_ioctl;

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
 *  @brief Function cmem_drv_alloc() Allocate contiguous host
           memory; Any other contiguous memory allocation scheme can be used by
           applications.
 *  @param[in]     num_of_buffers                  Number of buffers
 *  @param[in]     size of buffer                  Size of buffer
 *  @param[in]     host_buf_type                   Host Buffer type static or dynamic
 *  @param[out]    buf_desc			   Array of allocated buffers
 *  @retval        0: for success, -1 for failure 
 *  @pre  
 *  @post 
 */

int32_t cmem_drv_alloc(uint32_t num_of_buffers, uint32_t size_of_buffer, uint16_t host_buf_type,  cmem_host_buf_desc_t buf_desc[])
{
  int i;
  
  {
    int buf_index, ret_val;
    uint32_t remaining_num_buffers, alloc_num_buffers;
    
    buf_index = 0;
    remaining_num_buffers = num_of_buffers;
    while (remaining_num_buffers) 
    {
      alloc_num_buffers = (remaining_num_buffers > CMEM_MAX_BUF_PER_ALLOC) ? 
        CMEM_MAX_BUF_PER_ALLOC : remaining_num_buffers;
      cmem_ioctl.host_buf_info.num_buffers =alloc_num_buffers;
      cmem_ioctl.host_buf_info.type  =host_buf_type;

      for(i=0; i< alloc_num_buffers; i++) {
	cmem_ioctl.host_buf_info.buf_info[i].length = size_of_buffer;
      }
      ret_val = ioctl(dev_desc, CMEM_IOCTL_ALLOC_HOST_BUFFERS, &cmem_ioctl);
      if(ret_val != 0) {
        fprintf(stderr, "%s: ERROR: DMA MEM Buffer allocation failed\n", progname);
        return(-1);
      }

      for(i=0; i< alloc_num_buffers; i++) {
        buf_desc[buf_index+i].physAddr = cmem_ioctl.host_buf_info.buf_info[i].dmaAddr;
#ifdef CMEM_VERBOSE
        printf("Debug: mmap param length 0x%x, Addr: 0x%llx \n", cmem_ioctl.host_buf_info.buf_info[i].length,
          (unsigned long long) cmem_ioctl.host_buf_info.buf_info[i].dmaAddr);
#endif
        buf_desc[buf_index+i].userAddr = mmap(0,
          cmem_ioctl.host_buf_info.buf_info[i].length,
          PROT_READ | PROT_WRITE,
          MAP_SHARED,
          dev_desc, 
          (off_t) cmem_ioctl.host_buf_info.buf_info[i].dmaAddr);
#ifdef CMEM_VERBOSE
       printf("Buff num %d: Phys addr : 0x%llx User Addr: 0x%lx \n", buf_index, (unsigned long long ) buf_desc[buf_index+i].physAddr,
         (size_t)buf_desc[buf_index+i].userAddr );
#endif
        buf_desc[buf_index+i].length = cmem_ioctl.host_buf_info.buf_info[i].length;
      }
      buf_index += (int) alloc_num_buffers;
      remaining_num_buffers-=alloc_num_buffers;
    }
  }
  return(0);
}
/**
 *  @brief Function cmem_drv_free() Free contiguous dma host
 *         memory; 
 *  @param[in]     num_of_buffers                  Number of buffers
 *  @param[in]     host_buf_type                   Host Buffer type static or dynamic
 *  @param[out]    buf_desc			   Array of allocated buffers
 *  @retval        0: for success, -1 for failure 
 *  @pre  
 *  @post 
 */
int32_t cmem_drv_free(uint32_t num_of_buffers, uint16_t host_buf_type,  cmem_host_buf_desc_t buf_desc[])
{
  int i;
  
  {
    int ret_val;

    cmem_ioctl.host_buf_info.num_buffers =num_of_buffers;
    cmem_ioctl.host_buf_info.type  =host_buf_type;

    ret_val = ioctl(dev_desc, CMEM_IOCTL_FREE_HOST_BUFFERS, &cmem_ioctl);
    if(ret_val != 0) {
      fprintf(stderr, "%s: ERROR: DMA MEM Buffer Free failed\n", progname);
      return(-1);
    }

    for(i=0; i< num_of_buffers; i++) {
    /* TODO: Add code to copy the buffer entries. right now this is not used */
    /* Free frees all the memory */
    }
  }
  return(0);
}

