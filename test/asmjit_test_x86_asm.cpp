// [AsmJit]
// Complete x86/x64 JIT and Remote Assembler for C++.
//
// [License]
// Zlib - See LICENSE.md file in the package.

// [Dependencies]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#include "./asmjit.h"

using namespace asmjit;

// Signature of the generated function.
typedef void (*SumIntsFunc)(int* dst, const int* a, const int* b);

// This function works for both X86Assembler and X86Builder. It shows how
// `X86Emitter` can be used to make your code more generic.
static void makeFunc(X86Emitter* emitter) {
  // Decide which registers will be mapped to function arguments. Try changing
  // registers of `dst`, `src_a`, and `src_b` and see what happens in function's
  // prolog and epilog.
  X86Gp dst   = emitter->zax();
  X86Gp src_a = emitter->zcx();
  X86Gp src_b = emitter->zdx();

  // Decide which vector registers to use. We use these to keep the code generic,
  // you can switch to any other registers when needed.
  X86Xmm vec0 = x86::xmm0;
  X86Xmm vec1 = x86::xmm1;

  // Create and initialize `FuncDetail` and `FuncFrameInfo`. Both are
  // needed to create a function and they hold different kind of data.
  FuncDetail func;
  func.init(FuncSignature3<void, int*, const int*, const int*>(CallConv::kIdHost));

  FuncFrameInfo ffi;
  ffi.setDirtyRegs(X86Reg::kKindVec,      // Make XMM0 and XMM1 dirty. VEC kind
                   Utils::mask(0, 1));    // describes XMM|YMM|ZMM registers.

  FuncArgsMapper args(&func);             // Create function arguments mapper.
  args.assignAll(dst, src_a, src_b);      // Assign our registers to arguments.
  args.updateFrameInfo(ffi);              // Reflect our args in FuncFrameInfo.

  FuncFrameLayout layout;                 // Create the FuncFrameLayout, which
  layout.init(func, ffi);                 // contains metadata of prolog/epilog.

  // Emit prolog and allocate arguments to registers.
  FuncUtils::emitProlog(emitter, layout);
  FuncUtils::allocArgs(emitter, layout, args);

  emitter->movdqu(vec0, x86::ptr(src_a)); // Load 4 ints from [src_a] to XMM0.
  emitter->movdqu(vec1, x86::ptr(src_b)); // Load 4 ints from [src_b] to XMM1.
  emitter->paddd(vec0, vec1);             // Add 4 ints in XMM1 to XMM0.
  emitter->movdqu(x86::ptr(dst), vec0);   // Store the result to [dst].

  // Emit epilog and return.
  FuncUtils::emitEpilog(emitter, layout);
}

static int testFunc(uint32_t emitterType) {
  JitRuntime rt;                          // Create JIT Runtime
  FileLogger logger(stdout);              // Create logger that logs to stdout.

  CodeHolder code;                        // Create a CodeHolder.
  code.init(rt.getCodeInfo());            // Initialize it to match `rt`.
  code.setLogger(&logger);                // Attach logger to the code.

  Error err;
  if (emitterType == CodeEmitter::kTypeAssembler) {
    // Create the function by using X86Assembler.
    printf("Using X86Assembler:\n");
    X86Assembler a(&code);
    makeFunc(a.asEmitter());
  }
  else {
    // Create the function by using X86Builder.
    printf("Using X86Builder:\n");
    X86Builder cb(&code);
    makeFunc(cb.asEmitter());
    err = cb.finalize();
    if (err) {
      printf("X86Builder::finalize() failed: %s\n", DebugUtils::errorAsString(err));
      return 1;
    }
  }

  // Add the code generated to the runtime.
  SumIntsFunc fn;
  err = rt.add(&fn, &code);

  if (err) {
    printf("JitRuntime::add() failed: %s\n", DebugUtils::errorAsString(err));
    return 1;
  }

  // Execute the generated function.
  int inA[4] = { 4, 3, 2, 1 };
  int inB[4] = { 1, 5, 2, 8 };
  int out[4];
  fn(out, inA, inB);

  // Should print {5 8 4 9}.
  printf("Result = { %d %d %d %d }\n\n", out[0], out[1], out[2], out[3]);

  rt.release(fn);
  return !(out[0] == 5 && out[1] == 8 && out[2] == 4 && out[3] == 9);
}

int main(int argc, char* argv[]) {
  return testFunc(CodeEmitter::kTypeAssembler) |
         testFunc(CodeEmitter::kTypeBuilder);
}
