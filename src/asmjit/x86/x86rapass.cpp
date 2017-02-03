// [AsmJit]
// Complete x86/x64 JIT and Remote Assembler for C++.
//
// [License]
// Zlib - See LICENSE.md file in the package.

// [Export]
#define ASMJIT_EXPORTS

// [Guard]
#include "../asmjit_build.h"
#if defined(ASMJIT_BUILD_X86) && !defined(ASMJIT_DISABLE_COMPILER)

// [Dependencies]
#include "../base/cpuinfo.h"
#include "../base/utils.h"
#include "../x86/x86assembler.h"
#include "../x86/x86compiler.h"
#include "../x86/x86internal_p.h"
#include "../x86/x86rapass_p.h"

// [Api-Begin]
#include "../asmjit_apibegin.h"

namespace asmjit {

// ============================================================================
// [asmjit::X86RAPass - OpRWData]
// ============================================================================

#define R(IDX) { uint8_t(IDX), uint8_t(Any), uint16_t(TiedReg::kRReg) }
#define W(IDX) { uint8_t(Any), uint8_t(IDX), uint16_t(TiedReg::kWReg) }
#define X(IDX) { uint8_t(IDX), uint8_t(IDX), uint16_t(TiedReg::kXReg) }
#define NONE() { uint8_t(Any), uint8_t(Any), uint16_t(0) }
#define DEFINE_OPS(NAME, ...) static const OpRWData NAME[6] = { __VA_ARGS__ }
#define RETURN_OPS(...) do { DEFINE_OPS(ops, __VA_ARGS__); return ops; } while(0)

struct OpRWData {
  uint8_t rPhysId;
  uint8_t wPhysId;
  uint16_t flags;
};

static ASMJIT_INLINE const OpRWData* OpRWData_get(
  uint32_t instId, const X86Inst& instData, const Operand* opArray, uint32_t opCount) noexcept {

  enum {
    Zax = X86Gp::kIdAx,
    Zbx = X86Gp::kIdBx,
    Zcx = X86Gp::kIdCx,
    Zdx = X86Gp::kIdDx,
    Zsi = X86Gp::kIdSi,
    Zdi = X86Gp::kIdDi,
    Any = Globals::kInvalidRegId
  };

  // Common cases.
  DEFINE_OPS(rwiRO  , R(Any), R(Any), R(Any), R(Any), R(Any), R(Any));
  DEFINE_OPS(rwiWO  , W(Any), R(Any), R(Any), R(Any), R(Any), R(Any));
  DEFINE_OPS(rwiRW  , X(Any), R(Any), R(Any), R(Any), R(Any), R(Any));
  DEFINE_OPS(rwiXchg, X(Any), X(Any), R(Any), R(Any), R(Any), R(Any));

  const X86Inst::CommonData& commonData = instData.getCommonData();
  if (!commonData.isSpecial()) {
    if (commonData.isRW()) return rwiRW;
    if (commonData.isWO()) return rwiWO;
    if (commonData.isRO()) return rwiRO;
    if (commonData.isXchg()) return rwiXchg;
  }
  else {
    switch (instId) {
      // Deprecated.
      case X86Inst::kIdAaa:
      case X86Inst::kIdAad:
      case X86Inst::kIdAam:
      case X86Inst::kIdAas:
      case X86Inst::kIdDaa:
      case X86Inst::kIdDas:
        RETURN_OPS(X(Zax));

      // CPUID.
      case X86Inst::kIdCpuid:
        RETURN_OPS(X(Zax), W(Zbx), X(Zcx), W(Zdx));

      // Extend.
      case X86Inst::kIdCbw:
      case X86Inst::kIdCdqe:
      case X86Inst::kIdCwde:
        RETURN_OPS(X(Zax));

      case X86Inst::kIdCdq:
      case X86Inst::kIdCwd:
      case X86Inst::kIdCqo:
        RETURN_OPS(W(Zdx), R(Zax));

      // Cmpxchg.
      case X86Inst::kIdCmpxchg:
        RETURN_OPS(X(Any), R(Any), X(Zax));

      case X86Inst::kIdCmpxchg8b:
      case X86Inst::kIdCmpxchg16b:
        RETURN_OPS(NONE(), X(Zdx), X(Zax), R(Zcx), R(Zbx));

      // Mul/Div.
      case X86Inst::kIdDiv:
      case X86Inst::kIdIdiv:
        if (opCount == 2)
          RETURN_OPS(X(Zax), R(Any));
        else
          RETURN_OPS(X(Zdx), X(Zax), R(Any));

      case X86Inst::kIdImul:
        if (opCount == 2)
          return rwiRW;
        if (opCount == 3 && !(opArray[0].isReg() && opArray[1].isReg() && opArray[2].isRegOrMem()))
          return rwiRW;
        ASMJIT_FALLTHROUGH;

      case X86Inst::kIdMul:
        if (opCount == 2)
          RETURN_OPS(X(Zax), R(Any));
        else
          RETURN_OPS(W(Zdx), X(Zax), R(Any));

      case X86Inst::kIdMulx:
        RETURN_OPS(W(Any), W(Any), R(Any), R(Zdx));

      // Jecxz/Loop.
      case X86Inst::kIdJecxz:
      case X86Inst::kIdLoop:
      case X86Inst::kIdLoope:
      case X86Inst::kIdLoopne:
        RETURN_OPS(R(Zcx));

      // Lahf/Sahf.
      case X86Inst::kIdLahf: RETURN_OPS(W(Zax));
      case X86Inst::kIdSahf: RETURN_OPS(R(Zax));

      // Enter/Leave.
      case X86Inst::kIdEnter: break;
      case X86Inst::kIdLeave: break;

      // Ret.
      case X86Inst::kIdRet: break;

      // Monitor/MWait.
      case X86Inst::kIdMonitor    : return nullptr; // TODO: [COMPILER] Monitor/MWait.
      case X86Inst::kIdMwait      : return nullptr; // TODO: [COMPILER] Monitor/MWait.

      // Push/Pop.
      case X86Inst::kIdPush       : return rwiRO;
      case X86Inst::kIdPop        : return rwiWO;

      // Shift/Rotate.
      case X86Inst::kIdRcl:
      case X86Inst::kIdRcr:
      case X86Inst::kIdRol:
      case X86Inst::kIdRor:
      case X86Inst::kIdSal:
      case X86Inst::kIdSar:
      case X86Inst::kIdShl:
      case X86Inst::kIdShr:
        // Only special if the last operand is register.
        if (opArray[1].isReg())
          RETURN_OPS(X(Any), R(Zcx));
        else
          return rwiRW;

      case X86Inst::kIdShld:
      case X86Inst::kIdShrd:
        // Only special if the last operand is register.
        if (opArray[2].isReg())
          RETURN_OPS(X(Any), R(Any), R(Zcx));
        else
          return rwiRW;

      // RDTSC.
      case X86Inst::kIdRdtsc:
      case X86Inst::kIdRdtscp:
        RETURN_OPS(W(Zdx), W(Zax), W(Zcx));

      // Xsave/Xrstor.
      case X86Inst::kIdXrstor:
      case X86Inst::kIdXrstor64:
      case X86Inst::kIdXsave:
      case X86Inst::kIdXsave64:
      case X86Inst::kIdXsaveopt:
      case X86Inst::kIdXsaveopt64:
        RETURN_OPS(W(Any), R(Zdx), R(Zax));

      // Xsetbv/Xgetbv.
      case X86Inst::kIdXgetbv:
        RETURN_OPS(W(Zdx), W(Zax), R(Zcx));

      case X86Inst::kIdXsetbv:
        RETURN_OPS(R(Zdx), R(Zax), R(Zcx));

      // In/Out.
      case X86Inst::kIdIn  : RETURN_OPS(W(Zax), R(Zdx));
      case X86Inst::kIdIns : RETURN_OPS(X(Zdi), R(Zdx));
      case X86Inst::kIdOut : RETURN_OPS(R(Zdx), R(Zax));
      case X86Inst::kIdOuts: RETURN_OPS(R(Zdx), X(Zsi));

      // String instructions.
      case X86Inst::kIdCmps: RETURN_OPS(X(Zsi), X(Zdi));
      case X86Inst::kIdLods: RETURN_OPS(W(Zax), X(Zsi));
      case X86Inst::kIdMovs: RETURN_OPS(X(Zdi), X(Zsi));
      case X86Inst::kIdScas: RETURN_OPS(X(Zdi), R(Zax));
      case X86Inst::kIdStos: RETURN_OPS(X(Zdi), R(Zax));

      // SSE+/AVX+.
      case X86Inst::kIdMaskmovq:
      case X86Inst::kIdMaskmovdqu:
      case X86Inst::kIdVmaskmovdqu:
        RETURN_OPS(R(Any), R(Any), R(Zdi));

      // SSE4.1+ and SHA.
      case X86Inst::kIdBlendvpd:
      case X86Inst::kIdBlendvps:
      case X86Inst::kIdPblendvb:
      case X86Inst::kIdSha256rnds2:
        RETURN_OPS(W(Any), R(Any), R(0));

      // SSE4.2+.
      case X86Inst::kIdPcmpestri  :
      case X86Inst::kIdVpcmpestri : RETURN_OPS(R(Any), R(Any), NONE(), W(Zcx));
      case X86Inst::kIdPcmpistri  :
      case X86Inst::kIdVpcmpistri : RETURN_OPS(R(Any), R(Any), NONE(), W(Zcx), R(Zax), R(Zdx));
      case X86Inst::kIdPcmpestrm  :
      case X86Inst::kIdVpcmpestrm : RETURN_OPS(R(Any), R(Any), NONE(), W(0));
      case X86Inst::kIdPcmpistrm  :
      case X86Inst::kIdVpcmpistrm : RETURN_OPS(R(Any), R(Any), NONE(), W(0)  , R(Zax), R(Zdx));
    }
  }

  return rwiRW;
}

#undef RETURN_OPS
#undef DEFINE_OPS
#undef NONE
#undef X
#undef W
#undef R

// ============================================================================
// [asmjit::X86RAPass - Construction / Destruction]
// ============================================================================

X86RAPass::X86RAPass() noexcept
  : RAPass() {}
X86RAPass::~X86RAPass() noexcept {}

// ============================================================================
// [asmjit::X86RAPass - Prepare / Cleanup]
// ============================================================================

void X86RAPass::onInit() noexcept {
  uint32_t archType = cc()->getArchType();

  _archRegCount.set(X86Reg::kKindGp , archType == ArchInfo::kTypeX86 ? 7 : 15);
  _archRegCount.set(X86Reg::kKindMm , 8);
  _archRegCount.set(X86Reg::kKindK  , 7);
  _archRegCount.set(X86Reg::kKindVec, archType == ArchInfo::kTypeX86 ? 8 : 16);

  _allocableRegs.set(X86Reg::kKindGp , Utils::bits(_archRegCount.get(X86Reg::kKindGp ) & ~Utils::mask(X86Gp::kIdSp)));
  _allocableRegs.set(X86Reg::kKindMm , Utils::bits(_archRegCount.get(X86Reg::kKindMm )));
  _allocableRegs.set(X86Reg::kKindK  , Utils::bits(_archRegCount.get(X86Reg::kKindK  ) & ~Utils::mask(1))); // k0 is reserved.
  _allocableRegs.set(X86Reg::kKindVec, Utils::bits(_archRegCount.get(X86Reg::kKindVec)));

  if (_func->getFrameInfo().hasPreservedFP()) {
    _archRegCount._regs[X86Reg::kKindGp]--;
    _allocableRegs.andNot(X86Reg::kKindGp, Utils::mask(X86Gp::kIdBp));
  }

  _zsp = cc()->zsp();
  _zbp = cc()->zbp();

  _indexRegs = _allocableRegs.get(X86Reg::kKindGp) & ~Utils::mask(4);
  _avxEnabled = false;
}

void X86RAPass::onDone() noexcept {}

// ============================================================================
// [asmjit::X86RAPass - Steps - ConstructCFG]
// ============================================================================

class X86RACFGBuilder : public RACFGBuilder<X86RACFGBuilder> {
public:
  ASMJIT_INLINE X86RACFGBuilder(RAPass* pass) noexcept
    : RACFGBuilder<X86RACFGBuilder>(pass) {}

