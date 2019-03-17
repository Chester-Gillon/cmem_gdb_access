cmem_gdb_access
===============

This is an example of a Linux Contiguous Memory Allocator, with an access function which allows gdb to access the mapped memory when debugging.

The module directory is a modified version of the cmem driver from the DESKTOP-LINUX-SDK 01_00_02_00 (available at http://software-dl.ti.com/sdoemb/sdoemb_public_sw/desktop_linux_sdk/latest/index_FDS.html)

The cmem driver uses the Linux generic_access_phys function, added as an access function in cmem_mmap.

A previous version had a custom_vma_access function which worked with a 2.6 series Linux Kernel, but with a 4.15 Linux Kernel caused a "BUG: unable to handle kernel paging request" when attempted to use GDB to inspect a virtual address mapped using cmem. Changing to use the generic_access_phys function then allowed the test to run with the 4.15 Linux Kernel.

The original custom_vma_access, and the subsequent change to use generic_access_phys, were listed on https://stackoverflow.com/questions/654393/examining-mmaped-addresses-using-gdb

The cmem_test directory contains an Eclipse project which tests the cmem driver by allocating some buffers from the cmem driver, and writing
a string into each buffer. By viewing the buffer_text variable in the debugger, the contents in the mapped buffer can be viewed in the debugger.

[with the original cmem driver from DESKTOP-LINUX-SDK 01_00_02_00 the lack of the access function meant that the the debugger reported errors
 of the form <Address 0x7ffff7ff8000 out of bounds> when attempted to view the buffer_test variable.

TODO:
1) The module/cmemcfg.h has been configured for a fixed reserved address range, corresponding to memmap=528m$7672m on the Linux command line.
Rather than compile in a hard-coded reserved mapping, it would be better to make the cmem driver parse the command line and use the memory which has been reserved.
