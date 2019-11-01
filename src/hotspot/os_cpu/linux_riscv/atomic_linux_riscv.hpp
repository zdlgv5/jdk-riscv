/*
 * Copyright (c) 1997, 2019, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2019 SAP SE. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef OS_CPU_LINUX_RISCV_ATOMIC_LINUX_RISCV_HPP
#define OS_CPU_LINUX_RISCV_ATOMIC_LINUX_RISCV_HPP

//#ifndef RISCV64
//#error "Atomic currently only implemented for RISCV64"
//#endif

#include "utilities/debug.hpp"

// Implementation of class atomic

//
// machine barrier instructions:
//
// - sync            two-way memory barrier, aka fence
// - lwsync          orders  Store|Store,
//                            Load|Store,
//                            Load|Load,
//                   but not Store|Load
// - eieio           orders memory accesses for device memory (only)
// - isync           invalidates speculatively executed instructions
//                   From the POWER ISA 2.06 documentation:
//                    "[...] an isync instruction prevents the execution of
//                   instructions following the isync until instructions
//                   preceding the isync have completed, [...]"
//                   From IBM's AIX assembler reference:
//                    "The isync [...] instructions causes the processor to
//                   refetch any instructions that might have been fetched
//                   prior to the isync instruction. The instruction isync
//                   causes the processor to wait for all previous instructions
//                   to complete. Then any instructions already fetched are
//                   discarded and instruction processing continues in the
//                   environment established by the previous instructions."
//
// semantic barrier instructions:
// (as defined in orderAccess.hpp)
//
// - release         orders Store|Store,       (maps to lwsync)
//                           Load|Store
// - acquire         orders  Load|Store,       (maps to lwsync)
//                           Load|Load
// - fence           orders Store|Store,       (maps to sync)
//                           Load|Store,
//                           Load|Load,
//                          Store|Load
//

inline void pre_membar(atomic_memory_order order) {
  switch (order) {
    case memory_order_relaxed:
    case memory_order_acquire: break;
    case memory_order_release:
    case memory_order_acq_rel:
    default /*conservative*/ : __asm__ __volatile__ ("fence"   : : : "memory"); break;
  }
}

inline void post_membar(atomic_memory_order order) {
  switch (order) {
    case memory_order_relaxed:
    case memory_order_release: break;
    case memory_order_acquire:
    case memory_order_acq_rel:
    default /*conservative*/ : __asm__ __volatile__ ("fence"  : : : "memory"); break;
  }
}


template<size_t byte_size>
struct Atomic::PlatformAdd
  : Atomic::AddAndFetch<Atomic::PlatformAdd<byte_size> >
{
  template<typename I, typename D>
  D add_and_fetch(I add_value, D volatile* dest, atomic_memory_order order) const;
};

template<>
template<typename I, typename D>
inline D Atomic::PlatformAdd<4>::add_and_fetch(I add_value, D volatile* dest,
                                               atomic_memory_order order) const {
  fprintf(stderr, "Atomic::PlatformAdd<4>::add_and_fetch has been reached. Go to %s and check if it works\n", __FILE__);
  exit(1);
  STATIC_ASSERT(4 == sizeof(I));
  STATIC_ASSERT(4 == sizeof(D));

  D result;
  int is_successful;

  pre_membar(order);
  __asm__ __volatile__ (
    "1: lr.w   %0, (%2)      \n"
    "   add    %0, %0, %1    \n"
    "   sc.w   %3, %0, (%2)  \n"
    "   bnez   %3, 1b        \n"
    : /*%0*/"=&r" (result)
    : /*%1*/"r" (add_value), /*%2*/"r" (dest), /*%3*/"r" (is_successful)
    : "cc", "memory" );
  post_membar(order);

  return result;
}


template<>
template<typename I, typename D>
inline D Atomic::PlatformAdd<8>::add_and_fetch(I add_value, D volatile* dest,
                                               atomic_memory_order order) const {
  fprintf(stderr, "Atomic::PlatformAdd<8>::add_and_fetch has been reached. Go to %s and check if it works\n", __FILE__);
  exit(1);
  STATIC_ASSERT(8 == sizeof(I));
  STATIC_ASSERT(8 == sizeof(D));

  D result;
  int is_successful;

  pre_membar(order);
  __asm__ __volatile__ (
    "1: lr.d   %0, (%2)      \n"
    "   add    %0, %0, %1    \n"
    "   sc.d   %3, %0, (%2)  \n"
    "   bnez   %3, 1b        \n"
    : /*%0*/"=&r" (result)
    : /*%1*/"r" (add_value), /*%2*/"r" (dest), /*%3*/"r" (is_successful)
    : "cc", "memory" );
  post_membar(order);

  return result;
}

