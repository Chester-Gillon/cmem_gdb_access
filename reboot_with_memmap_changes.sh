#!/bin/bash
# Use kexec to reboot the running Kernel with with changes to the memmap arguments.
# This is to try a temporary change to add memmap arguments to use the cmem_gdb_access,
# which can be done from the command line of the running system rather than needing
# to stop at the grub menu during boot and manual edit the Kernel command line.
#
# Also allows temporarily changing memmap when don't access access to the console.
#
# An improvement would be to allow the amount of reserved memory to be specified,
# and determine a suitable space for the allocation, rather than the user having
# to specify the actual memmap options.

# Run a command, without applying word splitting to arguments, where an argument
# may be a quoted space delimited string.
# Taken from https://stackoverflow.com/a/42429180/4207678
run_command()
{
   "$@"
}

# Parse command line arguments
new_memmap_option=$1
if [ -z "${new_memmap_option}" ]
then
   echo "Usage: $0 <memmap_options>"
   exit 1
fi

# From the running Kernel build the files to be passed to kexec.
# This was created for AlmaLinux 8.9
running_kernel_version=$(uname -r)
running_kernel_bootfile=/boot/vmlinuz-${running_kernel_version}
running_kernel_initrd=/boot/initramfs-${running_kernel_version}.img
if [ ! -e ${running_kernel_bootfile} ]
then
   echo "Expected running kernel bootfile ${running_kernel_bootfile} doesn't exist"
   exit 1
fi
if [ ! -e ${running_kernel_initrd} ]
then
   echo "Expected running kernel initrd ${running_kernel_initrd} doesn't exist"
   exit 1
fi

# Get the current command line and remove any existing memmap option
current_args=$(</proc/cmdline)
new_args=()
for arg in ${current_args}
do
   case ${arg} in
      memmap=*)
         ;;
      *)
         new_args+=(${arg})
         ;;
   esac
done

# Append the new memmap option
new_args+=("memmap=${new_memmap_option}")

# Convert the array to a single string
new_args=${new_args[@]}

# Display what are about to do, and prompt for confirmation
echo About to:
echo sudo kexec -l ${running_kernel_bootfile} --initrd=${running_kernel_initrd} --append="\""${new_args}"\""
echo sudo shutdown -r now
echo
read -p "Are you sure you wish to continue?"
if [ "$REPLY" != "yes" ]; then
   echo "Aborting"
   exit
fi

# Reboot with the modified arguments
run_command sudo kexec -l ${running_kernel_bootfile} --initrd=${running_kernel_initrd} "--append=${new_args}"
sudo shutdown -r now

