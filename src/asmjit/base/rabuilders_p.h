// [AsmJit]
// Complete x86/x64 JIT and Remote Assembler for C++.
//
// [License]
// Zlib - See LICENSE.md file in the package.

// [Guard]
#ifndef _ASMJIT_BASE_RABUILDERS_P_H
#define _ASMJIT_BASE_RABUILDERS_P_H

#include "../asmjit_build.h"
#if !defined(ASMJIT_DISABLE_COMPILER)

// [Dependencies]
#include "../base/rapass_p.h"

// [Api-Begin]
#include "../asmjit_apibegin.h"

namespace asmjit {

//! \addtogroup asmjit_base
//! \{

// ============================================================================
// [asmjit::RATiedBuilder]
// ============================================================================

class RATiedBuilder {
public:
  ASMJIT_NONCOPYABLE(RATiedBuilder)

  enum { kAnyReg = Globals::kInvalidRegId };

  // --------------------------------------------------------------------------
  // [Construction / Destruction]
  // --------------------------------------------------------------------------

  ASMJIT_INLINE RATiedBuilder(RAPass* pass, RABlock* block) noexcept {
    reset(pass, block);
  }

  // --------------------------------------------------------------------------
  // [Reset / Done]
  // --------------------------------------------------------------------------

  ASMJIT_INLINE void reset(RAPass* pass, RABlock* block) noexcept {
    this->pass = pass;
    this->block = block;
    this->regStats.reset();
    this->index.reset(); // TODO: Remove.
    this->count.reset(); // TODO: Remove.
    this->cur = tmp;
  }

  ASMJIT_INLINE void done() noexcept {
    index.indexFromRegCount(count);
  }

  // --------------------------------------------------------------------------
  // [Accessors]
  // --------------------------------------------------------------------------

  ASMJIT_INLINE uint32_t getTotal() const noexcept {
    return static_cast<uint32_t>((size_t)(cur - tmp));
  }

  // --------------------------------------------------------------------------
  // [Add]
  // --------------------------------------------------------------------------

  ASMJIT_INLINE Error add(VirtReg* vReg, uint32_t flags, uint32_t allocable, uint32_t rPhysId, uint32_t wPhysId) noexcept {
    TiedReg* tReg = vReg->getTiedReg();
    uint32_t kind = vReg->getKind();

    regStats.makeUsed(kind);
    if (rPhysId != kAnyReg || rPhysId != kAnyReg)
      regStats.makePrecolored(kind);

    if (!tReg) {
      // Could happen when the builder is not reset properly after each instruction.
      ASMJIT_ASSERT(getTotal() < ASMJIT_ARRAY_SIZE(tmp));

      ASMJIT_PROPAGATE(pass->addToWorkRegs(vReg));
      tReg = cur++;
      tReg->init(vReg, flags, allocable, rPhysId, wPhysId);
      vReg->setTiedReg(tReg);
      return kErrorOk;
    }
    else {
      // Already used by this node, thus it must have `workReg` already assigned.
      ASMJIT_ASSERT(vReg->hasWorkReg());

      // TODO: What about `rPhysId`, in that case we should perform a move
      // outside and ban coalescing.

      if (ASMJIT_UNLIKELY(wPhysId != kAnyReg)) {
        if (ASMJIT_UNLIKELY(tReg->wPhysId != kAnyReg))
          return DebugUtils::errored(kErrorOverlappedRegs);
        tReg->wPhysId = static_cast<uint8_t>(wPhysId);
      }

      tReg->refCount++;
      tReg->flags |= flags;
      tReg->allocableRegs &= allocable;
      return kErrorOk;
    }
  }

  // --------------------------------------------------------------------------
  // [Store]
  // --------------------------------------------------------------------------

