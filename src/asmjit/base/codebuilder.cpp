// [AsmJit]
// Complete x86/x64 JIT and Remote Assembler for C++.
//
// [License]
// Zlib - See LICENSE.md file in the package.

// [Export]
#define ASMJIT_EXPORTS

// [Guard]
#include "../asmjit_build.h"
#if !defined(ASMJIT_DISABLE_BUILDER)

// [Dependencies]
#include "../base/codebuilder.h"
#include "../base/logging.h"

// [Api-Begin]
#include "../asmjit_apibegin.h"

namespace asmjit {

// ============================================================================
// [asmjit::CodeBuilder - Construction / Destruction]
// ============================================================================

CodeBuilder::CodeBuilder() noexcept
  : CodeEmitter(kTypeBuilder),
    _cbBaseZone(32768 - Zone::kZoneOverhead),
    _cbDataZone(16384 - Zone::kZoneOverhead),
    _cbPassZone(32768 - Zone::kZoneOverhead),
    _cbHeap(&_cbBaseZone),
    _cbPasses(),
    _cbLabels(),
    _firstNode(nullptr),
    _lastNode(nullptr),
    _cursor(nullptr),
    _nodeFlags(0) {}
CodeBuilder::~CodeBuilder() noexcept {}

// ============================================================================
// [asmjit::CodeBuilder - Events]
// ============================================================================

Error CodeBuilder::onAttach(CodeHolder* code) noexcept {
  return Base::onAttach(code);
}

Error CodeBuilder::onDetach(CodeHolder* code) noexcept {
  _cbPasses.reset();
  _cbLabels.reset();
  _cbHeap.reset(&_cbBaseZone);

  _cbBaseZone.reset(false);
  _cbDataZone.reset(false);
  _cbPassZone.reset(false);

  _nodeFlags = 0;

  _firstNode = nullptr;
  _lastNode = nullptr;
  _cursor = nullptr;

  return Base::onDetach(code);
}

// ============================================================================
// [asmjit::CodeBuilder - Node-Factory]
// ============================================================================

Error CodeBuilder::getCBLabel(CBLabel** pOut, uint32_t id) noexcept {
  if (_lastError)
    return _lastError;

  ASMJIT_ASSERT(_code != nullptr);
  size_t index = Operand::unpackId(id);

  if (ASMJIT_UNLIKELY(index >= _code->getLabelsCount()))
    return DebugUtils::errored(kErrorInvalidLabel);

  if (index >= _cbLabels.getLength())
    ASMJIT_PROPAGATE(_cbLabels.resize(&_cbHeap, index + 1));

  CBLabel* node = _cbLabels[index];
  if (!node) {
    node = newNodeT<CBLabel>(id);
    if (ASMJIT_UNLIKELY(!node))
      return DebugUtils::errored(kErrorNoHeapMemory);
    _cbLabels[index] = node;
  }

  *pOut = node;
  return kErrorOk;
}

Error CodeBuilder::registerLabelNode(CBLabel* node) noexcept {
  if (_lastError)
    return _lastError;

  ASMJIT_ASSERT(_code != nullptr);

  // Don't call setLastError() from here, we are noexcept and we are called
  // by `newLabelNode()` and `newFuncNode()`, which are noexcept as well.
  uint32_t id;
  ASMJIT_PROPAGATE(_code->newLabelId(id));
  size_t index = Operand::unpackId(id);

  // We just added one label so it must be true.
  ASMJIT_ASSERT(_cbLabels.getLength() < index + 1);
  ASMJIT_PROPAGATE(_cbLabels.resize(&_cbHeap, index + 1));

  _cbLabels[index] = node;
  node->_id = id;
  return kErrorOk;
}

CBLabel* CodeBuilder::newLabelNode() noexcept {
  CBLabel* node = newNodeT<CBLabel>();
  if (!node || registerLabelNode(node) != kErrorOk)
    return nullptr;
  return node;
}

CBAlign* CodeBuilder::newAlignNode(uint32_t mode, uint32_t alignment) noexcept {
  return newNodeT<CBAlign>(mode, alignment);
}

CBData* CodeBuilder::newDataNode(const void* data, uint32_t size) noexcept {
  if (size > CBData::kInlineBufferSize) {
    void* cloned = _cbDataZone.alloc(size);
    if (ASMJIT_UNLIKELY(!cloned))
      return nullptr;

    if (data)
      data = ::memcpy(cloned, data, size);
  }

  return newNodeT<CBData>(const_cast<void*>(data), size);
}

CBConstPool* CodeBuilder::newConstPool() noexcept {
  CBConstPool* node = newNodeT<CBConstPool>();
  if (!node || registerLabelNode(node) != kErrorOk)
    return nullptr;
  return node;
}

CBComment* CodeBuilder::newCommentNode(const char* s, size_t len) noexcept {
  if (s) {
    if (len == Globals::kInvalidIndex)
      len = ::strlen(s);

    if (len > 0) {
      s = static_cast<char*>(_cbDataZone.dup(s, len, true));
      if (!s) return nullptr;
    }
  }

  return newNodeT<CBComment>(s);
}

// ============================================================================
// [asmjit::CodeBuilder - Code-Emitter]
// ============================================================================

Error CodeBuilder::_emit(uint32_t instId, const Operand_& o0, const Operand_& o1, const Operand_& o2, const Operand_& o3) {
  uint32_t options = getOptions() | getGlobalOptions();
  const char* inlineComment = getInlineComment();

  uint32_t opCount = (!o3.isNone()) ? 4 :
                     (!o2.isNone()) ? 3 :
                     (!o1.isNone()) ? 2 :
                     (!o0.isNone()) ? 1 : 0;

  // Handle failure and rare cases first.
  const uint32_t kErrorsAndSpecialCases =
    kOptionMaybeFailureCase | // CodeEmitter in error state.
    kOptionStrictValidation | // Strict validation.
    kOptionOp4              | // Has 4th operand (indexed from zero).
    kOptionOp5              ; // Has 5th operand (indexed from zero).

  if (options & kErrorsAndSpecialCases) {
    // Don't do anything if we are in error state.
    if (_lastError)
      return _lastError;

    // Count 4th and 5th operands (indexed from zero).
    if (options & kOptionOp4) opCount = 5;
    if (options & kOptionOp5) opCount = 6;

#if !defined(ASMJIT_DISABLE_VALIDATION)
    // Strict validation.
    if (options & kOptionStrictValidation) {
      Error err = _validate(instId, o0, o1, o2, o3);
      if (ASMJIT_UNLIKELY(err != kErrorOk)) {
        resetOptions();
        resetInlineComment();
        return setLastError(err);
      }
    }
#endif // ASMJIT_DISABLE_VALIDATION

    // Clear flags that should not be added to `CBInst`.
    options &= ~(kOptionMaybeFailureCase | kOptionStrictValidation);
  }

  resetOptions();
  resetInlineComment();

  uint32_t opCapacity = CBInst::capacityOfOpCount(opCount);
  ASMJIT_ASSERT(opCapacity >= 4);

  CBInst* node = _cbHeap.allocT<CBInst>(CBInst::nodeSizeOfOpCapacity(opCapacity));
  if (ASMJIT_UNLIKELY(!node))
    return setLastError(DebugUtils::errored(kErrorNoHeapMemory));

  new(node) CBInst(this, instId, options, opCapacity);

  node->setOpCount(opCount);
  if (options & kOptionOpExtra)
    node->_opExtra = _opExtra;

  node->setOp(0, o0);
  node->setOp(1, o1);
  node->setOp(2, o2);
  node->setOp(3, o3);

  if (CBInst::kBaseOpCapacity > 4) {
    // Compile-time: Capacity of 5 operands by default in 32-bit mode.
    node->resetOp(4);
    if (opCapacity > 5) node->resetOp(5);
  }
  else if (opCapacity > 4) {
    node->resetOp(4);
    if (opCapacity > 5) node->resetOp(5);
  }

  if (options & kOptionOp4) node->setOp(4, _op4);
  if (options & kOptionOp5) node->setOp(5, _op5);

  if (inlineComment)
    node->setInlineComment(static_cast<char*>(_cbDataZone.dup(inlineComment, ::strlen(inlineComment), true)));

  addNode(node);
  return kErrorOk;
}

Label CodeBuilder::newLabel() {
  uint32_t id = 0;
  if (!_lastError) {
    CBLabel* node = newNodeT<CBLabel>(id);
    if (ASMJIT_UNLIKELY(!node)) {
      setLastError(DebugUtils::errored(kErrorNoHeapMemory));
    }
    else {
      Error err = registerLabelNode(node);
      if (ASMJIT_UNLIKELY(err))
        setLastError(err);
      else
        id = node->getId();
    }
  }
  return Label(id);
}

Label CodeBuilder::newNamedLabel(const char* name, size_t nameLength, uint32_t type, uint32_t parentId) {
  uint32_t id = 0;
  if (!_lastError) {
    CBLabel* node = newNodeT<CBLabel>(id);
    if (ASMJIT_UNLIKELY(!node)) {
      setLastError(DebugUtils::errored(kErrorNoHeapMemory));
    }
    else {
      Error err = _code->newNamedLabelId(id, name, nameLength, type, parentId);
      if (ASMJIT_UNLIKELY(err))
        setLastError(err);
      else
        id = node->getId();
    }
  }
  return Label(id);
}

Error CodeBuilder::bind(const Label& label) {
  if (_lastError)
    return _lastError;

  CBLabel* node;
  Error err = getCBLabel(&node, label);
  if (ASMJIT_UNLIKELY(err))
    return setLastError(err);

  addNode(node);
  return kErrorOk;
}

Error CodeBuilder::align(uint32_t mode, uint32_t alignment) {
  if (_lastError)
    return _lastError;

  CBAlign* node = newAlignNode(mode, alignment);
  if (ASMJIT_UNLIKELY(!node))
    return setLastError(DebugUtils::errored(kErrorNoHeapMemory));

  addNode(node);
  return kErrorOk;
}

Error CodeBuilder::embed(const void* data, uint32_t size) {
  if (_lastError)
    return _lastError;

  CBData* node = newDataNode(data, size);
  if (ASMJIT_UNLIKELY(!node))
    return setLastError(DebugUtils::errored(kErrorNoHeapMemory));

  addNode(node);
  return kErrorOk;
}

Error CodeBuilder::embedLabel(const Label& label) {
  if (_lastError)
    return _lastError;

  CBLabelData* node = newNodeT<CBLabelData>(label.getId());
  if (ASMJIT_UNLIKELY(!node))
    return setLastError(DebugUtils::errored(kErrorNoHeapMemory));

  addNode(node);
  return kErrorOk;
}

Error CodeBuilder::embedConstPool(const Label& label, const ConstPool& pool) {
  if (_lastError)
    return _lastError;

  if (!isLabelValid(label))
    return setLastError(DebugUtils::errored(kErrorInvalidLabel));

  ASMJIT_PROPAGATE(align(kAlignData, static_cast<uint32_t>(pool.getAlignment())));
  ASMJIT_PROPAGATE(bind(label));

  CBData* node = newDataNode(nullptr, static_cast<uint32_t>(pool.getSize()));
  if (ASMJIT_UNLIKELY(!node))
    return setLastError(DebugUtils::errored(kErrorNoHeapMemory));

  pool.fill(node->getData());
  addNode(node);
  return kErrorOk;
}

Error CodeBuilder::comment(const char* s, size_t len) {
  if (_lastError)
    return _lastError;

  CBComment* node = newCommentNode(s, len);
  if (ASMJIT_UNLIKELY(!node))
    return setLastError(DebugUtils::errored(kErrorNoHeapMemory));

  addNode(node);
  return kErrorOk;
}

// ============================================================================
// [asmjit::CodeBuilder - Node-Management]
// ============================================================================

CBNode* CodeBuilder::addNode(CBNode* node) noexcept {
  ASMJIT_ASSERT(node);
  ASMJIT_ASSERT(node->getPrev() == nullptr);
  ASMJIT_ASSERT(node->getNext() == nullptr);

  if (!_cursor) {
    if (!_firstNode) {
      _firstNode = node;
      _lastNode = node;
    }
    else {
      node->_setNext(_firstNode);
      _firstNode->_setPrev(node);
      _firstNode = node;
    }
  }
  else {
    CBNode* prev = _cursor;
    CBNode* next = _cursor->getNext();

    node->_setPrev(prev);
    node->_setNext(next);

    prev->_setNext(node);
    if (next)
      next->_setPrev(node);
    else
      _lastNode = node;
  }

  _cursor = node;
  return node;
}

CBNode* CodeBuilder::addAfter(CBNode* node, CBNode* ref) noexcept {
  ASMJIT_ASSERT(node);
  ASMJIT_ASSERT(ref);

  ASMJIT_ASSERT(node->getPrev() == nullptr);
  ASMJIT_ASSERT(node->getNext() == nullptr);

  CBNode* prev = ref;
  CBNode* next = ref->getNext();

  node->_setPrev(prev);
  node->_setNext(next);

  prev->_setNext(node);
  if (next)
    next->_setPrev(node);
  else
    _lastNode = node;

  return node;
}

CBNode* CodeBuilder::addBefore(CBNode* node, CBNode* ref) noexcept {
  ASMJIT_ASSERT(node != nullptr);
  ASMJIT_ASSERT(node->getPrev() == nullptr);
  ASMJIT_ASSERT(node->getNext() == nullptr);
  ASMJIT_ASSERT(ref != nullptr);

  CBNode* prev = ref->getPrev();
  CBNode* next = ref;

  node->_setPrev(prev);
  node->_setNext(next);

  next->_setPrev(node);
  if (prev)
    prev->_setNext(node);
  else
    _firstNode = node;

  return node;
}

CBNode* CodeBuilder::removeNode(CBNode* node) noexcept {
  CBNode* prev = node->getPrev();
  CBNode* next = node->getNext();

  if (_firstNode == node)
    _firstNode = next;
  else
    prev->_setNext(next);

  if (_lastNode == node)
    _lastNode  = prev;
  else
    next->_setPrev(prev);

  node->_setPrev(nullptr);
  node->_setNext(nullptr);

  if (_cursor == node)
    _cursor = prev;

  return node;
}

void CodeBuilder::removeNodes(CBNode* first, CBNode* last) noexcept {
  if (first == last) {
    removeNode(first);
    return;
  }

  CBNode* prev = first->getPrev();
  CBNode* next = last->getNext();

  if (_firstNode == first)
    _firstNode = next;
  else
    prev->_setNext(next);

  if (_lastNode == last)
    _lastNode  = prev;
  else
    next->_setPrev(prev);

  CBNode* node = first;
  for (;;) {
    CBNode* next = node->getNext();
    ASMJIT_ASSERT(next != nullptr);

    node->_setPrev(nullptr);
    node->_setNext(nullptr);

    if (_cursor == node)
      _cursor = prev;

    if (node == last)
      break;
    node = next;
  }
}

CBNode* CodeBuilder::setCursor(CBNode* node) noexcept {
  CBNode* old = _cursor;
  _cursor = node;
  return old;
}

// ============================================================================
// [asmjit::CodeBuilder - Passes]
// ============================================================================

ASMJIT_FAVOR_SIZE CBPass* CodeBuilder::getPassByName(const char* name) const noexcept {
  for (size_t i = 0, len = _cbPasses.getLength(); i < len; i++) {
        CBPass* pass = _cbPasses[i];
    if (::strcmp(pass->getName(), name) == 0)
      return pass;
  }

  return nullptr;
}

ASMJIT_FAVOR_SIZE Error CodeBuilder::addPass(CBPass* pass) noexcept {
  if (ASMJIT_UNLIKELY(pass == nullptr)) {
    // Since this is directly called by `addPassT()` we treat `null` argument
    // as out-of-memory condition. Otherwise it would be API misuse.
    return DebugUtils::errored(kErrorNoHeapMemory);
  }
  else if (ASMJIT_UNLIKELY(pass->_cb)) {
    // Kind of weird, but okay...
    if (pass->_cb == this)
      return kErrorOk;
    return DebugUtils::errored(kErrorInvalidState);
  }

  ASMJIT_PROPAGATE(_cbPasses.append(&_cbHeap, pass));
  pass->_cb = this;
  return kErrorOk;
}

ASMJIT_FAVOR_SIZE Error CodeBuilder::deletePass(CBPass* pass) noexcept {
  if (ASMJIT_UNLIKELY(pass == nullptr))
    return DebugUtils::errored(kErrorInvalidArgument);

  if (pass->_cb != nullptr) {
    if (pass->_cb != this)
      return DebugUtils::errored(kErrorInvalidState);

    size_t index = _cbPasses.indexOf(pass);
    ASMJIT_ASSERT(index != Globals::kInvalidIndex);

    pass->_cb = nullptr;
    _cbPasses.removeAt(index);
  }

  pass->~CBPass();
  return kErrorOk;
}

// ============================================================================
// [asmjit::CodeBuilder - RunPasses]
// ============================================================================

Error CodeBuilder::runPasses() {
  Error err = _lastError;
  if (ASMJIT_UNLIKELY(err))
    return err;

  ZoneVector<CBPass*>& passes = _cbPasses;
  for (size_t i = 0, len = passes.getLength(); i < len; i++) {
    CBPass* pass = passes[i];

    _cbPassZone.reset();
    err = pass->run(&_cbPassZone);
    if (err) break;
  }

  _cbPassZone.reset();
  return err ? setLastError(err) : err;
}

// ============================================================================
// [asmjit::CodeBuilder - Serialize]
// ============================================================================

Error CodeBuilder::serialize(CodeEmitter* dst) {
  Error err = kErrorOk;
  CBNode* node_ = getFirstNode();

  do {
    dst->setInlineComment(node_->getInlineComment());

    switch (node_->getType()) {
      case CBNode::kNodeInst: {
OnInst:
        CBInst* node = node_->as<CBInst>();
        ASMJIT_ASSERT(node->getOpCapacity() >= 4);

        const Operand* opArray = node->getOpArray();
        uint32_t opCount = node->getOpCount();

        dst->setOptions(node->getOptions());
        dst->_opExtra = node->getOpExtra();

        if (opCount > 4) {
          dst->setOp4(opArray[4]);
          if (opCount == 6) dst->setOp5(opArray[5]);
        }

        err = dst->_emit(node->getInstId(), opArray[0], opArray[1], opArray[2], opArray[3]);
        break;
      }

      case CBNode::kNodeData: {
        CBData* node = node_->as<CBData>();
        err = dst->embed(node->getData(), node->getSize());
        break;
      }

      case CBNode::kNodeAlign: {
        CBAlign* node = node_->as<CBAlign>();
        err = dst->align(node->getMode(), node->getAlignment());
        break;
      }

      case CBNode::kNodeLabel: {
OnLabel:
        CBLabel* node = node_->as<CBLabel>();
        err = dst->bind(node->getLabel());
        break;
      }

      case CBNode::kNodeLabelData: {
        CBLabelData* node = node_->as<CBLabelData>();
        err = dst->embedLabel(node->getLabel());
        break;
      }

      case CBNode::kNodeConstPool: {
        CBConstPool* node = node_->as<CBConstPool>();
        err = dst->embedConstPool(node->getLabel(), node->getConstPool());
        break;
      }

      case CBNode::kNodeComment: {
        CBComment* node = node_->as<CBComment>();
        err = dst->comment(node->getInlineComment());
        break;
      }

      default:
        if (node_->actsAsInst()) goto OnInst;
        if (node_->actsAsLabel()) goto OnLabel;
        break;
    }

    if (err) break;
    node_ = node_->getNext();
  } while (node_);

  return err;
}

// ============================================================================
// [asmjit::CodeBuilder - Logging]
// ============================================================================

#if !defined(ASMJIT_DISABLE_LOGGING)
Error CodeBuilder::dump(StringBuilder& sb, uint32_t logOptions) const noexcept {
  CBNode* node = getFirstNode();
  while (node) {
    ASMJIT_PROPAGATE(Logging::formatNode(sb, logOptions, this, node));
    sb.appendChar('\n');

    node = node->getNext();
  }

  return kErrorOk;
}
#endif // !ASMJIT_DISABLE_LOGGING

// ============================================================================
// [asmjit::CBPass - Construction / Destruction]
// ============================================================================

CBPass::CBPass(const char* name) noexcept
  : _cb(nullptr),
    _name(name) {}
CBPass::~CBPass() noexcept {}

} // asmjit namespace

// [Api-End]
#include "../asmjit_apiend.h"

// [Guard]
#endif // !ASMJIT_DISABLE_BUILDER
