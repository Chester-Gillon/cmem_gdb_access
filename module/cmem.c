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


#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>

#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/cdev.h>

#include <linux/dmaengine.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/kallsyms.h>

#include <asm/e820/api.h>

#include "cmem.h"

//#define DEBUG_ON
static dev_t cmem_dev_id;
static struct cdev cmem_cdev;
static struct class *cmem_class;
static int cmem_major;
static int cmem_minor;

struct device *cmem_dev;
cmem_host_buf_info_t *cmem_dyn_host_buf_info;
  

static spinlock_t l_lock;
static wait_queue_head_t l_read_wait;

typedef struct _reserved_mem_area_t {
   uint64_t start_addr;
   size_t size;
} reserved_mem_area_t;

static int num_memmap_reserved_areas;
static reserved_mem_area_t dynamic_mem_area;
 
uint64_t host_buf_alloc_ptr;
uint64_t host_dyn_buf_alloc_ptr;

static struct vm_operations_struct custom_vm_ops = {
    .access = generic_access_phys,
};
/**
* cmem_ioctl() - Application interface for cmem module
*
* 
*/
long cmem_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int ret = 0 ;

    switch (cmd) {
    case  CMEM_IOCTL_ALLOC_HOST_BUFFERS:
        {
            int i;
            cmem_ioctl_t cmem_ioctl_arg;
            cmem_ioctl_host_buf_info_t *const host_buf_info = &cmem_ioctl_arg.host_buf_info;

            if (copy_from_user (&cmem_ioctl_arg, (cmem_ioctl_t *) arg, sizeof (cmem_ioctl_arg)))
            {
                return -EFAULT;
            }
            /* Dynamic buffer allocation */
            if(cmem_dyn_host_buf_info)
            {
                cmem_host_buf_entry_t *tmp_buf_infoP;

                tmp_buf_infoP
                = kmalloc(((cmem_dyn_host_buf_info->num_buffers+host_buf_info->num_buffers)
                        *sizeof(cmem_host_buf_entry_t)), GFP_KERNEL);
                if(tmp_buf_infoP == NULL)
                {
                    dev_err(cmem_dev, "kmalloc 5 failed ");
                    return(-1);
                }
                memset(tmp_buf_infoP, 0,
                        ((cmem_dyn_host_buf_info->num_buffers+host_buf_info->num_buffers)
                                *sizeof(cmem_host_buf_entry_t)));
                /* copy and free old buffer  list */
                memcpy(tmp_buf_infoP, cmem_dyn_host_buf_info->buf_info,
                        cmem_dyn_host_buf_info->num_buffers*sizeof(cmem_host_buf_entry_t));
                kfree(cmem_dyn_host_buf_info->buf_info);
                cmem_dyn_host_buf_info->buf_info = tmp_buf_infoP;

            }
            else
            {
                cmem_dyn_host_buf_info = kmalloc(sizeof(cmem_host_buf_info_t), GFP_KERNEL);
                if(cmem_dyn_host_buf_info == NULL)
                {
                    dev_err(cmem_dev, "kmalloc 3 failed ");
                    return(-1);
                }

                memset(cmem_dyn_host_buf_info, 0, sizeof(cmem_host_buf_info_t));
                cmem_dyn_host_buf_info->buf_info
                = kmalloc((host_buf_info->num_buffers*sizeof(cmem_host_buf_entry_t)), GFP_KERNEL);
                if(cmem_dyn_host_buf_info->buf_info == NULL)
                {
                    dev_err(cmem_dev, "kmalloc 4 failed ");
                    goto err_kmalloc4;
                }
                memset(cmem_dyn_host_buf_info->buf_info, 0,
                        (host_buf_info->num_buffers*sizeof(cmem_host_buf_entry_t)));
            }
            for(i =0; i < host_buf_info->num_buffers; i++)
            {
                host_buf_info->buf_info[i].virtAddr = 0;
                if((host_dyn_buf_alloc_ptr+host_buf_info->buf_info[i].length) >
                (dynamic_mem_area.start_addr + dynamic_mem_area.size)) {
                    host_buf_info->buf_info[i].dmaAddr = 0;
                } else {
                    host_buf_info->buf_info[i].dmaAddr = host_dyn_buf_alloc_ptr;
                }

                host_dyn_buf_alloc_ptr += host_buf_info->buf_info[i].length;

                if(host_buf_info->buf_info[i].dmaAddr == 0 ) {
                    dev_err(cmem_dev, "Failed allocation of Dynamic Host memory %d\n", i);

                    return (-1);
                } else {
                    dev_info(cmem_dev,
                            "Allocated Host memory in Pcie space %d: Base Address: 0x%llx: Virtual Address 0x%p : Size 0x%x \n",
                            i, host_buf_info->buf_info[i].dmaAddr,
                            host_buf_info->buf_info[i].virtAddr,
                            (unsigned int)host_buf_info->buf_info[i].length);
                }

                /* Keep a local copy of this in driver */
                cmem_dyn_host_buf_info->buf_info[cmem_dyn_host_buf_info->num_buffers] = host_buf_info->buf_info[i];
                dev_info(cmem_dev, " Copied buffer %d, Base address: 0x%llx: Size 0x%zx \n",
                        cmem_dyn_host_buf_info->num_buffers,  cmem_dyn_host_buf_info->buf_info[cmem_dyn_host_buf_info->num_buffers].dmaAddr,
                        cmem_dyn_host_buf_info->buf_info[cmem_dyn_host_buf_info->num_buffers].length);
                cmem_dyn_host_buf_info->num_buffers++;
            }

            if (copy_to_user ((cmem_ioctl_t *) arg, &cmem_ioctl_arg, sizeof (cmem_ioctl_arg)))
            {
                return -EFAULT;
            }
        }
        break;

    case  CMEM_IOCTL_FREE_HOST_BUFFERS:
        {
            /* TODO: Currently free clears off all buffers :
              May need to provide dynamic alloc and free later*/
            cmem_ioctl_t cmem_ioctl_arg;

            if (copy_from_user (&cmem_ioctl_arg, (cmem_ioctl_t *) arg, sizeof (cmem_ioctl_arg)))
            {
                return -EFAULT;
            }
            host_dyn_buf_alloc_ptr = (dynamic_mem_area.start_addr);
            if(cmem_dyn_host_buf_info)
            {
                if(cmem_dyn_host_buf_info->buf_info)
                    kfree(cmem_dyn_host_buf_info->buf_info);
                kfree(cmem_dyn_host_buf_info);
                cmem_dyn_host_buf_info = NULL;
            }
            dev_info(cmem_dev,
                    "Freed  all Dynamic memory allocation \n");

        }
        break;

    default:
        ret = -1;
        break;
    }
    return ret;
    err_kmalloc4:
    kfree(cmem_dyn_host_buf_info);
    cmem_dyn_host_buf_info = NULL;
    return(-1);
}

