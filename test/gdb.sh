
qemu-riscv64 -L /home/wuxinlong/workspace/runtime/.tools/rootfs/riscv64 -g 12345 \
mono -v -v -v -v $*
# riscv64-unknown-linux-gnu-gdb -ex 'target remote localhost:12345' -ex 'b _start' -ex 'c' mono
