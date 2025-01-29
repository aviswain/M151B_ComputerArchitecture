// Copyright 2025 Blaise Tine
//
// Licensed under the Apache License;
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <iostream>
#include <string>
#include <stdlib.h>
#include <string.h>
#include <iomanip>
#include <vector>
#include <unordered_map>
#include <util.h>
#include "debug.h"
#include "types.h"
#include "core.h"
#include "instr.h"

using namespace tinyrv;

/*
 * Elements are grouped together in the "Tiny RISC-V" namespace 
 * to create a logical structure within the entire project. This
 * namespace is used to define details of the RISC-V 32-I base
 * extension. 
 */
namespace tinyrv {

// Each opcode in RV32-I has a corresponding instr. type  
static const std::unordered_map<Opcode, InstType> sc_instTable = {
  {Opcode::R,     InstType::R},
  {Opcode::L,     InstType::I},
  {Opcode::I,     InstType::I},
  {Opcode::S,     InstType::S},
  {Opcode::B,     InstType::B},
  {Opcode::LUI,   InstType::U},
  {Opcode::AUIPC, InstType::U},
  {Opcode::JAL,   InstType::J},
  {Opcode::JALR,  InstType::I},
  {Opcode::SYS,   InstType::I},
  {Opcode::FENCE, InstType::I},
};


enum Constants {
  
  // bit-widths of certain instr. fields
  width_opcode= 7,
  width_reg   = 5,
  width_func3 = 3,
  width_func7 = 7,
  width_i_imm = 12,
  width_j_imm = 20,

  /* 
   * Step 1 of decoding a specific field out of an instruction
   * is shifting all the bits over until the field starts at the LSB
   *
   * The amounts we need to shift by for each field are made below. 
   */
  shift_opcode= 0,
  shift_rd    = width_opcode,
  shift_func3 = shift_rd + width_reg,
  shift_rs1   = shift_func3 + width_func3,
  shift_rs2   = shift_rs1 + width_reg,
  shift_func2 = shift_rs2 + width_reg,
  shift_func7 = shift_rs2 + width_reg,

  // shift constants I made
  shift_i_imm = shift_rs2,
  shift_u_imm = shift_func3,
  shift_j_imm = shift_func3,
  shift_s_imm_4_0 = shift_rd,
  shift_s_imm_11_5 = shift_func7,
  shift_b_imm_4_1_11 = shift_rd,
  shift_b_imm_12_10_5 = shift_func7,

  /*
   * Step 2 of decoding is using a bit-mask to extract the bits of
   * the field we need.
   *
   * The bit masks we need for each field are made below. 
   */
  mask_opcode = (1 << width_opcode)- 1,
  mask_reg    = (1 << width_reg)   - 1,
  mask_func3  = (1 << width_func3) - 1,
  mask_func7  = (1 << width_func7) - 1,
  mask_i_imm  = (1 << width_i_imm) - 1,
  mask_j_imm  = (1 << width_j_imm) - 1,

