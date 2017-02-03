// [AsmJit]
// Complete x86/x64 JIT and Remote Assembler for C++.
//
// [License]
// Zlib - See LICENSE.md file in the package.

// [Guard]
#ifndef _ASMJIT_X86_X86RAPASS_P_H
#define _ASMJIT_X86_X86RAPASS_P_H

#include "../asmjit_build.h"
#if !defined(ASMJIT_DISABLE_COMPILER)

// [Dependencies]
#include "../base/codecompiler.h"
#include "../base/rabuilders_p.h"
#include "../base/rapass_p.h"
#include "../base/utils.h"
#include "../x86/x86assembler.h"
#include "../x86/x86compiler.h"

// [Api-Begin]
#include "../asmjit_apibegin.h"

namespace asmjit {

//! \addtogroup asmjit_x86
//! \{

// ============================================================================
// [asmjit::X86RAPass]
// ============================================================================

//! \internal
//!
//! X86 register allocation pass.
//!
//! Takes care of generating function prologs and epilogs, and also performs
//! register allocation.
class X86RAPass : public RAPass {
public:
  ASMJIT_NONCOPYABLE(X86RAPass)
  typedef RAPass Base;

  // --------------------------------------------------------------------------
  // [Construction / Destruction]
  // --------------------------------------------------------------------------

  X86RAPass() noexcept;
  virtual ~X86RAPass() noexcept;

  // --------------------------------------------------------------------------
  // [Init / Done]
  // --------------------------------------------------------------------------

  void onInit() noexcept override;
  void onDone() noexcept override;

  // --------------------------------------------------------------------------
  // [Steps]
  // --------------------------------------------------------------------------

  Error constructCFG() noexcept override;

  // --------------------------------------------------------------------------
  // [Accessors]
  // --------------------------------------------------------------------------

  //! Get compiler as `X86Compiler`.
  ASMJIT_INLINE X86Compiler* cc() const noexcept { return static_cast<X86Compiler*>(_cb); }
  //! Get a native size of GP register.
  ASMJIT_INLINE uint32_t getGpSize() const noexcept { return _zsp.getSize(); }

  // --------------------------------------------------------------------------
  // [Helpers]
  // --------------------------------------------------------------------------

  ASMJIT_INLINE RAData* newRAData(uint32_t tiedTotal) noexcept {
    return new(getZone()->alloc(sizeof(RAData) + tiedTotal * sizeof(TiedReg))) RAData(tiedTotal);
  }

  // --------------------------------------------------------------------------
  // [Members]
  // --------------------------------------------------------------------------

  X86Gp _zsp;                            //!< X86/X64 stack-pointer (ESP|RSP).
  X86Gp _zbp;                            //!< X86/X64 frame-pointer (EBP|RBP).

  uint32_t _indexRegs;
  bool _avxEnabled;
};

//! \}

} // asmjit namespace

// [Api-End]
#include "../asmjit_apiend.h"

// [Guard]
#endif // !ASMJIT_DISABLE_COMPILER
#endif // _ASMJIT_X86_X86RAPASS_P_H
