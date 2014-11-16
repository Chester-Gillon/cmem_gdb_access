cmem_gdb_access
===============

This is an example of a Linux Contiguous Memory Allocator, with an access function which allows gdb to access the mapped memory when debugging.

The module directory is a modified version of the cmem driver from the DESKTOP-LINUX-SDK 01_00_02_00 (available at http://software-dl.ti.com/sdoemb/sdoemb_public_sw/desktop_linux_sdk/latest/index_FDS.html)

The cmem driver has had the custom_vma_access and custom_generic_access_phys functions added, where custom_vma_access gets added as an access function
in cmem_mmap. The vm_private_data is used to store the (reserved) physical base address for the virtual address mapping.

The cmem_test directory contains an Eclipse project which tests the cmem driver by allocating some buffers from the cmem driver, and writing
a string into each buffer. By viewing the buffer_text variable in the debugger, the contents in the mapped buffer can be viewed in the debugger.

[with the original cmem driver from DESKTOP-LINUX-SDK 01_00_02_00 the lack of the access function meant that the the debugger reported errors
 of the form <Address 0x7ffff7ff8000 out of bounds> when attempted to view the buffer_test variable.

TODO:
1) On CentOS 6.6 64-bit the cmem_test program doesn't succesfully open the cmem device on the first load of the driver, calling module/unload.sh
followed by module/load.sh allows access.

2) The module/cmemcfg.h has been configured for a fixed reserved address range, corresponding to memmap=528m$7672m on the Linux command line.
Rather than compile in a hard-coded reserved mapping, it would be better to make the cmem driver parse the command line and use the memory which has been reserved.