  // mask constants I made
  mask_u_imm = mask_j_imm,
  mask_s_imm_4_0 = mask_reg,
  mask_s_imm_11_5 = mask_func7,
  mask_b_imm_4_1_11 = mask_reg,
  mask_b_imm_12_10_5 = mask_func7,
};

/*
 * Returns the string mnemonic of an instruction
 * based on its opcode and function fields
 */
static const char* op_string(const Instr &instr) {
  auto opcode = instr.getOpcode();
  auto func3  = instr.getFunc3();
  auto func7  = instr.getFunc7();
  auto imm    = instr.getImm();

  switch (opcode) {
  case Opcode::LUI:   return "LUI";
  case Opcode::AUIPC: return "AUIPC";
  case Opcode::R:
    switch (func3) {
    case 0: return func7 ? "SUB" : "ADD";
    case 1: return "SLL";
    case 2: return "SLT";
    case 3: return "SLTU";
    case 4: return "XOR";
    case 5: return (func7 & 0x20) ? "SRA" : "SRL";
    case 6: return "OR";
    case 7: return "AND";
    default:
      std::abort();
    }
  case Opcode::I:
    switch (func3) {
    case 0: return "ADDI";
    case 1: return "SLLI";
    case 2: return "SLTI";
    case 3: return "SLTIU";
    case 4: return "XORI";
    case 5: return (func7 & 0x20) ? "SRAI" : "SRLI";
    case 6: return "ORI";
    case 7: return "ANDI";
    default:
      std::abort();
    }
  case Opcode::B:
    switch (func3) {
    case 0: return "BEQ";
    case 1: return "BNE";
    case 4: return "BLT";
    case 5: return "BGE";
    case 6: return "BLTU";
    case 7: return "BGEU";
    default:
      std::abort();
    }
  case Opcode::JAL:  return "JAL";
  case Opcode::JALR: return "JALR";
  case Opcode::L:
    switch (func3) {
    case 0: return "LB";
    case 1: return "LH";
    case 2: return "LW";
    case 3: return "LD";
    case 4: return "LBU";
    case 5: return "LHU";
    case 6: return "LWU";
    default:
      std::abort();
    }
  case Opcode::S:
    switch (func3) {
    case 0: return "SB";
    case 1: return "SH";
    case 2: return "SW";
    case 3: return "SD";
    default:
      std::abort();
    }
  case Opcode::SYS:
    switch (func3) {
    case 0:
      switch (imm) {
      case 0x000: return "ECALL";
      case 0x001: return "EBREAK";
      case 0x002: return "URET";
      case 0x102: return "SRET";
      case 0x302: return "MRET";
      default:
        std::abort();
      }
    case 1: return "CSRRW";
    case 2: return "CSRRS";
    case 3: return "CSRRC";
    case 5: return "CSRRWI";
    case 6: return "CSRRSI";
    case 7: return "CSRRCI";
    default:
      std::abort();
    }
  case Opcode::FENCE:
    return "FENCE";
  default:
    std::abort();
  }
}

/*
 * Output the complete string representation of an instruction
 */
std::ostream &operator<<(std::ostream &os, const Instr &instr) {
  os << op_string(instr);
  int sep = 0;

  // Execution flags specify the behaviors of the instruction
  auto exec_flags = instr.getExeFlags();

  /*
   * Output the instruction's use of a destination register, 
   * source registers, or an immediate value.
   */
  if (exec_flags.use_rd) {
    if (sep++ != 0) { os << ", "; } else { os << " "; }
    os << "x" << std::dec << instr.getRd();
  }

  if (exec_flags.use_rs1) {
    if (sep++ != 0) { os << ", "; } else { os << " "; }
    os << "x" << std::dec << instr.getRs1();
  }

  if (exec_flags.use_rs2) {
    if (sep++ != 0) { os << ", "; } else { os << " "; }
    os << "x" << std::dec << instr.getRs2();
  }

  if (exec_flags.use_imm) {
    if (sep++ != 0) { os << ", "; } else { os << " "; }
    os << "0x" << std::hex << instr.getImm();
  }

  /*
   * Output the instruction's alu operation, branch 
   * operation and execution flags
   */
  os << std::dec << ", alu_op=" << instr.getAluOp()
      << ", br_op=" << instr.getBrOp()
     << ", exe_flags=" << instr.getExeFlags();

  return os;
}

}

