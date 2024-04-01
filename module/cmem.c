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


#include <linux/string.h>
#include <linux/sort.h>
#include <linux/mutex.h>
#include <linux/sched.h>

#include <linux/poll.h>
#include <linux/cdev.h>

#include <linux/slab.h>
#include <linux/kallsyms.h>

#include <asm/e820/api.h>


#include "cmem.h"

/* Used to create the cmem device */
static dev_t cmem_dev_id;
static struct cdev cmem_cdev;
static struct class *cmem_class;
static int cmem_major;
static int cmem_minor;

struct device *cmem_dev;


/* Defines one physically contiguous address region allocated by this module */
typedef struct
{
    /* The start address of the region */
    uint64_t start;
    /* The inclusive end address of the region */
    uint64_t end;
    /* Defines if the region is in-use:
     * - false means free for allocation
     * - true means has been allocated */
    bool allocated;
    /* When allocate is true which process performed the allocation.
     * Used to automatically free the allocation if the process terminates. */
    pid_t allocation_pid;
} cmem_allocation_region_t;


/* Used to perform allocations of physically contiguous address regions to user processes.
 * No specific alignment for allocations is performed by this module. */
typedef struct
{
    /* Dynamically sized array of regions.
     *
     * Initialised to free regions.
     * Updated as regions are allocated and freed. */
    cmem_allocation_region_t *regions;
    /* The current number of valid entries in the regions[] array */
    uint32_t num_regions;
    /* The current allocated length of the regions[] array, dynamically grown as required */
    uint32_t regions_allocated_length;
} cmem_allocation_regions_t;
static cmem_allocation_regions_t cmem_allocation_regions;

/* mutex used to protect cmem_allocation_regions from operations from multiple processes */
static DEFINE_MUTEX (cmem_allocation_regions_lock);


static struct vm_operations_struct custom_vm_ops = {
    .access = generic_access_phys,
};


/**
 * @brief sort comparison function for cmem regions, which compares the start values
 */
static int cmem_region_compare (const void *const compare_a, const void *const compare_b)
{
    const cmem_allocation_region_t *const region_a = compare_a;
    const cmem_allocation_region_t *const region_b = compare_b;

    if (region_a->start < region_b->start)
    {
        return -1;
    }
    else if (region_a->start == region_b->start)
    {
        return 0;
    }
    else
    {
        return 1;
    }
}


/**
 * @brief Append a new cmem region to the end of array.
 * @details This is a helper function which can leave the regions[] un-sorted
 * @param[in/out] allocator Contains the cmem regions to modify
 * @param[in] new_region The new cmem region to append
 */
static void cmem_append_region (cmem_allocation_regions_t *const allocator, const cmem_allocation_region_t *const new_region)
{
    const uint32_t grow_length = 64;

    /* Increase the allocated length of the array to ensure has space for the region */
    if (allocator->num_regions == allocator->regions_allocated_length)
    {
        size_t required_size;

        allocator->regions_allocated_length += grow_length;
        required_size = allocator->regions_allocated_length * sizeof (allocator->regions[0]);
        allocator->regions = krealloc (allocator->regions, required_size, GFP_KERNEL);

        if (allocator->regions == NULL)
        {
            dev_err (cmem_dev, "krealloc of %zu bytes failed", required_size);
            return;
        }
    }

    /* Append the new region to the end of the array. May not be address order, is sorted later */
    allocator->regions[allocator->num_regions] = *new_region;
    allocator->num_regions++;
}


/**
 * @brief Remove one cmem region, shuffling down the following entries in the array
 * @param[in/out] allocator Contains the cmem regions to modify
 * @param[in] region_index Identifies which region to remove
 */
static void cmem_remove_region (cmem_allocation_regions_t *const allocator, const uint32_t region_index)
{
    const uint32_t num_regions_to_shuffle = allocator->num_regions - region_index;

    memmove (&allocator->regions[region_index], &allocator->regions[region_index + 1],
            sizeof (allocator->regions[region_index]) * num_regions_to_shuffle);
    allocator->num_regions--;
}