/**
 * ti667x_ep_pcie_mmap() - Provide userspace mapping for specified kernel memory
 * @filp: File private data - ignored
 * @vma: User virtual memory area to map to
 *
 * At present, only allows mapping BAR1 & BAR2 spaces. It is assumed that these
 * BARs are internally translated to access ti667x L2SRAM and DDR RAM
 * respectively (application can ensure this using TI667X_PCI_SET_BAR_WINDOW
 * ioctl to setup proper translation on ti667x EP).
 *
 * Note that the application has to get the physical BAR address as assigned by
 * the host code. One way to achieve this is to use ioctl
 * TI667X_PCI_GET_BAR_INFO.
 */
int cmem_mmap(struct file *filp, struct vm_area_struct *vma)
{
    int ret = -EINVAL;
    unsigned long sz = vma->vm_end - vma->vm_start;
    unsigned long long addr = (unsigned long long)vma->vm_pgoff << PAGE_SHIFT;

    sscanf(filp->f_path.dentry->d_name.name, CMEM_MODFILE);

    dev_info(cmem_dev, "Mapping %#lx bytes from address %#llx\n",
            sz, addr);

    //  vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

    vma->vm_ops = &custom_vm_ops;
    ret = remap_pfn_range(vma, vma->vm_start,
            vma->vm_pgoff,
            sz, vma->vm_page_prot);

    return ret;
}