std::shared_ptr<Instr> Core::decode(uint32_t instr_code) const {

  /*
   * STEP 1: Parallel extraction of various instruction fields. 
   * --------------------------------------------------------------------------
   * Extract the following fields:

   *   - opcode, opcode
   *   - function 1, func3
   *   - function 2, func7
   *   - dest register, rd
   *   - src register 1, rs1
   *   - src register 2, rs2
   * 
   * Although not all fields will be used based on instruction type, extracting
   * the fields in parallel is better for energy efficiency because it 
   * reduces sequential logic.
   *
   * Insight: Activating decoders for unused fields and running them in 
   * parallel > understanding instruction and only using needed decoders.
   */
  auto instr = std::make_shared<Instr>();
  auto opcode = Opcode((instr_code >> shift_opcode) & mask_opcode);

  auto func3 = (instr_code >> shift_func3) & mask_func3;
  auto func7 = (instr_code >> shift_func7) & mask_func7;

  auto rd  = (instr_code >> shift_rd)  & mask_reg;
  auto rs1 = (instr_code >> shift_rs1) & mask_reg;
  auto rs2 = (instr_code >> shift_rs2) & mask_reg;

  /*
   * STEP 2: Get the instruction type
   * --------------------------------------------------------------------------
   * If the opcode is in the instruction table, get the instruction type.
   * Otherwise it is not supported by the decoder.
   */ 
  auto op_it = sc_instTable.find(opcode);
  if (op_it == sc_instTable.end()) {
    std::cout << std::hex << "Error: invalid opcode: 0x" << static_cast<int>(opcode) << std::endl;
    return nullptr;
  }

  /*
   * STEP 3: Decode instruction based on type
   * --------------------------------------------------------------------------
   * Set appropriate execution flags, immediate value, ALU operation, and/or
   * branch operation based on the instruction type and opcode.
   */

  ExeFlags exe_flags;
  memset(&exe_flags, 0, sizeof(ExeFlags));
  uint32_t imm = 0x0;

  auto inst_type = op_it->second;
  switch (inst_type) {
  case InstType::R:
    exe_flags.use_rd  = 1;
    exe_flags.use_rs1 = 1;
    exe_flags.use_rs2 = 1;
    break;

  case InstType::I: {
    switch (opcode) {
    case Opcode::I:
      exe_flags.use_rd  = 1;
      exe_flags.use_rs1 = 1;
      exe_flags.use_imm = 1;
      exe_flags.alu_s2_imm = 1;
      // CHECK:

      // extract immediate out of instr.
      imm = (instr_code >> shift_i_imm) & mask_i_imm;
      
      // sign extend 
      uint32_t sign_bit = imm & 0x800;
      imm = (imm << 20) >> 20;
      if (sign_bit) {
        imm |= 0xFFFFF000;
      }

      break;
    case Opcode::L:
    case Opcode::JALR: {
      exe_flags.use_rd  = 1;
      exe_flags.use_rs1 = 1;
      exe_flags.use_imm = 1;
      exe_flags.alu_s2_imm = 1;

      // CHECK:

      // extract immediate out of instr.
      imm = (instr_code >> shift_i_imm) & mask_i_imm;
      
      // sign extend 
      uint32_t sign_bit = imm & 0x800;
      imm = (imm << 20) >> 20;
      if (sign_bit) {
        imm |= 0xFFFFF000;
      }

    } break;
    case Opcode::SYS: {
      exe_flags.use_imm = 1;
      if (func3 != 0) {
        // CSR instructions
        exe_flags.use_rd = 1;
        if (func3 < 5) {
          exe_flags.use_rs1 = 1;
        }
      }
      imm = // TODO:
    } break;
    case Opcode::FENCE:
      break;
    default:
      std::abort();
      break;
    }
  } break;
  case InstType::S: {
    exe_flags.use_rs1 = 1;
    exe_flags.use_rs2 = 1;
    exe_flags.use_imm = 1;
    exe_flags.alu_s2_imm = 1;
    // CHECK:

    // extract the immediate values out of instruction
    uint32_t imm_4_0 = (instr_code >> shift_s_imm_4_0) & mask_s_imm_4_0;
    uint32_t imm_11_5 = (instr_code >> shift_s_imm_11_5) & mask_s_imm_11_5;

    // rearrange the immediate according to the S-type format
    imm = (imm_11_5 << 5) | imm_4_0;

    // sign extend
    uint32_t sign_bit = imm & 0x800;
    if (sign_bit) {
      imm |= 0xFFFFF000;
    }

  } break;

  case InstType::B: {
    exe_flags.use_rs1 = 1;
    exe_flags.use_rs2 = 1;
    exe_flags.use_imm = 1;
    exe_flags.alu_s2_imm = 1;
    // CHECK:

    // extract the immediate values out of instruction
    uint32_t imm_4_1_11 = (instr_code >> shift_b_imm_4_1_11) & mask_b_imm_4_1_11;
    uint32_t imm_12_10_5 = (instr_code >> shift_b_imm_12_10_5) & mask_b_imm_12_10_5;

    uint32_t imm_4_1 = (imm_4_1_11 >> 1) & 0xF;
    uint32_t imm_11 = imm_4_1_11 & 0x1;
    uint32_t imm_10_5 = imm_12_10_5 & 0x3F;
    uint32_t imm_12 = (imm_12_10_5 >> 6) & 0x1;

    // rearrange the immediate according to the S-type format but [11:0] instead of [12:1]
    imm = (imm_12 << 11) | (imm_11 << 10) | (imm_10_5 << 4) | imm_4_1;

    // sign extend
    uint32_t sign_bit = imm & 0x800;
    if (sign_bit) {
      imm |= 0xFFFFF000;
    }

    // shift left once to return immediate to [12:1]
    imm = imm << 1;    
  } break;

  case InstType::U: {
    exe_flags.use_rd  = 1;
    exe_flags.use_imm = 1;
    exe_flags.alu_s2_imm = 1;
    // CHECK: 
    imm = (instr_code >> shift_u_imm) & mask_u_imm;
    imm = imm << 12;
  } break;

  case InstType::J: {
    exe_flags.use_rd  = 1;
    exe_flags.use_imm = 1;
    exe_flags.alu_s2_imm = 1;
    // CHECK:
    // extract the immediate out of instruction
    imm = (instr_code >> shift_j_imm) & mask_j_imm;

    // rearrange the immediate according to the J-type format
    uint32_t imm_20 = (imm >> 19) & 0x1;

    uint32_t imm_19_12 = imm & 0xFF;
    uint32_t imm_11 = (imm >> 8) & 0x1;
    uint32_t imm_10_1 = (imm >> 9) & 0x3FF;

    imm = (imm_20 << 19) | (imm_19_12 << 11) | (imm_11 << 10) | imm_10_1;

    // sign extend to 32 bits
    uint32_t sign_bit = imm & 0x80000;
    imm = (imm << 12) >> 12;
    if (sign_bit) {
      imm |= 0xFFF00000;
    }

    // shift left for empty 0 bit
    imm = imm << 1;
  } break;

  default:
    std::abort();
  }

  // instruction opcode decoding

  AluOp alu_op = AluOp::NONE;
  BrOp br_op = BrOp::NONE;

  switch (opcode) {
  case Opcode::LUI: {
    // RV32I: LUI
    // CHECK:
    alu_op = AluOp::SLL;
    break;
  }
  case Opcode::AUIPC: {
    // RV32I: AUIPC
    // CHECK:
    alu_op = AluOp::ADD;
    exe_flags.alu_s1_PC = 1;
    break;
  }
  case Opcode::R: {
    // CHECK:

    switch(func3) {
      case 0x0:
        if (func7 == 0x00) {
          alu_op = AluOp::ADD;
        } else if (func7 == 0x20) {
          alu_op = AluOp::SUB;
        }
        break;
      case 0x4:
        alu_op = AluOp::XOR;
        break;
      case 0x6:
        alu_op = AluOp::OR;
        break;
      case 0x7:
        alu_op = AluOp::AND;
        break;
      case 0x1:
        alu_op = AluOp::SLL;
        break;
      case 0x5:
        if (func7 == 0x00) {
          alu_op = AluOp::SRL;
        } else if (func7 == 0x20) {
          alu_op = AluOp::SRA;
        }
        break;
      case 0x2:
        alu_op = AluOp::LTI;
        break;
      case 0x3:
        alu_op = AluOp::LTU;
        break;
      default:
        std::cout << std::hex << "Error: invalid func3 value in R-type instruction: 0x" << static_cast<int>(func3) << std::endl;
    }
  }
  case Opcode::I: {
    // CHECK:
    switch(func3) {
      case 0x0:
        alu_op = AluOp::ADD;
        break;
      case 0x4:
        alu_op = AluOp::XOR;
        break;
      case 0x6:
        alu_op = AluOp::OR;
        break;
      case 0x7:
        alu_op = AluOp::AND;
        break;
      case 0x1:
        if (func7 == 0x00) {
          AluOp::SLL;
        }
        break;
      case 0x5:
        if (func7 == 0x00) {
          alu_op = AluOp::SRL;
        } else if (func7 == 0x20) {
          alu_op = AluOp::SRA;
        }
        break;
      case 0x2:
        alu_op = AluOp::LTI;
        break;
      case 0x3:
        alu_op = AluOp::LTU;
        break;
      default:
        std::cout << std::hex << "Error: invalid func3 value in I-type instruction: 0x" << static_cast<int>(func3) << std::endl;
    }
  }
  case Opcode::B: {
    exe_flags.alu_s1_PC = 1;
    // CHECK:
    alu_op = AluOp::SUB;
    switch(func3) {
      case 0x0:
        br_op = BrOp::BEQ;
        break;
      case 0x1:
        br_op = BrOp::BNE;
        break;
      case 0x4:
        br_op = BrOp::BLT;
        break;
      case 0x5:
        br_op = BrOp::BGE;
        break;
      case 0x6:
        br_op = BrOp::BLTU;
        break;
      case 0x7:
        br_op = BrOp::BGEU;
        break;
      default:
        std::cout << std::hex << "Error: invalid func3 value in B-type instruction: 0x" << static_cast<int>(func3) << std::endl;
    }
    break;
  }
  case Opcode::JAL: {
    exe_flags.alu_s1_PC = 1;
    alu_op = // TODO:
    br_op = // TODO:
    break;
  }
  case Opcode::JALR: {
    alu_op = // TODO:
    br_op = // TODO:
    break;
  }
  case Opcode::L: {
    // RV32I: LB, LH, LW, LBU, LHU
    alu_op = // TODO:
    exe_flags.is_load = 1;
    break;
  }
  case Opcode::S: {
    // RV32I: SB, SH, SW
    alu_op = // TODO:
    exe_flags.is_store = 1;
    break;
  }
  case Opcode::SYS: {
    if (func3 == 0) {
      alu_op = AluOp::ADD;
      switch (imm) {
      case 0x000: // RV32I: ECALL
      case 0x001: // RV32I: EBREAK
        exe_flags.is_exit = 1;
        break;
      case 0x002: // RV32I: URET
      case 0x102: // RV32I: SRET
      case 0x302: // RV32I: MRET
        break;
      default:
        std::abort();
      }
    } else {
      exe_flags.is_csr = 1;
      exe_flags.alu_s2_csr = 1;
      switch (func3) {
      case 1: {
        // RV32I: CSRRW
        alu_op = AluOp::ADD;
        break;
      }
      case 2: {
        // RV32I: CSRRS
        alu_op = AluOp::OR;
        break;
      }
      case 3: {
        // RV32I: CSRRC
        alu_op = AluOp::AND;
        exe_flags.alu_s1_inv = 1;
        break;
      }
      case 5: {
        // RV32I: CSRRWI
        alu_op = AluOp::ADD;
        exe_flags.alu_s1_rs1 = 1;
        break;
      }
      case 6: {
        // RV32I: CSRRSI;
        alu_op = AluOp::OR;
        exe_flags.alu_s1_rs1 = 1;
        break;
      }
      case 7: {
        // RV32I: CSRRCI
        alu_op = AluOp::AND;
        exe_flags.alu_s1_inv = 1;
        exe_flags.alu_s1_rs1 = 1;
        break;
      }
      default:
        std::abort();
      }
    }
    break;
  }
  case Opcode::FENCE: {
    // RV32I: FENCE
    break;
  }
  default:
    std::abort();
  }

  instr->setOpcode(opcode);
  instr->setRd(rd);
  instr->setSrc1(rs1);
  instr->setSrc2(rs2);
  instr->setImm(imm);
  instr->setFunc3(func3);
  instr->setFunc7(func7);
  instr->setAluOp(alu_op);
  instr->setBrOp(br_op);
  instr->setExeFlags(exe_flags);

  return instr;
}