  ASMJIT_INLINE Error onInst(CBInst* inst, RABlock* block, uint32_t& jumpType, RARegStats& blockRegStats) noexcept {
    uint32_t instId = inst->getInstId();

    if (ASMJIT_UNLIKELY(!X86Inst::isDefinedId(instId)))
      return DebugUtils::errored(kErrorInvalidInstruction);

    const X86Inst& instData = X86Inst::getInst(instId);
    const X86Inst::CommonData& commonData = instData.getCommonData();

    X86Compiler* cc = static_cast<X86RAPass*>(_pass)->cc();
    uint32_t numVirtRegs = static_cast<uint32_t>(cc->getVirtRegArray().getLength());

    RATiedBuilder tb(_pass, block);
    uint32_t opCount = inst->getOpCount();
    uint32_t singleRegOps = 0;

    if (opCount) {
      const Operand* opArray = inst->getOpArray();
      const OpRWData* rwArray = OpRWData_get(instId, instData, opArray, opCount);

      for (uint32_t i = 0; i < opCount; i++) {
        const Operand& op = opArray[i];
        if (op.isReg()) {
          // Register operand.
          const X86Reg& reg = op.as<X86Reg>();
          uint32_t vIndex = Operand::unpackId(reg.getId());

          if (vIndex < Operand::kPackedIdCount) {
            if (ASMJIT_UNLIKELY(vIndex >= numVirtRegs))
              return DebugUtils::errored(kErrorInvalidVirtId);

            VirtReg* vReg = cc->getVirtRegAt(vIndex);
            uint32_t kind = vReg->getKind();

            uint32_t allocable = _pass->_allocableRegs.get(kind);
            ASMJIT_PROPAGATE(tb.add(vReg, rwArray[i].flags, allocable, rwArray[i].rPhysId, rwArray[i].wPhysId));

            if (singleRegOps == i)
              singleRegOps++;
          }
        }
        else if (op.isMem()) {
          // Memory operand.
          const X86Mem& mem = op.as<X86Mem>();
          if (mem.hasBaseReg()) {
            uint32_t vIndex = Operand::unpackId(mem.getBaseId());
            if (vIndex < Operand::kPackedIdCount) {
              if (ASMJIT_UNLIKELY(vIndex >= numVirtRegs))
                return DebugUtils::errored(kErrorInvalidVirtId);

              VirtReg* vReg = cc->getVirtRegAt(vIndex);
              uint32_t kind = vReg->getKind();

              uint32_t allocable = _pass->_allocableRegs.get(kind);
              ASMJIT_PROPAGATE(tb.add(vReg, TiedReg::kRReg, allocable, RAPass::kAnyReg, RAPass::kAnyReg));
            }
          }

          if (mem.hasIndexReg()) {
            uint32_t vIndex = Operand::unpackId(mem.getIndexId());
            if (vIndex < Operand::kPackedIdCount) {
              if (ASMJIT_UNLIKELY(vIndex >= numVirtRegs))
                return DebugUtils::errored(kErrorInvalidVirtId);

              VirtReg* vReg = cc->getVirtRegAt(vIndex);
              uint32_t kind = vReg->getKind();

              uint32_t allocable = _pass->_allocableRegs.get(kind);
              ASMJIT_PROPAGATE(tb.add(vReg, TiedReg::kRReg, allocable, RAPass::kAnyReg, RAPass::kAnyReg));
            }
          }
        }
      }
    }

    // Handle extra operand (either REP CX|ECX|RCX or AVX-512 {k} selector).
    if (inst->hasOpExtra()) {
      const Operand& opExtra = inst->getOpExtra();
      if (opExtra.isReg()) {
        const X86Reg& reg = opExtra.as<X86Reg>();
        uint32_t vIndex = Operand::unpackId(reg.getId());

        if (vIndex < Operand::kPackedIdCount) {
          if (ASMJIT_UNLIKELY(vIndex >= numVirtRegs))
            return DebugUtils::errored(kErrorInvalidVirtId);

          VirtReg* vReg = cc->getVirtRegAt(vIndex);
          uint32_t kind = vReg->getKind();

          if (kind == X86Gp::kKindK) {
            // AVX512 mask selector {k} register.
            //   (read-only, allocable to any register except {k0})
            ASMJIT_PROPAGATE(tb.add(vReg, TiedReg::kRReg, _pass->_allocableRegs.get(kind), RAPass::kAnyReg, RAPass::kAnyReg));
            singleRegOps = 0;
          }
          else {
            // REP {cx|ecx|rcx} register - read & write.
            ASMJIT_PROPAGATE(tb.add(vReg, TiedReg::kXReg, 0, X86Gp::kIdCx, X86Gp::kIdCx));
          }
        }
        else {
          uint32_t kind = reg.getKind();
          if (kind == X86Gp::kKindK && reg.getId() != 0)
            singleRegOps = 0;
        }
      }
    }

    // Handle special cases of some instructions where all operands share
    // the same register. In such case the single operand becomes read-only
    // or write-only.
    if (singleRegOps == opCount && tb.getTotal() == 1) {
      switch (commonData.getSingleRegCase()) {
        case X86Inst::kSingleRegNone:
          break;
        case X86Inst::kSingleRegRO:
          tb.tmp[0].flags &= ~TiedReg::kWReg;
          break;
        case X86Inst::kSingleRegWO:
          tb.tmp[0].flags &= ~TiedReg::kRReg;
          break;
      }
    }

    // Support for non `CBInst` nodes like `CCFuncCall` and `CCFuncRet`.
    if (inst->getType() != CBNode::kNodeInst) {
      // TODO:
      ASMJIT_ASSERT(!"IMPLEMENTED");
    }

    ASMJIT_PROPAGATE(tb.storeTo(inst));

    jumpType = commonData.getJumpType();
    blockRegStats.combineWith(tb.regStats);
  }
};

Error X86RAPass::constructCFG() noexcept {
  return X86RACFGBuilder(this).run();
}

} // asmjit namespace

// [Api-End]
#include "../asmjit_apiend.h"

// [Guard]
#endif // ASMJIT_BUILD_X86 && !ASMJIT_DISABLE_COMPILER
