/*
 * Portions Copyright (c) 1999-2003 Apple Computer, Inc. All Rights
 * Reserved.
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * This file was modified by William Kent in 2016 to support the Andromeda
 * project. This notice is included in support of clause 2.2(b) of the License.
 */

// This file was adapted from the contents of:
// http://www.opensource.apple.com/source/Libc/Libc-1082.20.4/stdlib/FreeBSD/random.c

#include <libsaio.h>

static uint32_t randtbl[32] = {
    3,
    
    0x991539b1, 0x16a5bce3, 0x6774a4cd, 0x3e01511e, 0x4e508aaa, 0x61048c05,
    0xf5500617, 0x846b7115, 0x6a19892c, 0x896a97af, 0xdb48f936, 0x14898454,
    0x37ffd106, 0xb58bff9c, 0x59e17104, 0xcf918a49, 0x09378c83, 0x52c7a471,
    0x8d293ea9, 0x1f4fc301, 0xc3db71be, 0x39b44e1c, 0xf8a44ef9, 0x4c8b80b1,
    0x19edc328, 0x87bf4bdd, 0xc9b240e5, 0xe9ee4b1b, 0x4382aee7, 0x535b6b41,
    0xf3bec5da
};

static uint32_t *state = &randtbl[1];
static uint32_t *fptr = &randtbl[4];
static uint32_t *rptr = &randtbl[1];
static uint32_t *end_ptr = &randtbl[32];

static uint32_t good_rand(int32_t x) {
    int32_t hi, lo;
    if (x == 0) x= 123459876;
    hi = x / 127773;
    lo = x % 127773;
    x = 16807 * lo - 2836 * hi;
    if (x < 0) x += 0x7FFFFFFF;
    return x;
}

uint32_t random(void) {
    uint32_t i;
    uint32_t *f, *r;
    
    f = fptr; r = rptr;
    *f += *r;
    i = (*f >> 1) & 0x7FFFFFFF;
    if (++f >= end_ptr) {
        f = state;
        ++r;
    } else if (++r >= end_ptr) {
        r = state;
    }
    
    fptr = f; rptr = r;
    return i;
}

void srandom(uint32_t seed) {
    for (int i = 1; i < 31; i++) state[i] = good_rand(state[i - 1]);
    fptr = &state[3];
    rptr = &state[0];
    for (int i = 0; i < 310; i++) (void)random();
}
