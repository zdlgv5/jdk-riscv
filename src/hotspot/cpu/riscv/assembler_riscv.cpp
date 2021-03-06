/*
 * Copyright (c) 1997, 2018, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2015 SAP SE. All rights reserved.
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

#include "precompiled.hpp"
#include "asm/assembler.inline.hpp"
#include "gc/shared/cardTableBarrierSet.hpp"
#include "gc/shared/collectedHeap.inline.hpp"
#include "interpreter/interpreter.hpp"
#include "memory/resourceArea.hpp"
#include "prims/methodHandles.hpp"
#include "runtime/biasedLocking.hpp"
#include "runtime/interfaceSupport.inline.hpp"
#include "runtime/objectMonitor.hpp"
#include "runtime/os.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubRoutines.hpp"
#include "utilities/macros.hpp"

#ifdef PRODUCT
#define BLOCK_COMMENT(str) // nothing
#else
#define BLOCK_COMMENT(str) block_comment(str)
#endif

int AbstractAssembler::code_fill_byte() {
  return 0x00;                  // illegal instruction 0x00000000
}


// Patch instruction `inst' at offset `inst_pos' to refer to
// `dest_pos' and return the resulting instruction.  We should have
// pcs, not offsets, but since all is relative, it will work out fine.
int Assembler::patched_branch(int dest_pos, int inst, int inst_pos) {
  int m = 0; // mask for displacement field
  int v = 0; // new value for displacement field

  int inv_op = get_opcode(inst);
  switch (inv_op) {
    case BRANCH_RV_OPCODE: m = immb(-2); v = immb(disp(dest_pos, inst_pos)); break;
    case   JALR_RV_OPCODE: m = immi(-1); v = immi(disp(dest_pos, inst_pos)); break;
    case    JAL_RV_OPCODE: m = immj(-2); v = immj(disp(dest_pos, inst_pos)); break;
    default: ShouldNotReachHere();
  }
  return inst & ~m | v;
}

// Return the offset, relative to _code_begin, of the destination of
// the branch inst at offset pos.
int Assembler::branch_destination(int inst, int pos) {
  int r = 0;
  switch (get_opcode(inst)) {
    case b_op:  r = bxx_destination_offset(inst, pos); break;
    case bc_op: r = inv_bd_field(inst, pos); break;
    default: ShouldNotReachHere();
  }
  return r;
}

// Low-level andi-one-instruction-macro.
void Assembler::andi_PPC(Register a, Register s, const long ui16) {
  if (is_power_of_2_long(((jlong) ui16)+1)) {
    // pow2minus1
    clrldi_PPC(a, s, 64-log2_long((((jlong) ui16)+1)));
  } else if (is_power_of_2_long((jlong) ui16)) {
    // pow2
    rlwinm_PPC(a, s, 0, 31-log2_long((jlong) ui16), 31-log2_long((jlong) ui16));
  } else if (is_power_of_2_long((jlong)-ui16)) {
    // negpow2
    clrrdi_PPC(a, s, log2_long((jlong)-ui16));
  } else {
    assert(is_uimm(ui16, 16), "must be 16-bit unsigned immediate");
    andi__PPC(a, s, ui16);
  }
}

#define LOAD_REGISTER_OR_CONSTANT(_command) \
  if (roc.is_constant()) { \
    if (s1 == noreg) { \
      int rest12 = load_const_optimized(d, roc.as_constant(), noreg, true); \
      _command(d, d, rest12); \
    } else if (is_simm(roc.as_constant(), 12)) { \
      _command(d, s1, roc.as_constant()); \
    } else { \
      int rest12 = add_const_optimized(d, s1, roc.as_constant(), noreg, true); \
      _command(d, d, rest12); \
    } \
  } else { \
    if (s1 == noreg) \
      _command(d, roc.as_register(), 0); \
    else { \
      add(d, roc.as_register(), s1); \
      _command(d, d, 0); \
    } \
  }

// RegisterOrConstant version.
void Assembler::ld(Register d, Register s1, RegisterOrConstant roc) {
  LOAD_REGISTER_OR_CONSTANT(ld)
}

void Assembler::lw(Register d, Register s1, RegisterOrConstant roc) {
  LOAD_REGISTER_OR_CONSTANT(lw)
}

void Assembler::lwu(Register d, Register s1, RegisterOrConstant roc) {
  LOAD_REGISTER_OR_CONSTANT(lwu)
}

void Assembler::lh(Register d, Register s1, RegisterOrConstant roc) {
  LOAD_REGISTER_OR_CONSTANT(lh)
}

void Assembler::lhu(Register d, Register s1, RegisterOrConstant roc) {
  LOAD_REGISTER_OR_CONSTANT(lhu)
}

void Assembler::lb(Register d, Register s1, RegisterOrConstant roc) {
  LOAD_REGISTER_OR_CONSTANT(lb)
}

void Assembler::lbu(Register d, Register s1, RegisterOrConstant roc) {
  LOAD_REGISTER_OR_CONSTANT(lbu)
}

#define STORE_REGISTER_OR_CONSTANT(_command) \
  if (roc.is_constant()) { \
    if (s1 == noreg) { \
      guarantee(tmp != noreg, "Need tmp reg to encode large constants"); \
      int rest12 = load_const_optimized(tmp, roc.as_constant(), noreg, true); \
      _command(d, tmp, rest12); \
    } else if (is_simm(roc.as_constant(), 12)) { \
      _command(d, s1, roc.as_constant()); \
    } else { \
      guarantee(tmp != noreg, "Need tmp reg to encode large constants"); \
      int rest12 = add_const_optimized(tmp, s1, roc.as_constant(), noreg, true); \
      _command(d, tmp, rest12); \
    } \
  } else { \
    if (s1 == noreg) \
      _command(d, roc.as_register(), 0); \
    else { \
      add(tmp, roc.as_register(), s1); \
      _command(d, tmp, 0); \
    } \
  }

void Assembler::sd(Register d, Register s1, RegisterOrConstant roc, Register tmp) {
  STORE_REGISTER_OR_CONSTANT(sd)
}

void Assembler::sw(Register d, Register s1, RegisterOrConstant roc, Register tmp) {
  STORE_REGISTER_OR_CONSTANT(sw)
}

void Assembler::sh(Register d, Register s1, RegisterOrConstant roc, Register tmp) {
  STORE_REGISTER_OR_CONSTANT(sh)
}

void Assembler::sb(Register d, Register s1, RegisterOrConstant roc, Register tmp) {
  STORE_REGISTER_OR_CONSTANT(sb)
}

// RegisterOrConstant version.
void Assembler::ld_PPC(Register d, RegisterOrConstant roc, Register s1) {
  if (roc.is_constant()) {
    if (s1 == noreg) {
      int simm16_rest = load_const_optimized(d, roc.as_constant(), noreg, true);
      Assembler::ld_PPC(d, simm16_rest, d);
    } else if (is_simm(roc.as_constant(), 16)) {
      Assembler::ld_PPC(d, roc.as_constant(), s1);
    } else {
      load_const_optimized(d, roc.as_constant());
      Assembler::ldx_PPC(d, d, s1);
    }
  } else {
    if (s1 == noreg)
      Assembler::ld_PPC(d, 0, roc.as_register());
    else
      Assembler::ldx_PPC(d, roc.as_register(), s1);
  }
}

void Assembler::lwa_PPC(Register d, RegisterOrConstant roc, Register s1) {
  if (roc.is_constant()) {
    if (s1 == noreg) {
      int simm16_rest = load_const_optimized(d, roc.as_constant(), noreg, true);
      Assembler::lwa_PPC(d, simm16_rest, d);
    } else if (is_simm(roc.as_constant(), 16)) {
      Assembler::lwa_PPC(d, roc.as_constant(), s1);
    } else {
      load_const_optimized(d, roc.as_constant());
      Assembler::lwax_PPC(d, d, s1);
    }
  } else {
    if (s1 == noreg)
      Assembler::lwa_PPC(d, 0, roc.as_register());
    else
      Assembler::lwax_PPC(d, roc.as_register(), s1);
  }
}

void Assembler::lwz_PPC(Register d, RegisterOrConstant roc, Register s1) {
  if (roc.is_constant()) {
    if (s1 == noreg) {
      int simm16_rest = load_const_optimized(d, roc.as_constant(), noreg, true);
      Assembler::lwz_PPC(d, simm16_rest, d);
    } else if (is_simm(roc.as_constant(), 16)) {
      Assembler::lwz_PPC(d, roc.as_constant(), s1);
    } else {
      load_const_optimized(d, roc.as_constant());
      Assembler::lwzx_PPC(d, d, s1);
    }
  } else {
    if (s1 == noreg)
      Assembler::lwz_PPC(d, 0, roc.as_register());
    else
      Assembler::lwzx_PPC(d, roc.as_register(), s1);
  }
}

void Assembler::lha_PPC(Register d, RegisterOrConstant roc, Register s1) {
  if (roc.is_constant()) {
    if (s1 == noreg) {
      int simm16_rest = load_const_optimized(d, roc.as_constant(), noreg, true);
      Assembler::lha_PPC(d, simm16_rest, d);
    } else if (is_simm(roc.as_constant(), 16)) {
      Assembler::lha_PPC(d, roc.as_constant(), s1);
    } else {
      load_const_optimized(d, roc.as_constant());
      Assembler::lhax_PPC(d, d, s1);
    }
  } else {
    if (s1 == noreg)
      Assembler::lha_PPC(d, 0, roc.as_register());
    else
      Assembler::lhax_PPC(d, roc.as_register(), s1);
  }
}

void Assembler::lhz_PPC(Register d, RegisterOrConstant roc, Register s1) {
  if (roc.is_constant()) {
    if (s1 == noreg) {
      int simm16_rest = load_const_optimized(d, roc.as_constant(), noreg, true);
      Assembler::lhz_PPC(d, simm16_rest, d);
    } else if (is_simm(roc.as_constant(), 16)) {
      Assembler::lhz_PPC(d, roc.as_constant(), s1);
    } else {
      load_const_optimized(d, roc.as_constant());
      Assembler::lhzx_PPC(d, d, s1);
    }
  } else {
    if (s1 == noreg)
      Assembler::lhz_PPC(d, 0, roc.as_register());
    else
      Assembler::lhzx_PPC(d, roc.as_register(), s1);
  }
}

void Assembler::lbz_PPC(Register d, RegisterOrConstant roc, Register s1) {
  if (roc.is_constant()) {
    if (s1 == noreg) {
      int simm16_rest = load_const_optimized(d, roc.as_constant(), noreg, true);
      Assembler::lbz_PPC(d, simm16_rest, d);
    } else if (is_simm(roc.as_constant(), 16)) {
      Assembler::lbz_PPC(d, roc.as_constant(), s1);
    } else {
      load_const_optimized(d, roc.as_constant());
      Assembler::lbzx_PPC(d, d, s1);
    }
  } else {
    if (s1 == noreg)
      Assembler::lbz_PPC(d, 0, roc.as_register());
    else
      Assembler::lbzx_PPC(d, roc.as_register(), s1);
  }
}

void Assembler::std_PPC(Register d, RegisterOrConstant roc, Register s1, Register tmp) {
  if (roc.is_constant()) {
    if (s1 == noreg) {
      guarantee(tmp != noreg, "Need tmp reg to encode large constants");
      int simm16_rest = load_const_optimized(tmp, roc.as_constant(), noreg, true);
      Assembler::std_PPC(d, simm16_rest, tmp);
    } else if (is_simm(roc.as_constant(), 16)) {
      Assembler::std_PPC(d, roc.as_constant(), s1);
    } else {
      guarantee(tmp != noreg, "Need tmp reg to encode large constants");
      load_const_optimized(tmp, roc.as_constant());
      Assembler::stdx_PPC(d, tmp, s1);
    }
  } else {
    if (s1 == noreg)
      Assembler::std_PPC(d, 0, roc.as_register());
    else
      Assembler::stdx_PPC(d, roc.as_register(), s1);
  }
}

void Assembler::stw_PPC(Register d, RegisterOrConstant roc, Register s1, Register tmp) {
  if (roc.is_constant()) {
    if (s1 == noreg) {
      guarantee(tmp != noreg, "Need tmp reg to encode large constants");
      int simm16_rest = load_const_optimized(tmp, roc.as_constant(), noreg, true);
      Assembler::stw_PPC(d, simm16_rest, tmp);
    } else if (is_simm(roc.as_constant(), 16)) {
      Assembler::stw_PPC(d, roc.as_constant(), s1);
    } else {
      guarantee(tmp != noreg, "Need tmp reg to encode large constants");
      load_const_optimized(tmp, roc.as_constant());
      Assembler::stwx_PPC(d, tmp, s1);
    }
  } else {
    if (s1 == noreg)
      Assembler::stw_PPC(d, 0, roc.as_register());
    else
      Assembler::stwx_PPC(d, roc.as_register(), s1);
  }
}

void Assembler::sth_PPC(Register d, RegisterOrConstant roc, Register s1, Register tmp) {
  if (roc.is_constant()) {
    if (s1 == noreg) {
      guarantee(tmp != noreg, "Need tmp reg to encode large constants");
      int simm16_rest = load_const_optimized(tmp, roc.as_constant(), noreg, true);
      Assembler::sth_PPC(d, simm16_rest, tmp);
    } else if (is_simm(roc.as_constant(), 16)) {
      Assembler::sth_PPC(d, roc.as_constant(), s1);
    } else {
      guarantee(tmp != noreg, "Need tmp reg to encode large constants");
      load_const_optimized(tmp, roc.as_constant());
      Assembler::sthx_PPC(d, tmp, s1);
    }
  } else {
    if (s1 == noreg)
      Assembler::sth_PPC(d, 0, roc.as_register());
    else
      Assembler::sthx_PPC(d, roc.as_register(), s1);
  }
}

void Assembler::stb_PPC(Register d, RegisterOrConstant roc, Register s1, Register tmp) {
  if (roc.is_constant()) {
    if (s1 == noreg) {
      guarantee(tmp != noreg, "Need tmp reg to encode large constants");
      int simm16_rest = load_const_optimized(tmp, roc.as_constant(), noreg, true);
      Assembler::stb_PPC(d, simm16_rest, tmp);
    } else if (is_simm(roc.as_constant(), 16)) {
      Assembler::stb_PPC(d, roc.as_constant(), s1);
    } else {
      guarantee(tmp != noreg, "Need tmp reg to encode large constants");
      load_const_optimized(tmp, roc.as_constant());
      Assembler::stbx_PPC(d, tmp, s1);
    }
  } else {
    if (s1 == noreg)
      Assembler::stb_PPC(d, 0, roc.as_register());
    else
      Assembler::stbx_PPC(d, roc.as_register(), s1);
  }
}

void Assembler::add_PPC(Register d, RegisterOrConstant roc, Register s1) {
  if (roc.is_constant()) {
    intptr_t c = roc.as_constant();
    assert(is_simm(c, 16), "too big");
    addi_PPC(d, s1, (int)c);
  }
  else add_PPC(d, roc.as_register(), s1);
}

void Assembler::add(Register d, RegisterOrConstant roc, Register s1) {
  if (roc.is_constant()) {
    intptr_t c = roc.as_constant();
    assert(is_simm(c, 16), "too big");
    addi(d, s1, (int)c);
  }
  else add(d, roc.as_register(), s1);
}

void Assembler::subf_PPC(Register d, RegisterOrConstant roc, Register s1) {
  if (roc.is_constant()) {
    intptr_t c = roc.as_constant();
    assert(is_simm(-c, 16), "too big");
    addi_PPC(d, s1, (int)-c);
  }
  else subf_PPC(d, roc.as_register(), s1);
}

void Assembler::cmpd_PPC(ConditionRegister d, RegisterOrConstant roc, Register s1) {
  if (roc.is_constant()) {
    intptr_t c = roc.as_constant();
    assert(is_simm(c, 16), "too big");
    cmpdi_PPC(d, s1, (int)c);
  }
  else cmpd_PPC(d, roc.as_register(), s1);
}

// Load a 64 bit constant. Patchable.
void Assembler::load_const(Register d, long x, Register tmp) {
  load_const_optimized(d, x, tmp, false);
}

void Assembler::li(Register d, void* addr) {
  li(d, (long)(unsigned long)addr);
}


// load 64-bit immediate value
void Assembler::li(Register d, long imm) {
  // tty->print_cr("li %s, 0x%lx at %p", d->name(), (unsigned long)imm, pc());

  if (-0x800 <= imm && imm < 0x800) {
    addi(d, R0_ZERO, imm);
    return;
  }

  long off = imm - (long)pc();

//  if (INT32_MIN <= off && off <= INT32_MAX) { // TODO RISCV fix it on negative values
//    // load using AUIPC
//    unsigned long uoff = off;
//    unsigned long low = uoff & 0xfff;
//    unsigned long high = (uoff >> 12) & 0xfffff;
//    if (low >= 0x800) {
//      ++high;
//    }
//    auipc(d, high);
//    if (low) {
//      addi(d, d, low);
//    }
//    return;
//  }

  unsigned long uimm = imm;
  unsigned long value = uimm;

  unsigned long sha = 0; // shift amount for final value
  if (imm < INT32_MIN || imm > INT32_MAX) {
    while (!(value & 1)) {
      ++sha;
      value >>= 1;
    }
  }

  unsigned long low = value & 0xfff; // low section is 12 lowest bits
  unsigned long mid = value >> 12; // mid section is 20+ following bits
  if (low >= 0x800) {
    ++mid;
  }

  unsigned long shb = 0; // shift amount for mid section
  if (mid >= 0x100000 && !(mid >> 51)) {
    while (!(mid & 1)) {
      ++shb;
      mid >>= 1;
    }
  }

  unsigned long high = mid >> 20; // high section is remaining bits
  mid &= 0xfffff;
  if (mid >= 0x80000) {
    ++high;
  }
  high &= 0xfffffffful;

  if (!high) {
    // load mid
    if (mid) {
      lui(d, mid);
      if (shb) {
        slli(d, d, shb);
      }
    }

    // load low
    if (low) {
      addi(d, mid ? d : R0_ZERO, low);
      if (sha) {
        slli(d, d, sha);
      }
    }

    return;
  }

  // load negative constant the dumb way

  if (imm < 0) {
    li(d, -imm);
    sub(d, R0_ZERO, d);
    return;
  }

  // when all else fails, load by parts

  sha = 0;
  while (!(uimm & 1)) {
    ++sha;
    uimm >>= 1;
  }
  low = uimm & 0xffful;
  mid = uimm ^ low;
  if (low >= 0x800) {
    mid += 0x1000;
  }
  li(d, mid); // we zero lowest non-zero 12 bits, so recursion is finite
  if (low) {
    addi(d, d, low);
  }
  if (sha) {
    slli(d, d, sha);
  }
}

// Load a 64 bit constant, optimized, not identifyable.
// Tmp can be used to increase ILP. Set return_simm12_rest=true to get a
// 12 bit immediate offset.
int Assembler::load_const_optimized(Register d, long imm, Register tmp, bool return_simm12_rest) {
  // TODO_RISCV: utilize tmp register
  assert_different_registers(d, tmp);

  int retval = 0;
  unsigned long uimm = imm;

  if (!return_simm12_rest) {
    li(d, imm);
    return retval;
  }

  unsigned long low = uimm & 0xfff;
  unsigned long high = uimm >> 12;
  if (low >= 0x800) {
    retval = (int)low - 0x1000;
    ++high;
  }
  li(d, high << 12);
  return retval;
}

// We emit only one addition to s to optimize latency.
int Assembler::add_const_optimized(Register d, Register s, long imm, Register tmp, bool return_simm12_rest) {
  assert(s != d || tmp != noreg, "unsupported");
  assert(tmp != s, "tmp register should not be equal to src");
  unsigned long uimm = imm;

  // small constant
  if (-0x800 <= imm && imm < 0x800) {
    if (return_simm12_rest && s == d) {
      return imm;
    }
    addi(d, s, imm);
    return 0;
  }

  if (tmp == noreg) {
    // use destination as temp register
    tmp = d;
  }

  Register tmp1 = tmp,
           tmp2 = noreg;

  if (d != tmp && d != s) {
    tmp1 = d;
    tmp2 = tmp;
  }
  int simm12_rest = load_const_optimized(tmp1, imm, tmp2, return_simm12_rest);
  add(d, tmp1, s);
  return simm12_rest;
}

#ifndef PRODUCT
// Test of riscv assembler.
void Assembler::test_asm() {
  // RISCV 1, section 3.3.8, Fixed-Point Arithmetic Instructions
  addi_PPC(   R0,  R1,  10);
  addis_PPC(  R5,  R2,  11);
  addic__PPC( R3,  R31, 42);
  subfic_PPC( R21, R12, 2112);
  add_PPC(    R3,  R2,  R1);
  add__PPC(   R11, R22, R30);
  subf_PPC(   R7,  R6,  R5);
  subf__PPC(  R8,  R9,  R4);
  addc_PPC(   R11, R12, R13);
  addc__PPC(  R14, R14, R14);
  subfc_PPC(  R15, R16, R17);
  subfc__PPC( R18, R20, R19);
  adde_PPC(   R20, R22, R24);
  adde__PPC(  R29, R27, R26);
  subfe_PPC(  R28, R1,  R0);
  subfe__PPC( R21, R11, R29);
  neg_PPC(    R21, R22);
  neg__PPC(   R13, R23);
  mulli_PPC(  R0,  R11, -31);
  mulld_PPC(  R1,  R18, R21);
  mulld__PPC( R2,  R17, R22);
  mullw_PPC(  R3,  R16, R23);
  mullw__PPC( R4,  R15, R24);
  divd_PPC(   R5,  R14, R25);
  divd__PPC(  R6,  R13, R26);
  divw_PPC(   R7,  R12, R27);
  divw__PPC(  R8,  R11, R28);

  li_PPC(     R3, -4711);

  // RISCV 1, section 3.3.9, Fixed-Point Compare Instructions
  cmpi_PPC(   CCR7,  0, R27, 4711);
  cmp_PPC(    CCR0, 1, R14, R11);
  cmpli_PPC(  CCR5,  1, R17, 45);
  cmpl_PPC(   CCR3, 0, R9,  R10);

  cmpwi_PPC(  CCR7,  R27, 4711);
  cmpw_PPC(   CCR0, R14, R11);
  cmplwi_PPC( CCR5,  R17, 45);
  cmplw_PPC(  CCR3, R9,  R10);

  cmpdi_PPC(  CCR7,  R27, 4711);
  cmpd_PPC(   CCR0, R14, R11);
  cmpldi_PPC( CCR5,  R17, 45);
  cmpld_PPC(  CCR3, R9,  R10);

  // RISCV 1, section 3.3.11, Fixed-Point Logical Instructions
  andi__PPC(  R4,  R5,  0xff);
  andis__PPC( R12, R13, 0x7b51);
  ori_PPC(    R1,  R4,  13);
  oris_PPC(   R3,  R5,  177);
  xori_PPC(   R7,  R6,  51);
  xoris_PPC(  R29, R0,  1);
  andr_PPC(   R17, R21, R16);
  and__PPC(   R3,  R5,  R15);
  orr_PPC(    R2,  R1,  R9);
  or__PPC(    R17, R15, R11);
  xorr_PPC(   R19, R18, R10);
  xor__PPC(   R31, R21, R11);
  nand_PPC(   R5,  R7,  R3);
  nand__PPC(  R3,  R1,  R0);
  nor_PPC(    R2,  R3,  R5);
  nor__PPC(   R3,  R6,  R8);
  andc_PPC(   R25, R12, R11);
  andc__PPC(  R24, R22, R21);
  orc_PPC(    R20, R10, R12);
  orc__PPC(   R22, R2,  R13);

  nop_PPC();

  // RISCV 1, section 3.3.12, Fixed-Point Rotate and Shift Instructions
  sld_PPC(    R5,  R6,  R8);
  sld__PPC(   R3,  R5,  R9);
  slw_PPC(    R2,  R1,  R10);
  slw__PPC(   R6,  R26, R16);
  srd_PPC(    R16, R24, R8);
  srd__PPC(   R21, R14, R7);
  srw_PPC(    R22, R25, R29);
  srw__PPC(   R5,  R18, R17);
  srad_PPC(   R7,  R11, R0);
  srad__PPC(  R9,  R13, R1);
  sraw_PPC(   R7,  R15, R2);
  sraw__PPC(  R4,  R17, R3);
  sldi_PPC(   R3,  R18, 63);
  sldi__PPC(  R2,  R20, 30);
  slwi_PPC(   R1,  R21, 30);
  slwi__PPC(  R7,  R23, 8);
  srdi_PPC(   R0,  R19, 2);
  srdi__PPC(  R12, R24, 5);
  srwi_PPC(   R13, R27, 6);
  srwi__PPC(  R14, R29, 7);
  sradi_PPC(  R15, R30, 9);
  sradi__PPC( R16, R31, 19);
  srawi_PPC(  R17, R31, 15);
  srawi__PPC( R18, R31, 12);

  clrrdi_PPC( R3, R30, 5);
  clrldi_PPC( R9, R10, 11);

  rldicr_PPC( R19, R20, 13, 15);
  rldicr__PPC(R20, R20, 16, 14);
  rldicl_PPC( R21, R21, 30, 33);
  rldicl__PPC(R22, R1,  20, 25);
  rlwinm_PPC( R23, R2,  25, 10, 11);
  rlwinm__PPC(R24, R3,  12, 13, 14);

  // RISCV 1, section 3.3.2 Fixed-Point Load Instructions
  lwzx_PPC(   R3,  R5, R7);
  lwz_PPC(    R11,  0, R1);
  lwzu_PPC(   R31, -4, R11);

  lwax_PPC(   R3,  R5, R7);
  lwa_PPC(    R31, -4, R11);
  lhzx_PPC(   R3,  R5, R7);
  lhz_PPC(    R31, -4, R11);
  lhzu_PPC(   R31, -4, R11);


  lhax_PPC(   R3,  R5, R7);
  lha_PPC(    R31, -4, R11);
  lhau_PPC(   R11,  0, R1);

  lbzx_PPC(   R3,  R5, R7);
  lbz_PPC(    R31, -4, R11);
  lbzu_PPC(   R11,  0, R1);

  ld_PPC(     R31, -4, R11);
  ldx_PPC(    R3,  R5, R7);
  ldu_PPC(    R31, -4, R11);

  //  RISCV 1, section 3.3.3 Fixed-Point Store Instructions
  stwx_PPC(   R3,  R5, R7);
  stw_PPC(    R31, -4, R11);
  stwu_PPC(   R11,  0, R1);

  sthx_PPC(   R3,  R5, R7 );
  sth_PPC(    R31, -4, R11);
  sthu_PPC(   R31, -4, R11);

  stbx_PPC(   R3,  R5, R7);
  stb_PPC(    R31, -4, R11);
  stbu_PPC(   R31, -4, R11);

  std_PPC(    R31, -4, R11);
  stdx_PPC(   R3,  R5, R7);
  stdu_PPC(   R31, -4, R11);

 // RISCV 1, section 3.3.13 Move To/From System Register Instructions
  mtlr_PPC(   R3);
  mflr_PPC(   R3);
  mtctr_PPC(  R3);
  mfctr_PPC(  R3);
  mtcrf_PPC(  0xff, R15);
  mtcr_PPC(   R15);
  mtcrf_PPC(  0x03, R15);
  mtcr_PPC(   R15);
  mfcr_PPC(   R15);

 // RISCV 1, section 2.4.1 Branch Instructions
  Label lbl1, lbl2, lbl3;
  bind(lbl1);

  b_PPC(pc());
  b_PPC(pc() - 8);
  b_PPC(lbl1);
  b_PPC(lbl2);
  b_PPC(lbl3);

  bl_PPC(pc() - 8);
  bl_PPC(lbl1);
  bl_PPC(lbl2);

  bcl_PPC(4, 10, pc() - 8);
  bcl_PPC(4, 10, lbl1);
  bcl_PPC(4, 10, lbl2);

  bclr_PPC( 4, 6, 0);
  bclrl_PPC(4, 6, 0);

  bind(lbl2);

  bcctr_PPC( 4, 6, 0);
  bcctrl_PPC(4, 6, 0);

  blt_PPC(CCR0, lbl2);
  bgt_PPC(CCR1, lbl2);
  beq_PPC(CCR2, lbl2);
  bso_PPC(CCR3, lbl2);
  bge_PPC(CCR4, lbl2);
  ble_PPC(CCR5, lbl2);
  bne_PPC(CCR6, lbl2);
  bns_PPC(CCR7, lbl2);

  bltl_PPC(CCR0, lbl2);
  bgtl_PPC(CCR1, lbl2);
  beql_PPC(CCR2, lbl2);
  bsol_PPC(CCR3, lbl2);
  bgel_PPC(CCR4, lbl2);
  blel_PPC(CCR5, lbl2);
  bnel_PPC(CCR6, lbl2);
  bnsl_PPC(CCR7, lbl2);
  blr_PPC();

  sync_PPC();
  icbi_PPC( R1, R2);
  dcbst_PPC(R2, R3);

  // FLOATING POINT instructions riscv.
  // RISCV 1, section 4.6.2 Floating-Point Load Instructions
  lfs_PPC( F1, -11, R3);
  lfsu_PPC(F2, 123, R4);
  lfsx_PPC(F3, R5,  R6);
  lfd_PPC( F4, 456, R7);
  lfdu_PPC(F5, 789, R8);
  lfdx_PPC(F6, R10, R11);

  // RISCV 1, section 4.6.3 Floating-Point Store Instructions
  stfs_PPC(  F7,  876, R12);
  stfsu_PPC( F8,  543, R13);
  stfsx_PPC( F9,  R14, R15);
  stfd_PPC(  F10, 210, R16);
  stfdu_PPC( F11, 111, R17);
  stfdx_PPC( F12, R18, R19);

  // RISCV 1, section 4.6.4 Floating-Point Move Instructions
  fmr_PPC(   F13, F14);
  fmr__PPC(  F14, F15);
  fneg_PPC(  F16, F17);
  fneg__PPC( F18, F19);
  fabs_PPC(  F20, F21);
  fabs__PPC( F22, F23);
  fnabs_PPC( F24, F25);
  fnabs__PPC(F26, F27);

  // RISCV 1, section 4.6.5.1 Floating-Point Elementary Arithmetic
  // Instructions
  fadd_PPC(  F28, F29, F30);
  fadd__PPC( F31, F0,  F1);
  fadds_PPC( F2,  F3,  F4);
  fadds__PPC(F5,  F6,  F7);
  fsub_PPC(  F8,  F9,  F10);
  fsub__PPC( F11, F12, F13);
  fsubs_PPC( F14, F15, F16);
  fsubs__PPC(F17, F18, F19);
  fmul_PPC(  F20, F21, F22);
  fmul__PPC( F23, F24, F25);
  fmuls_PPC( F26, F27, F28);
  fmuls__PPC(F29, F30, F31);
  fdiv_PPC(  F0,  F1,  F2);
  fdiv__PPC( F3,  F4,  F5);
  fdivs_PPC( F6,  F7,  F8);
  fdivs__PPC(F9,  F10, F11);

  // RISCV 1, section 4.6.6 Floating-Point Rounding and Conversion
  // Instructions
  frsp_PPC(  F12, F13);
  fctid_PPC( F14, F15);
  fctidz_PPC(F16, F17);
  fctiw_PPC( F18, F19);
  fctiwz_PPC(F20, F21);
  fcfid_PPC( F22, F23);

  // RISCV 1, section 4.6.7 Floating-Point Compare Instructions
  fcmpu_PPC( CCR7, F24, F25);

  tty->print_cr("\ntest_asm disassembly (0x%lx 0x%lx):", p2i(code()->insts_begin()), p2i(code()->insts_end()));
  code()->decode();
}

#endif // !PRODUCT