  ASMJIT_INLINE Error storeTo(CBNode* node) noexcept {
    uint32_t total = getTotal();
    size_t size = sizeof(RAData) - sizeof(TiedReg) + total * sizeof(TiedReg);
    RAData* raData = pass->getZone()->allocT<RAData>(size);

    if (ASMJIT_UNLIKELY(!raData))
      return kErrorNoHeapMemory;

    raData->tiedTotal = total;
    raData->inRegs.reset();
    raData->outRegs.reset();
    raData->clobberedRegs.reset();
    raData->tiedIndex = index;
    raData->tiedCount = count;

    for (uint32_t i = 0; i < total; i++) {
      TiedReg* tReg = &tmp[i];
      VirtReg* vReg = tReg->vReg;

      vReg->resetTiedReg();

      if (tReg->rPhysId != kAnyReg || tReg->wPhysId != kAnyReg)
        block->addFlags(RABlock::kFlagHasFixedRegs);

      raData->tiedArray[i] = *tReg;
    }

    node->setPassData<RAData>(raData);
    return kErrorOk;
  }

  // --------------------------------------------------------------------------
  // [Members]
  // --------------------------------------------------------------------------

  RAPass* pass;
  RABlock* block;

  RARegStats regStats;

  // TODO: Maybe deprecated?
  RARegCount index;                      //!< Index of tied registers per kind.
  RARegCount count;                      //!< Count of tied registers per kind.

  TiedReg* cur;                          //!< Current tied register.
  TiedReg tmp[80];                       //!< Array of tied registers (temporary).
};

// ============================================================================
// [asmjit::RACFGBuilder]
// ============================================================================

template<typename This>
class RACFGBuilder {
public:
  ASMJIT_INLINE RACFGBuilder(RAPass* pass) noexcept
    : _pass(pass) {}