template<>
template<typename T>
inline T Atomic::PlatformXchg<4>::operator()(T exchange_value,
                                             T volatile* dest,
                                             atomic_memory_order order) const {
  // Note that xchg doesn't necessarily do an acquire
  // (see synchronizer.cpp).

  T old_value;
  int is_successful;

  pre_membar(order);
  __asm__ __volatile__ (
    /* atomic loop */
    "1:                                                       \n"
    "   lr.w   %[old_value], (%[dest])                        \n"
    "   sc.w   %[is_successful], %[exchange_value], (%[dest]) \n"
    "   bnez   %[is_successful], 1b                           \n"
    /* exit */
    "2:                                                       \n"
    /* out */
    : [old_value]       "=&r"   (old_value)
    /* in */
    : [dest]            "r"     (dest),
      [exchange_value]  "r"     (exchange_value),
      [is_successful]   "r"     (is_successful)
    /* clobber */
    : "cc",
      "memory"
    );
  post_membar(order);

  return old_value;
}

template<>
template<typename T>
inline T Atomic::PlatformXchg<8>::operator()(T exchange_value,
                                             T volatile* dest,
                                             atomic_memory_order order) const {
  fprintf(stderr, "Atomic::PlatformXchg<8> has been reached. Go to %s and check if it works\n", __FILE__);
  exit(1);
  STATIC_ASSERT(8 == sizeof(T));
  // Note that xchg doesn't necessarily do an acquire
  // (see synchronizer.cpp).

  T old_value;
  int is_successful;

  pre_membar(order);
  __asm__ __volatile__ (
    /* atomic loop */
    "1:                                                       \n"
    "   lr.d   %[old_value], (%[dest])                        \n"
    "   sc.d   %[is_successful], %[exchange_value], (%[dest]) \n"
    "   bnez   %[is_successful], 1b                           \n"
    /* exit */
    "2:                                                       \n"
    /* out */
    : [old_value]       "=&r"   (old_value)
    /* in */
    : [dest]            "r"     (dest),
      [exchange_value]  "r"     (exchange_value),
      [is_successful]   "r"     (is_successful)
    /* clobber */
    : "cc",
      "memory"
    );
  post_membar(order);

  return old_value;
}

template<>
template<typename T>
inline T Atomic::PlatformCmpxchg<1>::operator()(T exchange_value,
                                                T volatile* dest,
                                                T compare_value,
                                                atomic_memory_order order) const {
  fprintf(stderr, "Atomic::PlatformCmpxchg<1> has been reached. Go to %s and check if it works\n", __FILE__);
  exit(1);
  STATIC_ASSERT(1 == sizeof(T));

  // Note that cmpxchg guarantees a two-way memory barrier across
  // the cmpxchg, so it's really a a 'fence_cmpxchg_fence' if not
  // specified otherwise (see atomic.hpp).

  // Using 32 bit internally.
  volatile int *dest_base = (volatile int*)((uintptr_t)dest & ~3);

#ifdef VM_LITTLE_ENDIAN
  const unsigned int shift_amount        = ((uintptr_t)dest & 3) * 8;
#else
  const unsigned int shift_amount        = ((~(uintptr_t)dest) & 3) * 8;
#endif
  const unsigned int masked_compare_val  = ((unsigned int)(unsigned char)compare_value),
                     masked_exchange_val = ((unsigned int)(unsigned char)exchange_value),
                     xor_value           = (masked_compare_val ^ masked_exchange_val) << shift_amount;

  unsigned int old_value, value32;
  int is_successful;

  pre_membar(order);
  __asm__ __volatile__ (
    /* simple guard */
    "   lb     %[old_value], (%[dest])                       \n"
    "   bne    %[masked_compare_val], %[old_value], 2f       \n"
    /* atomic loop */
    "1:                                                      \n"
    "   lr.w    %[value32], (%[dest_base])                   \n"
    /* extract byte and compare */
    "   srl     %[old_value], %[value32], %[shift_amount]    \n"
    "   slli    %[old_value], %[old_value], 56               \n"
    "   srli    %[old_value], %[old_value], 56               \n"
    "   bne     %[masked_compare_val], %[old_value], 2f      \n"
    /* replace byte and try to store */
    "   xor     %[value32], %[xor_value], %[value32]         \n"
    "   sc.w    %[is_successful], %[value32], (%[dest_base]) \n"
    "   bnez    %[is_successful], 1b                         \n"
    /* exit */
    "2:                                                      \n"
    /* out */
    : [old_value]           "=&r"   (old_value),
      [value32]             "=&r"   (value32)
    /* in */
    : [dest]                "r"     (dest),
      [dest_base]           "r"     (dest_base),
      [shift_amount]        "r"     (shift_amount),
      [masked_compare_val]  "r"     (masked_compare_val),
      [xor_value]           "r"     (xor_value),
      [is_successful]       "r"     (is_successful)
    /* clobber */
    : "cc",
      "memory"
    );
  post_membar(order);

  return PrimitiveConversions::cast<T>((unsigned char)old_value);
}

