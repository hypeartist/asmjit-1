// [AsmJit]
// Complete x86/x64 JIT and Remote Assembler for C++.
//
// [License]
// Zlib - See LICENSE.md file in the package.

// [Guard]
#ifndef _ASMJIT_X86_X86SSETOAVXPASS_P_H
#define _ASMJIT_X86_X86SSETOAVXPASS_P_H

#include "../asmjit_build.h"
#if !defined(ASMJIT_DISABLE_BUIILDER)

// [Dependencies]
#include "../x86/x86builder.h"

// [Api-Begin]
#include "../asmjit_apibegin.h"

namespace asmjit {

//! \addtogroup asmjit_x86
//! \{

// ============================================================================
// [asmjit::X86SseToAvxPass]
// ============================================================================

class X86SseToAvxPass : public CBPass {
  ASMJIT_NONCOPYABLE(X86SseToAvxPass)
public:
  typedef CBPass Base;

  X86SseToAvxPass() noexcept;
  Error run(Zone* zone) noexcept override;

  enum ProbeMask {
    kProbeMmx  = 1U << X86Reg::kRegMm,    //!< Instruction uses MMX registers.
    kProbeXmm  = 1U << X86Reg::kRegXmm    //!< Instruction uses XMM registers.
  };

  static ASMJIT_INLINE uint32_t probeRegs(const Operand* opArray, uint32_t opCount) noexcept {
    uint32_t mask = 0;
    for (uint32_t i = 0; i < opCount; i++) {
      const Operand& op = opArray[i];
      if (!op.isReg()) continue;
      mask |= Utils::mask(op.as<Reg>().getType());
    }
    return mask;
  }

  // --------------------------------------------------------------------------
  // [Members]
  // --------------------------------------------------------------------------

  bool _translated;
};

//! \}

} // asmjit namespace

// [Api-End]
#include "../asmjit_apiend.h"

// [Guard]
#endif // !ASMJIT_DISABLE_BUIILDER
#endif // _ASMJIT_X86_X86SSETOAVXPASS_P_H
