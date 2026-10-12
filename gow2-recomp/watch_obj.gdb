set pagination off
set width 0
break ppu_run
run
printf "### at ppu_run; vm_base = %p\n", vm_base
watch *(unsigned int *)(vm_base + 0x6FF480)
echo \n### continue to WRITE #1 ###\n
continue
echo \n### WRITE #1 backtrace ###\n
bt 16
echo \n### continue to WRITE #2 ###\n
continue
echo \n### WRITE #2 backtrace ###\n
bt 16
detach
quit