template<>
template<typename T>
inline T Atomic::PlatformCmpxchg<4>::operator()(T exchange_value,
                                                T volatile* dest,
                                                T compare_value,
                                                atomic_memory_order order) const {
  STATIC_ASSERT(4 == sizeof(T));

  // Note that cmpxchg guarantees a two-way memory barrier across
  // the cmpxchg, so it's really a a 'fence_cmpxchg_fence' if not
  // specified otherwise (see atomic.hpp).

  T old_value;
  int is_successful;

  pre_membar(order);
  __asm__ __volatile__ (
    /* simple guard */
    "   lw     %[old_value], (%[dest])                          \n"
    "   bne    %[compare_value], %[old_value], 2f               \n"
    /* atomic loop */
    "1:                                                         \n"
    "   lr.w    %[old_value], (%[dest])                         \n"
    "   bne     %[compare_value], %[old_value], 2f              \n"
    "   sc.w    %[is_successful], %[exchange_value], (%[dest] ) \n"
    "   bnez    %[is_successful], 1b                            \n"
    /* exit */
    "2:                                                         \n"
    /* out */
    : [old_value]       "=&r"   (old_value)
    /* in */
    : [dest]            "r"     (dest),
      [compare_value]   "r"     (compare_value),
      [exchange_value]  "r"     (exchange_value),
      [is_successful]   "r"     (is_successful)
    /* clobber */
    : "cc",
      "memory"
    );
  post_membar(order);

  return old_value;
}

template<>
template<typename T>
inline T Atomic::PlatformCmpxchg<8>::operator()(T exchange_value,
                                                T volatile* dest,
                                                T compare_value,
                                                atomic_memory_order order) const {
  STATIC_ASSERT(8 == sizeof(T));

  // Note that cmpxchg guarantees a two-way memory barrier across
  // the cmpxchg, so it's really a a 'fence_cmpxchg_fence' if not
  // specified otherwise (see atomic.hpp).

  T old_value;
  int is_successful;

  pre_membar(order);
  __asm__ __volatile__ (
    /* simple guard */
    "   ld     %[old_value], (%[dest])                         \n"
    "   bne    %[compare_value], %[old_value], 2f              \n"
    /* atomic loop */
    "1:                                                        \n"
    "   lr.d    %[old_value], (%[dest])                        \n"
    "   bne     %[compare_value], %[old_value], 2f             \n"
    "   sc.d    %[is_successful], %[exchange_value], (%[dest]) \n"
    "   bnez    %[is_successful], 1b                           \n"
    /* exit */
    "2:                                                        \n"
    /* out */
    : [old_value]       "=&r"   (old_value)
    /* in */
    : [dest]            "r"     (dest),
      [compare_value]   "r"     (compare_value),
      [exchange_value]  "r"     (exchange_value),
      [is_successful]   "r"     (is_successful)
    /* clobber */
    : "cc",
      "memory"
    );
  post_membar(order);

  return old_value;
}

#endif // OS_CPU_LINUX_RISCV_ATOMIC_LINUX_RISCV_HPP