/**
 * @brief Update the array of cmem regions with a new region.
 * @brief The new region can either:
 *        a. Add a free region at initialisation.
 *        b. Mark a region as allocated. This may split an existing free region.
 *        c. Free a previously allocated region. This may combine adjacent free regions.
 * @param[in/out] allocator Contains the cmem regions to update
 * @param[in] new_region Defines the new region
 */
static void cmem_update_regions (cmem_allocation_regions_t *const allocator, const cmem_allocation_region_t *const new_region)
{
    bool new_region_processed = false;
    uint32_t region_index;

    /* Search for which existing region the region overlaps */
    for (region_index = 0; !new_region_processed && (region_index < allocator->num_regions); region_index++)
    {
        /* Take a copy of the existing region, since cmem_append_region() may change the pointers */
        const cmem_allocation_region_t existing_region = allocator->regions[region_index];

        if ((new_region->start >= existing_region.start) && (new_region->end <= existing_region.end))
        {
            /* Remove the existing region */
            cmem_remove_region (allocator, region_index);

            /* If the new region doesn't start at the beginning of the existing region, then append a region
             * with the same allocated state as the existing region to fill up to the start of the new region */
            if (new_region->start > existing_region.start)
            {
                const cmem_allocation_region_t before_region =
                {
                    .start = existing_region.start,
                    .end = new_region->start - 1,
                    .allocated = existing_region.allocated,
                    .allocation_pid = existing_region.allocated ? existing_region.allocation_pid : -1
                };

                cmem_append_region (allocator, &before_region);
            }

            /* Append the new region */
            cmem_append_region (allocator, new_region);

            /* If the new region ends before that of the existing region, then append a region with the same
             * allocated state as the existing region to fill up to the end of the existing region */
            if (new_region->end < existing_region.end)
            {
                const cmem_allocation_region_t after_region =
                {
                    .start = new_region->end + 1,
                    .end = existing_region.end,
                    .allocated = existing_region.allocated,
                    .allocation_pid = existing_region.allocated ? existing_region.allocation_pid : -1
                };

                cmem_append_region (allocator, &after_region);
            }

            new_region_processed = true;
        }
    }

    if (!new_region_processed)
    {
        /* The new region doesn't overlap an existing region. Must be inserting free regions at initialisation */
        cmem_append_region (allocator, new_region);
    }

    /* Sort the cmem regions into ascending start order */
    sort (allocator->regions, allocator->num_regions, sizeof (allocator->regions[0]), cmem_region_compare, NULL);

    /* Combine adjacent cmem regions which are free.
     * Adjacent cmem regions which are allocated need to be kept as separate regions to support freeing them
     * automatically when the allocating process exits. */
    region_index = 0;
    while ((region_index + 1) < allocator->num_regions)
    {
        cmem_allocation_region_t *const this_region = &allocator->regions[region_index];
        const uint32_t next_region_index = region_index + 1;
        const cmem_allocation_region_t *const next_region = &allocator->regions[next_region_index];

        if (((this_region->end + 1) == next_region->start) && !this_region->allocated && !next_region->allocated)
        {
            this_region->end = next_region->end;
            cmem_remove_region (allocator, next_region_index);
        }
        else if (this_region->end >= next_region->start)
        {
            /* Bug if the adjacent IOVA regions are overlapping */
            dev_err (cmem_dev, "Adjacent iova_regions overlap");
        }
        else
        {
            region_index++;
        }
    }
}


/**
 * @brief Attempt to perform an cmem allocation, by searching the free cmem regions
 * @param[in] cmd CMEM_IOCTL_ALLOC_A32_HOST_BUFFERS or CMEM_IOCTL_ALLOC_A64_HOST_BUFFERS to indicate the type of allocation.
 * @param[in/out] allocator Contains the cmem regions to allocate from
 * @param[in] min_start Minimum start IOVA to use for the allocation.
 *                      May be non-zero to cause a 64-bit DMA capable device to initially avoid the
 *                      first 4 GiB of address space.
 * @param[in] length The length of the allocation required
 * @param[out] region The allocated region. Success is indicated when allocated is true
 */