unsigned int cmem_poll(struct file *filp, poll_table *wait)
{
    return(0);
}

/**
* cmem_fops - Declares supported file access functions
*/
static const struct file_operations cmem_fops = {
    .owner          = THIS_MODULE,
    .mmap           = cmem_mmap,
    .unlocked_ioctl = cmem_ioctl,
    .poll           = cmem_poll
};


/**
 * @details Callback for parse_args() which extracts the value of reserved memory regions from memmap arguments.
 * The reserved memory regions are validated, and if valid used to update the reserved memory areas.
 * The first memmap entry for reserved is used for dynamic_mem_area
 * Any additional reserved memory areas are ignored.
 * @param[in] param The name of the parameter
 * @param[in] val The value of the parameter
 * @param[in] unused Not used
 * @param[in] arg Not used
 * @return Returns zero allow the parsing of parameters to continue
 */
static int cmem_boot_param_cb (char *param, char *val, const char *unused, void *arg)
{
    unsigned long long region_size, region_start; 
    reserved_mem_area_t *mem_area;

    if (strcmp (param, "memmap") == 0)
    {
        while (val != NULL)
        {
            char *seperator = strchr (val, ',');
            if (seperator != NULL)
            {
                *seperator++ = '\0';
            }

            region_size = memparse (val, &val);
            if (*val == '$')
            {
                region_start = memparse (val + 1, &val);

                switch (num_memmap_reserved_areas)
                {
                case 0: 
                    mem_area = &dynamic_mem_area;
                    break;

                default:
                    mem_area = NULL;
                    break;
                }

                if (mem_area != NULL)
                {
                    /* Sanity check that the memmap region extracted from the Kernel parameters is marked as RAM
                     * by the firmware and reserved by Linux. I.e. to only use real RAM not in use by Linux.
                     * @todo Uses the mapped <any> functions which are exported, but for a robust check should
                     *       check the entire region is of the specified type. */
                    if (!e820__mapped_raw_any (region_start, region_start + region_size, E820_TYPE_RAM))
                    {
                        pr_info(CMEM_DRVNAME " Ignored memmap start 0x%llx size 0x%llx not marked as RAM by firmware\n",
                                region_start, region_size);
                    }
                    else if (!e820__mapped_any (region_start, region_start + region_size, E820_TYPE_RESERVED))
                    {
                        pr_info(CMEM_DRVNAME " Ignored memmap start 0x%llx size 0x%llx not marked as reserved by Linux\n",
                                region_start, region_size);
                    }
                    else
                    {
                        mem_area->start_addr = region_start;
                        mem_area->size = region_size;
                        num_memmap_reserved_areas++;
                    }
                }
            }
            val = seperator;
        }
    }

    return 0;
}

/**
 * @details Get the memory areas to be used for contiguous memory allocations from the reserved memory regions
 *          specified in the memmap arguments in the Kernel parameters.
 * @returns Returns zero if the memory areas have been identified and the cmem driver can be loaded.
 *          Any other value indicates an error. 
 */
static int get_mem_areas_from_memmap_params (void)
{
    const char **lookup_saved_command_line;
    char *cmdline;

    char *(*parse_args_lookup)(const char *doing,
            char *args,
            const struct kernel_param *params,
            unsigned num,
            s16 min_level,
            s16 max_level,
            void *arg,
            int (*unknown)(char *param, char *val,
                    const char *doing, void *arg));

    /* Can't find an exported way of obtaining the Kernel command line, so lookup the saved_command_line variable */
    lookup_saved_command_line = (const char **) kallsyms_lookup_name ("saved_command_line");
    if ((lookup_saved_command_line == NULL) || ((*lookup_saved_command_line) == NULL))
    {
        pr_info(CMEM_DRVNAME " Failed to lookup saved_command_line\n");
        return -EINVAL;
    }

    /* Lookup the function to parse arguments, to re-use the parsing code which handles escaping of parameters */
    parse_args_lookup = (void *) kallsyms_lookup_name ("parse_args");
    if ((parse_args_lookup == NULL) || ((*parse_args_lookup) == NULL))
    {
        pr_info(CMEM_DRVNAME " Failed to lookup parse_args\n");
        return -EINVAL;
    }

    cmdline = kstrdup (*lookup_saved_command_line, GFP_KERNEL);
    parse_args_lookup("cmem params", cmdline, NULL, 0, 0, 0, NULL, &cmem_boot_param_cb);
    kfree (cmdline);

    if (dynamic_mem_area.size == 0)
    {
        pr_info(CMEM_DRVNAME " Request mem region failed: dynamic \n");
        return -EINVAL;
    }

    return 0;
}

