/*
 * Copyright (c) 2003 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * The contents of this file constitute Original Code as defined in and
 * are subject to the Apple Public Source License Version 2.0 (the
 * "License").  You may not use this file except in compliance with the
 * License.  Please obtain a copy of the License at
 * http://www.apple.com/publicsource and read it before using this file.
 *
 * This Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */
/*
 *  load.c - Functions for decoding a Mach-o Kernel.
 *
 *  Copyright (c) 1998-2003 Apple Computer, Inc.
 *
 */

#include <mach-o/fat.h>
#include <mach-o/loader.h>
#include <mach/machine/thread_status.h>

#include <sl.h>

static long DecodeSegment(long cmdBase, unsigned int*load_addr, unsigned int *load_size);
static long DecodeSegment64(long cmdBase, unsigned int *load_addr, unsigned int *load_size);
static long DecodeUnixThread(long cmdBase, unsigned int *entry, int use_64bit_load_command);


static unsigned long gBinaryAddress;
BOOL   gHaveKernelCache;

// Public Functions

long ThinFatFile(void **binary, unsigned long *length)
{
    unsigned long nfat, swapped, size = 0;
    struct fat_header *fhp = (struct fat_header *)*binary;
    struct fat_arch   *fap =
    (struct fat_arch *)((unsigned long)*binary + sizeof(struct fat_header));
    
    if (fhp->magic == FAT_MAGIC) {
        nfat = fhp->nfat_arch;
        swapped = 0;
    } else if (fhp->magic == FAT_CIGAM) {
        nfat = OSSwapInt32(fhp->nfat_arch);
        swapped = 1;
    } else {
        return -1;
    }
    
    for (; nfat > 0; nfat--, fap++) {
        if (swapped) {
            fap->cputype = OSSwapInt32(fap->cputype);
            fap->offset = OSSwapInt32(fap->offset);
            fap->size = OSSwapInt32(fap->size);
        }
        
        if (fap->cputype == CPU_TYPE_I386) {
            *binary = (void *) ((unsigned long)*binary + fap->offset);
            size = fap->size;
            break;
        }
    }
    
    if (length != 0) *length = size;
    
    return 0;
}

long DecodeMachO(void *binary, entry_t *rentry, char **raddr, int *rsize)
{
    struct mach_header *mH;
    unsigned long  ncmds, cmdBase, cmd, cmdsize;
    //  long   headerBase, headerAddr, headerSize;
    unsigned int vmaddr = ~0;
    unsigned int vmend = 0;
    unsigned long  cnt;
    long  ret = -1;
    unsigned int entry = 0;
    
    gBinaryAddress = (unsigned long)binary;
    
    //  headerBase = gBinaryAddress;
    cmdBase = (unsigned long)gBinaryAddress + sizeof(struct mach_header);
    
    mH = (struct mach_header *)(gBinaryAddress);
    if (mH->magic != MH_MAGIC && mH->magic != MH_MAGIC_64) {
        error("Mach-O file has bad magic number\n");
        return -1;
    }
    
    if (mH->magic == MH_MAGIC_64) {
        // 64-bit binaries have a slightly larger mach_header than 32-bit binaries.
        cmdBase = (unsigned long)gBinaryAddress + sizeof(struct mach_header_64);
    }
    
#if DEBUG
    printf("magic:      %x\n", (unsigned)mH->magic);
    printf("cputype:    %x\n", (unsigned)mH->cputype);
    printf("cpusubtype: %x\n", (unsigned)mH->cpusubtype);
    printf("filetype:   %x\n", (unsigned)mH->filetype);
    printf("ncmds:      %x\n", (unsigned)mH->ncmds);
    printf("sizeofcmds: %x\n", (unsigned)mH->sizeofcmds);
    printf("flags:      %x\n", (unsigned)mH->flags);
    getc();
#endif
    
    ncmds = mH->ncmds;
    
    for (cnt = 0; cnt < ncmds; cnt++) {
        cmd = ((long *)cmdBase)[0];
        cmdsize = ((long *)cmdBase)[1];
        unsigned int load_addr;
        unsigned int load_size;
        
        switch (cmd) {
                
            case LC_SEGMENT:
                ret = DecodeSegment(cmdBase, &load_addr, &load_size);
                if (ret == 0 && load_size != 0 && load_addr >= KERNEL_ADDR) {
                    vmaddr = min(vmaddr, load_addr);
                    vmend = max(vmend, load_addr + load_size);
                }
                break;
                
            case LC_SEGMENT_64:
                ret = DecodeSegment64(cmdBase, &load_addr, &load_size);
                if (ret == 0 && load_size != 0 && load_addr >= KERNEL_ADDR) {
                    vmaddr = min(vmaddr, load_addr);
                    vmend = max(vmend, load_addr + load_size);
                }
                break;
                
            case LC_UNIXTHREAD:
                ret = DecodeUnixThread(cmdBase, &entry, mH->magic == MH_MAGIC_64);
                break;
                
            default:
#if 0
                verbose("Ignoring cmd type %d.\n", (unsigned)cmd);
#endif
                break;
        }
        
        if (ret != 0) {
#if DEBUG
            printf("DecodeMachO() returning -1 (ret = %d)", ret);
#endif
            return -1;
        }
        
        cmdBase += cmdsize;
    }
    
    *rentry = (entry_t)( (unsigned long) entry & 0x3fffffff );
    *rsize = vmend - vmaddr;
    *raddr = (char *)vmaddr;
    
#if DEBUG
    printf("DecodeMachO() returning %ld\n", ret);
#endif
    return ret;
}