static void cmem_attempt_allocation (const unsigned int cmd,
                                     cmem_allocation_regions_t *const allocator,
                                     const uint64_t min_start, const size_t length,
                                     cmem_allocation_region_t *const region)
{
    const uint64_t max_a32_end = 0xffffffffUL;
    uint32_t region_index;
    size_t min_unused_space;

    /* Search for the smallest existing free region in which the aligned size will fit,
     * to try and reduce running out of IOVA addresses due to fragmentation. */
    min_unused_space = 0;
    for (region_index = 0; region_index < allocator->num_regions; region_index++)
    {
        const cmem_allocation_region_t *const existing_region = &allocator->regions[region_index];

        if (existing_region->allocated)
        {
            /* Skip this region, as not free */
        }
        else if (existing_region->end < min_start)
        {
            /* Skip this region, as all of it is below the minimum start */
        }
        else if ((cmd == CMEM_IOCTL_ALLOC_A32_HOST_BUFFERS) && (existing_region->start > max_a32_end))
        {
            /* Skip this region, as all of it is above what can be addressed the device which is only 32-bit capable */
        }
        else
        {
            /* Limit the usable start for the region to the minimum */
            const uint64_t usable_region_start = (existing_region->start >= min_start) ? existing_region->start : min_start;

            /* When the device is only 32-bit address capable limit the end to the first 4 GiB */
            const uint64_t usable_region_end =
                    ((cmd == CMEM_IOCTL_ALLOC_A32_HOST_BUFFERS) && (max_a32_end < existing_region->end)) ?
                            max_a32_end : existing_region->end;

            const uint64_t usable_region_size = (usable_region_end + 1) - usable_region_start;

            if (usable_region_size >= length)
            {
                const uint64_t region_unused_space = usable_region_size - length;

                if (!region->allocated || (region_unused_space < min_unused_space))
                {
                    region->start = usable_region_start;
                    region->end = region->start + (length - 1);
                    region->allocated = true;
                    region->allocation_pid = task_pid_nr (current);
                    min_unused_space = region_unused_space;
                }
            }
        }
    }
}


/**
 * @brief Allocate a cmem region for use by a DMA mapping for a device
 * @param[in] cmd CMEM_IOCTL_ALLOC_A32_HOST_BUFFERS or CMEM_IOCTL_ALLOC_A64_HOST_BUFFERS to indicate the type of allocation.
 * @param[in/out] allocator Contains the cmem regions to allocate from
 * @param[in] length The length of the allocation required
 * @param[out] region The allocated region. Success is indicated when allocated is true
 */
static void cmem_allocate_region (const unsigned int cmd,
                                  cmem_allocation_regions_t *const allocator,
                                  const size_t length,
                                  cmem_allocation_region_t *const region)
{
    /* Default to no allocation */
    region->start = 0;
    region->end = 0;
    region->allocated = false;
    region->allocation_pid = -1;

    if (cmd == CMEM_IOCTL_ALLOC_A64_HOST_BUFFERS)
    {
        /* For 64-bit capable devices first attempt to allocate addresses above the first 4 GiB,
         * to try and keep the first 4 GiB for devices which are only 32-bit capable. */
        const uint64_t a64_min_start = 0x100000000UL;

        cmem_attempt_allocation (cmd, allocator, a64_min_start, length, region);
    }

    /* If allocation wasn't successful, or only a 32-bit capable device, try the allocation with no minimum start */
    if (!region->allocated)
    {
        cmem_attempt_allocation (cmd, allocator, 0, length, region);
    }

    if (region->allocated)
    {
        /* Record the region as now allocated */
        cmem_update_regions (allocator, region);
    }
}


