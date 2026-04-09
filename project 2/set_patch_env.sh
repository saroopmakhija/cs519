#! /bin/bash
export p21=./linux-5.15.0
export patch_dirs=(mm/filemap.c arch/x86/entry/syscalls/syscall_64.tbl include/linux/syscalls.h include/linux/mm.h mm/memory.c mm/Makefile)
export out_dirs=(filemap_c.patch syscall_64.patch syscall_h.patch mm_h.patch memory_c.patch mm_makefile.patch)