  ASMJIT_INLINE Error run() noexcept {
    ASMJIT_RA_LOG_INIT(_pass->getLogger());
    ASMJIT_RA_LOG_FORMAT("[RA::ConstructCFG]\n");

    CodeCompiler* cc = _pass->cc();
    CCFunc* func = _pass->getFunc();
    CBNode* node = func;

    // Create the first (entry) block.
    RABlock* currentBlock = _pass->newBlock(node);
    if (ASMJIT_UNLIKELY(!currentBlock))
      return DebugUtils::errored(kErrorNoHeapMemory);

    bool hasCode = false;
    size_t blockIndex = 0;
    uint32_t position = 0;

    RARegStats blockRegStats;
    blockRegStats.reset();

#if !defined(ASMJIT_DISABLE_LOGGING)
    StringBuilderTmp<256> sb;
    RABlock* lastPrintedBlock = nullptr;

    if (logger) {
      lastPrintedBlock = currentBlock;
      logger->logf("{Block #%u}\n", lastPrintedBlock->getBlockId());
    }
#endif // !ASMJIT_DISABLE_LOGGING

    for (;;) {
      for (;;) {
        ASMJIT_ASSERT(!node->hasPosition());
        node->setPosition(++position);

        if (node->getType() == CBNode::kNodeLabel) {
          if (!currentBlock) {
            // If the current code is unreachable the label makes it reachable again.
            currentBlock = node->as<CBLabel>()->getBlock();
            if (currentBlock) {
              // If the label has a block assigned we can either continue with
              // it or skip it if the block has been constructed already.
              if (currentBlock->isConstructed())
                break;
            }
            else {
              // Only create a new block if the label doesn't have assigned one.
              currentBlock = _pass->newBlock(node);
              if (ASMJIT_UNLIKELY(!currentBlock))
                return DebugUtils::errored(kErrorNoHeapMemory);

              node->as<CBLabel>()->setBlock(currentBlock);
              hasCode = false;
              blockRegStats.reset();
            }
          }
          else {
            // Label makes the current block constructed. There is a chance that the
            // Label is not used, but we don't know that at this point. Later, when
            // we have enough information we will be able to merge continuous blocks
            // into a single one if it's beneficial.
            currentBlock->setLast(node->getPrev());
            currentBlock->makeConstructed(blockRegStats);

            if (node->as<CBLabel>()->hasBlock()) {
              RABlock* successor = node->as<CBLabel>()->getBlock();
              if (currentBlock == successor) {
                // The label currently processed is part of the current block. This
                // is only possible for multiple labels that are right next to each
                // other, or are separated by .align directives and/or comments.
                if (hasCode)
                  return DebugUtils::errored(kErrorInvalidState);
              }
              else {
                ASMJIT_PROPAGATE(currentBlock->appendSuccessor(successor));
                _pass->logSuccessors(currentBlock);

                currentBlock = successor;
                hasCode = false;
                blockRegStats.reset();
              }
            }
            else {
              // First time we see this label.
              if (hasCode) {
                // Cannot continue the current block if it already contains some
                // code. We need to create a new block and make it a successor.
                currentBlock->setLast(node->getPrev());
                currentBlock->makeConstructed(blockRegStats);

                RABlock* successor = _pass->newBlock(node);
                if (ASMJIT_UNLIKELY(!successor))
                  return DebugUtils::errored(kErrorNoHeapMemory);

                ASMJIT_PROPAGATE(currentBlock->appendSuccessor(successor));
                _pass->logSuccessors(currentBlock);

                currentBlock = successor;
                hasCode = false;
                blockRegStats.reset();
              }

              node->as<CBLabel>()->setBlock(currentBlock);
            }
          }
#if !defined(ASMJIT_DISABLE_LOGGING)
          if (logger) {
            if (lastPrintedBlock != currentBlock) {
              lastPrintedBlock = currentBlock;
              logger->logf("{Block #%u}\n", lastPrintedBlock->getBlockId());
            }

            sb.clear();
            Logging::formatNode(sb, 0, cc, node);
            logger->logf("  %s\n", sb.getData());
          }
#endif // !ASMJIT_DISABLE_LOGGING
        }
        else {
#if !defined(ASMJIT_DISABLE_LOGGING)
          if (logger) {
            sb.clear();
            Logging::formatNode(sb, 0, cc, node);
            logger->logf("  %s\n", sb.getData());
          }
#endif // !ASMJIT_DISABLE_LOGGING

          if (node->actsAsInst()) {
            if (ASMJIT_UNLIKELY(!currentBlock)) {
              // If this code is unreachable then it has to be removed.
              CBNode* next = node->getNext();
              cc->removeNode(node);
              node = next;

              position--;
              continue;
            }
            else {
              // Handle `CBInst`, `CCFuncCall`, and `CCFuncRet`. All of
              // these share the `CBInst` interface and contain operands.
              hasCode = true;

              CBInst* inst = node->as<CBInst>();
              uint32_t jumpType = AnyInst::kJumpTypeNone;

              static_cast<This*>(this)->onInst(inst, currentBlock, jumpType, blockRegStats);

              // Support for conditional and unconditional jumps.
              if (jumpType == AnyInst::kJumpTypeDirect || jumpType == AnyInst::kJumpTypeConditional) {
                // Jmp/Jcc/Call/Loop/etc...
                uint32_t opCount = inst->getOpCount();
                const Operand* opArray = inst->getOpArray();

                // The last operand must be label (this supports also instructions
                // like jecx in explicit form).
                if (opCount == 0 || !opArray[opCount - 1].isLabel())
                  return DebugUtils::errored(kErrorInvalidState);

                CBLabel* cbLabel;
                ASMJIT_PROPAGATE(cc->getCBLabel(&cbLabel, opArray[opCount - 1].as<Label>()));

                RABlock* jumpSuccessor = _pass->newBlockOrMergeWith(cbLabel);
                if (ASMJIT_UNLIKELY(!jumpSuccessor))
                  return DebugUtils::errored(kErrorNoHeapMemory);

                currentBlock->setLast(node);
                currentBlock->makeConstructed(blockRegStats);
                ASMJIT_PROPAGATE(currentBlock->appendSuccessor(jumpSuccessor));

                if (jumpType == AnyInst::kJumpTypeDirect) {
                  // Unconditional jump makes the code after the jump unreachable,
                  // which will be removed instantly during the CFG construction;
                  // as we cannot allocate registers for instructions that are not
                  // part of any block. Of course we can leave these instructions
                  // as they are, however, that would only postpone the problem as
                  // assemblers can't encode instructions that use virtual registers.
                  _pass->logSuccessors(currentBlock);
                  currentBlock = nullptr;
                }
                else {
                  node = node->getNext();
                  if (ASMJIT_UNLIKELY(!node))
                    return DebugUtils::errored(kErrorInvalidState);

                  RABlock* flowSuccessor;
                  if (node->getType() == CBNode::kNodeLabel) {
                    if (node->as<CBLabel>()->hasBlock()) {
                      flowSuccessor = node->as<CBLabel>()->getBlock();
                    }
                    else {
                      flowSuccessor = _pass->newBlock(node);
                      if (ASMJIT_UNLIKELY(!flowSuccessor))
                        return DebugUtils::errored(kErrorNoHeapMemory);
                      node->as<CBLabel>()->setBlock(flowSuccessor);
                    }
                  }
                  else {
                    flowSuccessor = _pass->newBlock(node);
                    if (ASMJIT_UNLIKELY(!flowSuccessor))
                      return DebugUtils::errored(kErrorNoHeapMemory);
                  }

                  ASMJIT_PROPAGATE(currentBlock->prependSuccessor(flowSuccessor));
                  _pass->logSuccessors(currentBlock);

                  currentBlock = flowSuccessor;
                  hasCode = false;
                  blockRegStats.reset();

                  if (currentBlock->isConstructed())
                    break;

                  lastPrintedBlock = currentBlock;
                  ASMJIT_RA_LOG_FORMAT("{Block #%u}\n", lastPrintedBlock->getBlockId());
                  continue;
                }
              }
            }
          }
          else if (node->getType() == CBNode::kNodeSentinel) {
            // Sentinel could be anything, however, if this is the end of function
            // marker it's the function's exit. This means this node must be added
            // to `_exits`.
            if (node == func->getEnd()) {
              // Only add the current block to exists if it's reachable.
              if (currentBlock) {
                currentBlock->setLast(node);
                currentBlock->makeConstructed(blockRegStats);
                ASMJIT_PROPAGATE(_pass->_exits.append(&_pass->_heap, currentBlock));
              }
              break;
            }
          }
          else if (node->getType() == CBNode::kNodeFunc) {
            // CodeCompiler can only compile single function at a time. If we
            // encountered a function it must be the current one, bail if not.
            if (ASMJIT_UNLIKELY(node != func))
              return DebugUtils::errored(kErrorInvalidState);
            // PASS if this is the first node.
          }
          else {
            // PASS if this is a non-interesting or unknown node.
          }
        }

        // Advance to the next node.
        node = node->getNext();

        // NOTE: We cannot encounter a NULL node, because every function must be
        // terminated by a `stop` node. If we encountered a NULL node it means that
        // something went wrong and this node list is corrupted; bail in such case.
        if (ASMJIT_UNLIKELY(!node))
          return DebugUtils::errored(kErrorInvalidState);
      }

      // We finalized the current block so find another to process or return if
      // there are no more blocks.
      do {
        if (++blockIndex >= _pass->_blocks.getLength()) {
          _pass->_nodesCount = position;
          return kErrorOk;
        }

        currentBlock = _pass->_blocks[blockIndex];
      } while (currentBlock->isConstructed());

      node = currentBlock->getLast();
      hasCode = false;
      blockRegStats.reset();
    }
  }

  RAPass* _pass;
};

//! \}

} // asmjit namespace

// [Api-End]
#include "../asmjit_apiend.h"

// [Guard]
#endif // !ASMJIT_DISABLE_COMPILER
#endif // _ASMJIT_BASE_RABUILDERS_P_H