/**
* cmem_ioctl() - Application interface for cmem module to allocate or free contiguous memory regions
*/
static long cmem_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    cmem_ioctl_t cmem_ioctl_arg;
    int ret = 0;
    uint32_t buffer_index;
    uint32_t region_index;

    mutex_lock (&cmem_allocation_regions_lock);

    switch (cmd)
    {
    case CMEM_IOCTL_ALLOC_A64_HOST_BUFFERS:
    case CMEM_IOCTL_ALLOC_A32_HOST_BUFFERS:
        {
            cmem_allocation_region_t allocated_region;

            /* Allocate the specified buffers */
            if (copy_from_user (&cmem_ioctl_arg, (cmem_ioctl_t *) arg, sizeof (cmem_ioctl_arg)))
            {
                ret = -EFAULT;
            }
            else if (cmem_ioctl_arg.host_buf_info.num_buffers > CMEM_MAX_BUF_PER_ALLOC)
            {
                ret = -EINVAL;
            }
            else
            {
                for (buffer_index = 0; (ret == 0) && (buffer_index < cmem_ioctl_arg.host_buf_info.num_buffers); buffer_index++)
                {
                    cmem_host_buf_entry_t *const buffer = &cmem_ioctl_arg.host_buf_info.buf_info[buffer_index];

                    cmem_allocate_region (cmd, &cmem_allocation_regions, buffer->length, &allocated_region);
                    if (allocated_region.allocated)
                    {
                        buffer->dma_address = allocated_region.start;
                    }
                    else
                    {
                        /* Indicate the individual allocation failed, and indicate an overall failure */
                        buffer->length = 0;
                        buffer->dma_address = 0;
                        ret = -ENOMEM;
                    }
                }

                if (copy_to_user ((cmem_ioctl_t *) arg, &cmem_ioctl_arg, sizeof (cmem_ioctl_arg)))
                {
                    ret = -EFAULT;
                }
            }
        }
        break;

    case CMEM_IOCTL_FREE_HOST_BUFFERS:
        {
            /* Free the specified buffers, checking the current process performed the allocations */
            if (copy_from_user (&cmem_ioctl_arg, (cmem_ioctl_t *) arg, sizeof (cmem_ioctl_arg)))
            {
                ret = -EFAULT;
            }
            else if (cmem_ioctl_arg.host_buf_info.num_buffers > CMEM_MAX_BUF_PER_ALLOC)
            {
                ret = -EINVAL;
            }
            else
            {
                for (buffer_index = 0; (ret == 0) && (buffer_index < cmem_ioctl_arg.host_buf_info.num_buffers); buffer_index++)
                {
                    const cmem_host_buf_entry_t *const buffer = &cmem_ioctl_arg.host_buf_info.buf_info[buffer_index];
                    const cmem_allocation_region_t region_to_free =
                    {
                        .start = buffer->dma_address,
                        .end = buffer->dma_address + buffer->length - 1,
                        .allocated = false,
                        .allocation_pid = -1
                    };
                    bool region_found = false;

                    for (region_index = 0; !region_found && (region_index < cmem_allocation_regions.num_regions); region_index++)
                    {
                        const cmem_allocation_region_t *const existing_region = &cmem_allocation_regions.regions[region_index];

                        if ((region_to_free.start == existing_region->start) && (region_to_free.end == existing_region->end) &&
                            (task_pid_nr (current) == existing_region->allocation_pid))
                        {
                            cmem_update_regions (&cmem_allocation_regions, &region_to_free);
                            region_found = true;
                        }
                    }

                    if (!region_found)
                    {
                        ret = -EINVAL;
                    }
                }
            }
        }
        break;

    default:
        ret = -EINVAL;
        break;
    }

    mutex_unlock (&cmem_allocation_regions_lock);

    return ret;
}


/**
 * @brief When a process closes the cmem driver, free any outstanding allocations for the process
 */