/**
* cmem_init() - Initialize DMA Buffers device
*
* Initialize DMA buffers device.
*/

static int __init cmem_init(void)
{
    int ret;

    ret = get_mem_areas_from_memmap_params ();
    if (ret)
    {
        return ret;
    }

    ret = alloc_chrdev_region(&cmem_dev_id, 0, 1, CMEM_DRVNAME);
    if (ret) {
        pr_err(CMEM_DRVNAME ": could not allocate the character driver");
        return -1;
    }

    cmem_major = MAJOR(cmem_dev_id);
    cmem_minor = 0;

    cmem_class = class_create(THIS_MODULE, CMEM_DRVNAME);
    if (!cmem_class) {
        unregister_chrdev_region(cmem_dev_id, 1);   
        pr_err(CMEM_DRVNAME ": Failed to add device to sys fs\n");
        goto err_class_create ;
    }
    cdev_init(&cmem_cdev, &cmem_fops);
    cmem_cdev.owner = THIS_MODULE;
    cmem_cdev.ops = &cmem_fops;

    ret = cdev_add(&cmem_cdev, MKDEV(cmem_major, cmem_minor), 1);
    if (ret) {
        pr_err(CMEM_DRVNAME ": Failed creation of node\n");
        goto err_dev_add;
    }

    pr_info(CMEM_DRVNAME ": Major %d Minor %d assigned\n",
            cmem_major, cmem_minor);


    cmem_dev = device_create(cmem_class, NULL, MKDEV(cmem_major, cmem_minor),
            NULL, CMEM_MODFILE);
    if(cmem_dev < 0) {
        pr_info(CMEM_DRVNAME ": Error creating device \n");
        goto err_dev_create;
    }

    dev_info(cmem_dev, "Added device to the sys file system\n");
    cmem_dyn_host_buf_info = NULL;

    if( dynamic_mem_area.start_addr== 0)
    {
        pr_info(CMEM_DRVNAME " Request mem region failed: dynamic \n");
        goto dynamic_reserve_fail_cleanup;
    }
    pr_info(CMEM_DRVNAME " Dynamic Memory start Addr : 0x%llx Size: 0x%zx\n",dynamic_mem_area.start_addr,
            dynamic_mem_area.size);
    host_dyn_buf_alloc_ptr = (dynamic_mem_area.start_addr);

    spin_lock_init(&l_lock);
    init_waitqueue_head(&l_read_wait);
    return 0 ;

    dynamic_reserve_fail_cleanup:

    err_dev_create:
    cdev_del(&cmem_cdev);

    err_dev_add:
    class_destroy(cmem_class);
    err_class_create:
    unregister_chrdev_region(cmem_dev_id, 1);

    return(-1);
}
module_init(cmem_init);

/**
* cmem_cleanup() - Perform cleanups before module unload
*/
static void __exit cmem_cleanup(void)
{

    if(cmem_dyn_host_buf_info)
    {
        host_dyn_buf_alloc_ptr = (dynamic_mem_area.start_addr);
        if(cmem_dyn_host_buf_info->buf_info)
            kfree(cmem_dyn_host_buf_info->buf_info);
        kfree(cmem_dyn_host_buf_info);
    }
    /* Free memory reserved */
    device_destroy(cmem_class, MKDEV(cmem_major,0));

    class_destroy(cmem_class);
    cdev_del(&cmem_cdev);
    unregister_chrdev_region(cmem_dev_id, 1);
    pr_info(CMEM_DRVNAME "Module removed  \n");
}
module_exit(cmem_cleanup);
MODULE_LICENSE("Dual BSD/GPL");