// Private Functions

static long DecodeSegment(long cmdBase, unsigned int *load_addr, unsigned int *load_size)
{
    struct segment_command *segCmd;
    unsigned long vmaddr, fileaddr;
    long   vmsize, filesize;
    
    segCmd = (struct segment_command *)cmdBase;
    
    vmaddr = (segCmd->vmaddr & 0x3fffffff);
    vmsize = segCmd->vmsize;
    
    fileaddr = (gBinaryAddress + segCmd->fileoff);
    filesize = segCmd->filesize;
    
    if (filesize == 0) {
        *load_addr = ~0;
        *load_size = 0;
        return 0;
    }
    
#if DEBUG
    printf("segname: %s, vmaddr: %x, vmsize: %x, fileoff: %x, filesize: %x, nsects: %d, flags: %x.\n",
           segCmd->segname, (unsigned)vmaddr, (unsigned)vmsize, (unsigned)fileaddr, (unsigned)filesize,
           (unsigned) segCmd->nsects, (unsigned)segCmd->flags);
    getc();
#endif
    
    if (! ((vmaddr >= KERNEL_ADDR &&
            (vmaddr + vmsize) <= (KERNEL_ADDR + KERNEL_LEN)) ||
           (vmaddr >= HIB_ADDR &&
            (vmaddr + vmsize) <= (HIB_ADDR + HIB_LEN)))) {
               stop("Kernel overflows available space");
           }
    
    if (vmsize && (strcmp(segCmd->segname, "__PRELINK") == 0)) {
        gHaveKernelCache = 1;
    }
    
    // Copy from file load area.
    bcopy((char *)fileaddr, (char *)vmaddr, filesize);
    
    // Zero space at the end of the segment.
    bzero((char *)(vmaddr + filesize), vmsize - filesize);
    
    *load_addr = vmaddr;
    *load_size = vmsize;
    
    return 0;
}

static long DecodeSegment64(long cmdBase, unsigned int *load_addr, unsigned int *load_size)
{
    struct segment_command_64 *segCmd;
    unsigned long vmaddr, fileaddr;
    long   vmsize, filesize;
    
    segCmd = (struct segment_command_64 *)cmdBase;
    
    vmaddr = (segCmd->vmaddr & 0x3fffffff);
    vmsize = segCmd->vmsize;
    
    fileaddr = (gBinaryAddress + segCmd->fileoff);
    filesize = segCmd->filesize;
    
    if (filesize == 0) {
        *load_addr = ~0;
        *load_size = 0;
        return 0;
    }
    
#if DEBUG
    printf("segname: %s, vmaddr: %x, vmsize: %x, fileoff: %x, filesize: %x, nsects: %d, flags: %x.\n",
           segCmd->segname, (unsigned)vmaddr, (unsigned)vmsize, (unsigned)fileaddr, (unsigned)filesize,
           (unsigned) segCmd->nsects, (unsigned)segCmd->flags);
    getc();
#endif
    
    if (! ((vmaddr >= KERNEL_ADDR &&
            (vmaddr + vmsize) <= (KERNEL_ADDR + KERNEL_LEN)) ||
           (vmaddr >= HIB_ADDR &&
            (vmaddr + vmsize) <= (HIB_ADDR + HIB_LEN)))) {
               stop("Kernel overflows available space");
           }
    
    if (vmsize && (strcmp(segCmd->segname, "__PRELINK") == 0)) {
        gHaveKernelCache = 1;
    }
    
    // Don't copy segments that have a zero vmsize. (It is my
    // understanding that these segments aren't intended to be
    // copied into memory in the first place.)
    if (vmsize != 0) {
        // Copy from file load area.
        bcopy((char *)fileaddr, (char *)vmaddr, filesize);
        
        // Zero space at the end of the segment.
        bzero((char *)(vmaddr + filesize), vmsize - filesize);
    }
    
    *load_addr = vmaddr;
    *load_size = vmsize;
    
#if DEBUG
    printf("Done with segname %s\n", segCmd->segname);
#endif
    return 0;
}


static long DecodeUnixThread(long cmdBase, unsigned int *entry, int use_64bit_load_command)
{
    if (use_64bit_load_command) {
        x86_thread_state64_t *threadState = (x86_thread_state64_t *)(cmdBase + sizeof(struct thread_command) + 8);
        *entry = (threadState->rip & 0x3fffffff);
    } else {
        i386_thread_state_t *i386ThreadState;
        
        i386ThreadState = (i386_thread_state_t *)
        (cmdBase + sizeof(struct thread_command) + 8);
        
        *entry = (i386ThreadState->eip & 0x3fffffff);
    }
    
#if DEBUG
    printf("Kernel entry point: %x\n", *entry);
    getc();
#endif
    
    return 0;
}