int cmem_release (struct inode *const inodep, struct file *const filp)
{
    uint32_t region_index;
    bool region_removed;

    mutex_lock (&cmem_allocation_regions_lock);
    do
    {
        region_removed = false;
        for (region_index = 0; !region_removed && (region_index < cmem_allocation_regions.num_regions); region_index++)
        {
            const cmem_allocation_region_t *const existing_region = &cmem_allocation_regions.regions[region_index];

            if (existing_region->allocated && (existing_region->allocation_pid == task_pid_nr (current)))
            {
                const cmem_allocation_region_t region_to_free =
                {
                    .start = existing_region->start,
                    .end = existing_region->end,
                    .allocated = false,
                    .allocation_pid = -1
                };

                dev_info(cmem_dev, "Freed %#llx bytes from address %#llx for pid %d\n",
                        (existing_region->end + 1) - existing_region->start, existing_region->start, task_pid_nr (current));
                cmem_update_regions (&cmem_allocation_regions, &region_to_free);
                region_removed = true;
            }
        }
    } while (region_removed);
    mutex_unlock (&cmem_allocation_regions_lock);

    return 0;
}

/**
 * cmem_mmap() - Provide userspace mapping for specified kernel memory
 * @filp: File private data - ignored
 * @vma: User virtual memory area to map to
 */
static int cmem_mmap(struct file *const filp, struct vm_area_struct *const vma)
{
    int ret = -EINVAL;
    unsigned long sz = vma->vm_end - vma->vm_start;
    unsigned long long addr = (unsigned long long)vma->vm_pgoff << PAGE_SHIFT;

    sscanf(filp->f_path.dentry->d_name.name, CMEM_MODFILE);

    dev_info(cmem_dev, "Mapping %#lx bytes from address %#llx for pid %d\n",
            sz, addr, task_pid_nr (current));

    vma->vm_ops = &custom_vm_ops;
    ret = remap_pfn_range(vma, vma->vm_start,
            vma->vm_pgoff,
            sz, vma->vm_page_prot);

    return ret;
}


static unsigned int cmem_poll(struct file *const filp, poll_table *const wait)
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
    .poll           = cmem_poll,
    .release        = cmem_release
};


/**
 * @details Callback for parse_args() which extracts the value of reserved memory regions from memmap arguments.
 *          The reserved memory regions are validated, and if valid used to update the reserved memory areas.
 * @param[in] param The name of the parameter
 * @param[in] val The value of the parameter
 * @param[in] unused Not used
 * @param[in] arg Not used
 * @return Returns zero allow the parsing of parameters to continue
 */
static int cmem_boot_param_cb (char *const param, char *val, const char *const unused, void *const arg)
{
    unsigned long long region_size, region_start;
    cmem_allocation_region_t new_region;

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
                    new_region.start = region_start;
                    new_region.end = region_start + region_size;
                    new_region.allocated = false;
                    new_region.allocation_pid = -1;
                    cmem_update_regions (&cmem_allocation_regions, &new_region);
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

    if (cmem_allocation_regions.num_regions == 0)
    {
        pr_info(CMEM_DRVNAME " No reserved memory regions found\n");
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
    uint32_t region_index;
    int ret;

    mutex_init (&cmem_allocation_regions_lock);

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

    for (region_index = 0; region_index < cmem_allocation_regions.num_regions; region_index++)
    {
        const cmem_allocation_region_t *const region = &cmem_allocation_regions.regions[region_index];

        pr_info(CMEM_DRVNAME " Free Region start Addr : 0x%llx Size: 0x%llx\n",
                region->start, region->end - region->start);
    }

    return 0;

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
    /* Free memory reserved */
    if (cmem_allocation_regions.regions != NULL)
    {
        kfree (cmem_allocation_regions.regions);
    }
    device_destroy(cmem_class, MKDEV(cmem_major,0));

    class_destroy(cmem_class);
    cdev_del(&cmem_cdev);
    unregister_chrdev_region(cmem_dev_id, 1);
    pr_info(CMEM_DRVNAME "Module removed  \n");
}
module_exit(cmem_cleanup);
MODULE_LICENSE("Dual BSD/GPL");
