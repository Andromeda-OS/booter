/*
 * Copyright (c) 1999-2003 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * Portions Copyright (c) 1999-2003 Apple Computer, Inc.  All Rights
 * Reserved.  This file contains Original Code and/or Modifications of
 * Original Code as defined in and that are subject to the Apple Public
 * Source License Version 2.0 (the "License").  You may not use this file
 * except in compliance with the License.  Please obtain a copy of the
 * License at http://www.apple.com/publicsource and read it before using
 * this file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON- INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */
/*
 * Copyright 1993 NeXT, Inc.
 * All rights reserved.
 */

#include "libsaio.h"
#include "bootstruct.h"
#include "random.h"

#define VGA_TEXT_MODE 0

/*==========================================================================
 * Initialize the structure of parameters passed to
 * the kernel by the booter.
 */

boot_args         *bootArgs;
PrivateBootInfo_t *bootInfo;
Node              *gMemoryMapNode;

static char platformName[64];
static uint64_t FSBFrequency = 266 * (1000 * 1000); // A semi-arbitrary number of megahertz.
static uint32_t EarlyEntropyBuffer[64 / sizeof(uint32_t)];
#define _countof(array) (sizeof(array)/sizeof(array[0]))

void initKernBootStruct( int biosdev )
{
	Node *node;
	int nameLen;
	static int init_done = 0;

	if ( !init_done )
	{
		bootArgs = (boot_args *)malloc(sizeof(boot_args));
		bootInfo = (PrivateBootInfo_t *)malloc(sizeof(PrivateBootInfo_t));
		if (bootArgs == 0 || bootInfo == 0)
			stop("Couldn't allocate boot info\n");

		bzero(bootArgs, sizeof(boot_args));
		bzero(bootInfo, sizeof(PrivateBootInfo_t));

		// Get system memory map. Also update the size of the
		// conventional/extended memory for backwards compatibility.

		bootInfo->memoryMapCount =
		getMemoryMap( bootInfo->memoryMap, kMemoryMapCountMax,
					 (unsigned long *) &bootInfo->convmem,
					 (unsigned long *) &bootInfo->extmem );

		if ( bootInfo->memoryMapCount == 0 )
		{
			// BIOS did not provide a memory map, systems with
			// discontiguous memory or unusual memory hole locations
			// may have problems.

			bootInfo->convmem = getConventionalMemorySize();
			bootInfo->extmem  = getExtendedMemorySize();
		}

		bootInfo->configEnd    = bootInfo->config;
		bootArgs->Video.v_display = VGA_TEXT_MODE;

		DT__Initialize();

		node = DT__FindNode("/", true);
		if (node == 0) {
			stop("Couldn't create root node");
		}
		getPlatformName(platformName);
		nameLen = strlen(platformName) + 1;
		DT__AddProperty(node, "compatible", nameLen, platformName);
		DT__AddProperty(node, "model", nameLen, platformName);

		Node *efi_node = DT__FindNode("/efi/platform", true);
		if (efi_node == 0) {
			stop("Couldn't create \"/efi/platform\" node, mach_kernel will not boot correctly");
		}

		Node *chosen_node = DT__FindNode("/chosen", true);
		if (chosen_node == 0) {
			stop("Couldn't create \"/chosen\" node, mach_kernel will not boot correctly");
		}

		uint32_t cpuid_eax = 1, cpuid_ecx = 0;
		__asm__ volatile("cpuid" : "=c" (cpuid_ecx) : "a" (cpuid_eax), "c" (cpuid_ecx));

		if ((cpuid_ecx & (1 << 30)) != 0) {
			for (int i = 0; i < _countof(EarlyEntropyBuffer); i++) {
				// This code block was taken adapted from code on this page:
				// http://stackoverflow.com/questions/21541968/is-flags-eflags-part-of-cc-condition-control-for-clobber-list/21552100#21552100
				char cf = 0; uint32_t val = 0;
				do {
					__asm__ volatile("rdrand %0; setc %1" : "=r" (val), "=qm" (cf) :: "cc");
				} while (cf == 0);
				// End code block from Stack Overflow.

				EarlyEntropyBuffer[i] = val;
			}
		} else {
			uint32_t tsc_hi, tsc_lo;
			__asm__ volatile("rdtsc" : "=d" (tsc_hi), "=a" (tsc_lo));
			srandom(tsc_hi ^ tsc_lo);

			for (int i = 0; i < _countof(EarlyEntropyBuffer); i++) {
				EarlyEntropyBuffer[i] = random();
			}
		}

		DT__AddProperty(chosen_node, "random-seed", sizeof(EarlyEntropyBuffer), EarlyEntropyBuffer);
		DT__AddProperty(efi_node, "FSBFrequency", sizeof(FSBFrequency), &FSBFrequency);

		gMemoryMapNode = DT__FindNode("/chosen/memory-map", true);

		bootArgs->Version  = kBootArgsVersion;
		bootArgs->Revision = kBootArgsRevision;

		init_done = 1;
	}

	// Update kernDev from biosdev.

	bootInfo->kernDev = biosdev;
}


/* Copy boot args after kernel and record address. */

void
reserveKernBootStruct(void)
{
	void *oldAddr = bootArgs;
	bootArgs = (boot_args *)AllocateKernelMemory(sizeof(boot_args));
	bcopy(oldAddr, bootArgs, sizeof(boot_args));
}

void
finalizeBootStruct(void)
{
	uint32_t size;
	void *addr;
	int i;
	EfiMemoryRange *memoryMap;
	MemoryRange *range;
	int memoryMapCount = bootInfo->memoryMapCount;

	if (memoryMapCount == 0) {
		// XXX could make a two-part map here
		stop("Unable to convert memory map into proper format\n");
	}

	// convert memory map to boot_args memory map
	memoryMap = (EfiMemoryRange *)AllocateKernelMemory(sizeof(EfiMemoryRange) * memoryMapCount);
	bootArgs->MemoryMap = memoryMap;
	bootArgs->MemoryMapSize = sizeof(EfiMemoryRange) * memoryMapCount;
	bootArgs->MemoryMapDescriptorSize = sizeof(EfiMemoryRange);
	bootArgs->MemoryMapDescriptorVersion = 0;

	for (i=0; i<memoryMapCount; i++, memoryMap++) {
		range = &bootInfo->memoryMap[i];
		switch(range->type) {
			case kMemoryRangeACPI:
				memoryMap->Type = kEfiACPIReclaimMemory;
				break;
			case kMemoryRangeNVS:
				memoryMap->Type = kEfiACPIMemoryNVS;
				break;
			case kMemoryRangeUsable:
				memoryMap->Type = kEfiConventionalMemory;
				break;
			case kMemoryRangeReserved:
			default:
				memoryMap->Type = kEfiReservedMemoryType;
				break;
		}
		memoryMap->PhysicalStart = range->base;
		memoryMap->VirtualStart = range->base;
		memoryMap->NumberOfPages = range->length >> I386_PGSHIFT;
		memoryMap->Attribute = 0;
	}

	// copy bootFile into device tree
	// XXX

	// add PCI info somehow into device tree
	// XXX

	// Flatten device tree
	DT__FlattenDeviceTree(0, &size);
	addr = (void *)AllocateKernelMemory(size);
	if (addr == 0) {
		stop("Couldn't allocate device tree\n");
	}

	DT__FlattenDeviceTree((void **)&addr, &size);
	bootArgs->deviceTreeP = (void *)addr;
	bootArgs->deviceTreeLength = size;
}
