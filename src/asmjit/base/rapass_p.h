// [AsmJit]
// Complete x86/x64 JIT and Remote Assembler for C++.
//
// [License]
// Zlib - See LICENSE.md file in the package.

// [Guard]
#ifndef _ASMJIT_BASE_RAPASS_P_H
#define _ASMJIT_BASE_RAPASS_P_H

#include "../asmjit_build.h"
#if !defined(ASMJIT_DISABLE_COMPILER)

// [Dependencies]
#include "../base/codecompiler.h"
#include "../base/zone.h"

// [Api-Begin]
#include "../asmjit_apibegin.h"

#if !defined(ASMJIT_DISABLE_LOGGING)
# define ASMJIT_RA_LOG_INIT(LOGGER) \
  Logger* logger = LOGGER;
# define ASMJIT_RA_LOG_FORMAT(...)  \
  do {                              \
    if (logger)                     \
      logger->logf(__VA_ARGS__);    \
  } while (0)
# define ASMJIT_RA_LOG_COMPLEX(...) \
  do {                              \
    if (logger) {                   \
      __VA_ARGS__                   \
    }                               \
  } while (0)
#else
# define ASMJIT_RA_LOG_INIT(LOGGER) ASMJIT_NOP
# define ASMJIT_RA_LOG_FORMAT(...) ASMJIT_NOP
# define ASMJIT_RA_LOG_COMPLEX(...) ASMJIT_NOP
#endif

namespace asmjit {

//! \addtogroup asmjit_base
//! \{

// ============================================================================
// [Forward Declarations]
// ============================================================================

class RAPass;
class RALoop;
class RABlock;

typedef ZoneVector<RALoop*> RALoops;
typedef ZoneVector<RABlock*> RABlocks;
typedef ZoneVector<WorkReg*> WorkRegs;

// ============================================================================
// [asmjit::RARegStats]
// ============================================================================

//! Information associated with each instruction, propagated to blocks, loops,
//! and the whole function. This information cane be used to do some decision
//! before the register allocator tries to do its job. For example to use fast
//! register allocation inside a block or loop it cannot have clobbered and/or
//! precolored registers, etc...
struct RARegStats {
  ASMJIT_ENUM(Index) {
    kIndexPrecolored = 0,
    kIndexClobbered  = 8,
    kIndexUsed       = 16
  };

  ASMJIT_ENUM(Mask) {
    kMaskPrecolored  = 0xFF << kIndexPrecolored,
    kMaskClobbered   = 0xFF << kIndexClobbered,
    kMaskUsed        = 0xFF << kIndexUsed
  };

  ASMJIT_INLINE void reset() noexcept { _packed = 0; }
  ASMJIT_INLINE void combineWith(const RARegStats& other) noexcept { _packed |= other._packed; }

  ASMJIT_INLINE bool hasClobbered() const noexcept { return (_packed & kMaskClobbered) != 0U; }
  ASMJIT_INLINE bool hasClobbered(uint32_t kind) const noexcept { return (_packed & Utils::mask(kIndexClobbered + kind)) != 0; }
  ASMJIT_INLINE void makeClobbered(uint32_t kind) noexcept { _packed |= Utils::mask(kIndexClobbered + kind); }

  ASMJIT_INLINE bool hasPrecolored() const noexcept { return (_packed & kMaskPrecolored) != 0U; }
  ASMJIT_INLINE bool hasPrecolored(uint32_t kind) const noexcept { return (_packed & Utils::mask(kIndexPrecolored + kind)) != 0; }
  ASMJIT_INLINE void makePrecolored(uint32_t kind) noexcept { _packed |= Utils::mask(kIndexPrecolored + kind); }

  ASMJIT_INLINE bool hasUsed() const noexcept { return (_packed & kMaskUsed) != 0U; }
  ASMJIT_INLINE bool hasUsed(uint32_t kind) const noexcept { return (_packed & Utils::mask(kIndexUsed + kind)) != 0; }
  ASMJIT_INLINE void makeUsed(uint32_t kind) noexcept { _packed |= Utils::mask(kIndexUsed + kind); }

  uint32_t _packed;
};

// ============================================================================
// [asmjit::RARegCount]
// ============================================================================

//! Registers count.
struct RARegCount {
  // --------------------------------------------------------------------------
  // [Reset]
  // --------------------------------------------------------------------------

  //! Reset all counters to zero.
  ASMJIT_INLINE void reset() noexcept { _packed = 0; }

  // --------------------------------------------------------------------------
  // [Accessors]
  // --------------------------------------------------------------------------

  //! Get register count by a register `kind`.
  ASMJIT_INLINE uint32_t get(uint32_t kind) const noexcept {
    ASMJIT_ASSERT(kind < Globals::kMaxVRegKinds);

    uint32_t shift = Utils::byteShiftOfDWordStruct(kind);
    return (_packed >> shift) & static_cast<uint32_t>(0xFF);
  }

  //! Set register count by a register `kind`.
  ASMJIT_INLINE void set(uint32_t kind, uint32_t n) noexcept {
    ASMJIT_ASSERT(kind < Globals::kMaxVRegKinds);
    ASMJIT_ASSERT(n <= 0xFF);

    uint32_t shift = Utils::byteShiftOfDWordStruct(kind);
    _packed = (_packed & ~static_cast<uint32_t>(0xFF << shift)) + (n << shift);
  }

  //! Add register count by a register `kind`.
  ASMJIT_INLINE void add(uint32_t kind, uint32_t n = 1) noexcept {
    ASMJIT_ASSERT(kind < Globals::kMaxVRegKinds);
    ASMJIT_ASSERT(0xFF - static_cast<uint32_t>(_regs[kind]) >= n);

    uint32_t shift = Utils::byteShiftOfDWordStruct(kind);
    _packed += n << shift;
  }

  // --------------------------------------------------------------------------
  // [Misc]
  // --------------------------------------------------------------------------

  //! Build register indexes based on the given `count` of registers.
  ASMJIT_INLINE void indexFromRegCount(const RARegCount& count) noexcept {
    uint32_t x = static_cast<uint32_t>(count._regs[0]);
    uint32_t y = static_cast<uint32_t>(count._regs[1]) + x;
    uint32_t z = static_cast<uint32_t>(count._regs[2]) + y;

    ASMJIT_ASSERT(y <= 0xFF);
    ASMJIT_ASSERT(z <= 0xFF);
    _packed = Utils::pack32_4x8(0, x, y, z);
  }

  // --------------------------------------------------------------------------
  // [Members]
  // --------------------------------------------------------------------------

  union {
    uint8_t _regs[4];
    uint32_t _packed;
  };
};

// ============================================================================
// [asmjit::RARegMask]
// ============================================================================

//! Registers mask.
struct RARegMask {
  // --------------------------------------------------------------------------
  // [Reset]
  // --------------------------------------------------------------------------

  //! Reset all register masks to zero.
  ASMJIT_INLINE void reset() noexcept {
    for (uint32_t i = 0; i < Globals::kMaxVRegKinds; i++)
      _masks[i] = 0;
  }

  ASMJIT_INLINE void reset(uint32_t kind) noexcept {
    ASMJIT_ASSERT(kind < Globals::kMaxVRegKinds);
    _masks[kind] = 0;
  }

  // --------------------------------------------------------------------------
  // [IsEmpty / Has]
  // --------------------------------------------------------------------------

  //! Get whether all register masks are zero (empty).
  ASMJIT_INLINE bool isEmpty() const noexcept {
    uint32_t m = 0;
    for (uint32_t i = 0; i < Globals::kMaxVRegKinds; i++)
      m |= _masks[i];
    return m == 0;
  }

  ASMJIT_INLINE bool has(uint32_t kind, uint32_t mask = 0xFFFFFFFFU) const noexcept {
    ASMJIT_ASSERT(kind < Globals::kMaxVRegKinds);
    return (_masks[kind] & mask) != 0;
  }

  // --------------------------------------------------------------------------
  // [Accessors]
  // --------------------------------------------------------------------------

  ASMJIT_INLINE uint32_t get(uint32_t kind) const noexcept {
    ASMJIT_ASSERT(kind < Globals::kMaxVRegKinds);
    return _masks[kind];
  }

  ASMJIT_INLINE void set(const RARegMask& other) noexcept {
    for (uint32_t i = 0; i < Globals::kMaxVRegKinds; i++)
      _masks[i] = other._masks[i];
  }

  ASMJIT_INLINE void set(uint32_t kind, uint32_t mask) noexcept {
    ASMJIT_ASSERT(kind < Globals::kMaxVRegKinds);
    _masks[kind] = mask;
  }

  ASMJIT_INLINE void and_(const RARegMask& other) noexcept {
    for (uint32_t i = 0; i < Globals::kMaxVRegKinds; i++)
      _masks[i] &= other._masks[i];
  }

  ASMJIT_INLINE void and_(uint32_t kind, uint32_t mask) noexcept {
    ASMJIT_ASSERT(kind < Globals::kMaxVRegKinds);
    _masks[kind] &= mask;
  }

  ASMJIT_INLINE void andNot(const RARegMask& other) noexcept {
    for (uint32_t i = 0; i < Globals::kMaxVRegKinds; i++)
      _masks[i] &= ~other._masks[i];
  }

  ASMJIT_INLINE void andNot(uint32_t kind, uint32_t mask) noexcept {
    ASMJIT_ASSERT(kind < Globals::kMaxVRegKinds);
    _masks[kind] &= ~mask;
  }

  ASMJIT_INLINE void or_(const RARegMask& other) noexcept {
    for (uint32_t i = 0; i < Globals::kMaxVRegKinds; i++)
      _masks[i] |= other._masks[i];
  }

  ASMJIT_INLINE void or_(uint32_t kind, uint32_t mask) noexcept {
    ASMJIT_ASSERT(kind < Globals::kMaxVRegKinds);
    _masks[kind] |= mask;
  }

  ASMJIT_INLINE void xor_(const RARegMask& other) noexcept {
    for (uint32_t i = 0; i < Globals::kMaxVRegKinds; i++)
      _masks[i] ^= other._masks[i];
  }

  ASMJIT_INLINE void xor_(uint32_t kind, uint32_t mask) noexcept {
    ASMJIT_ASSERT(kind < Globals::kMaxVRegKinds);
    _masks[kind] ^= mask;
  }

  // --------------------------------------------------------------------------
  // [Members]
  // --------------------------------------------------------------------------

  uint32_t _masks[Globals::kMaxVRegKinds];
};

// ============================================================================
// [asmjit::LiveBits]
// ============================================================================

typedef ZoneBitVector LiveBits;

// ============================================================================
// [asmjit::LiveSpan]
// ============================================================================

class LiveSpan {
public:
  ASMJIT_INLINE LiveSpan() noexcept : a(0), b(0) {}
  ASMJIT_INLINE LiveSpan(const LiveSpan& other) noexcept : a(other.a), b(other.b) {}
  ASMJIT_INLINE LiveSpan(uint32_t a, uint32_t b) noexcept : a(a), b(b) {}

  uint32_t a, b;
};

// ============================================================================
// [asmjit::LiveRange]
// ============================================================================

class LiveRange {
public:
  ASMJIT_NONCOPYABLE(LiveRange)

  // --------------------------------------------------------------------------
  // [Construction / Destruction]
  // --------------------------------------------------------------------------

  explicit ASMJIT_INLINE LiveRange() noexcept : _spans() {}

  // --------------------------------------------------------------------------
  // [Reset]
  // --------------------------------------------------------------------------

  ASMJIT_INLINE void reset() noexcept {
    _spans.reset();
  }

  // --------------------------------------------------------------------------
  // [Interface]
  // --------------------------------------------------------------------------

  ASMJIT_INLINE bool isEmpty() const noexcept { return _spans.isEmpty(); }
  ASMJIT_INLINE size_t getLength() const noexcept { return _spans.getLength(); }

  // --------------------------------------------------------------------------
  // [Members]
  // --------------------------------------------------------------------------

  ZoneVector<LiveSpan> _spans;
};

// ============================================================================
// [asmjit::RAStackSlot]
// ============================================================================

//! Stack slot.
struct RAStackSlot {
  RAStackSlot* next;                     //!< Next active cell.
  int32_t offset;                        //!< Cell offset, relative to base-offset.
  uint32_t size;                         //!< Cell size.
  uint32_t alignment;                    //!< Cell alignment.
};

// ============================================================================
// [asmjit::RAStackManager]
// ============================================================================

//! Stack management.
struct RAStackManager {
  enum Size {
    kSize1     = 0,
    kSize2     = 1,
    kSize4     = 2,
    kSize8     = 3,
    kSize16    = 4,
    kSize32    = 5,
    kSize64    = 6,
    kSizeStack = 7,
    kSizeCount = 8
  };

  // --------------------------------------------------------------------------
  // [Reset]
  // --------------------------------------------------------------------------

  ASMJIT_INLINE void reset() noexcept { ::memset(this, 0, sizeof(*this)); }

  // --------------------------------------------------------------------------
  // [Members]
  // --------------------------------------------------------------------------

  uint32_t _bytesUsed;                   //!< Count of bytes used.
  uint32_t _alignment;                   //!< Calculated alignment.
  uint32_t _usageCount[kSizeCount];      //!< Number of used cells by size.

  RAStackSlot* _homeList;                //!< Spill slots of `VirtReg`s.
  RAStackSlot* _stackList;               //!< Stack slots used by the function.
};

// ============================================================================
// [asmjit::RABlock]
// ============================================================================

class RABlock {
public:
  ASMJIT_NONCOPYABLE(RABlock)

  ASMJIT_ENUM(LiveType) {
    kLiveIn    = 0,
    kLiveOut   = 1,
    kLiveGen   = 2,
    kLiveKill  = 3,
    kLiveCount = 4
  };

  ASMJIT_ENUM(Flags) {
    kFlagIsConstructed    = 0x00000001U, //!< Block has been constructed from nodes.
    kFlagIsSinglePass     = 0x00000002U, //!< Executed only once (initialization code).
    kFlagHasLiveness      = 0x00000004U, //!< Used during liveness analysis.
    kFlagHasFixedRegs     = 0x00000010U, //!< Block contains fixed registers (precolored).
    kFlagHasFuncCalls     = 0x00000020U  //!< Block contains function calls.
  };

  // --------------------------------------------------------------------------
  // [Construction / Destruction]
  // --------------------------------------------------------------------------

  ASMJIT_INLINE RABlock(RAPass* ra, uint32_t blockId = 0) noexcept
    : _ra(ra),
      _blockId(blockId),
      _flags(0),
      _first(nullptr),
      _last(nullptr),
      _weight(0),
      _povOrder(0xFFFFFFFFU),
      _regStats(),
      _timestamp(0),
      _loop(nullptr),
      _idom(nullptr),
      _predecessors(),
      _successors() {

    _liveBits[kLiveIn  ].reset();
    _liveBits[kLiveOut ].reset();
    _liveBits[kLiveGen ].reset();
    _liveBits[kLiveKill].reset();
  }

  // --------------------------------------------------------------------------
  // [Accessors]
  // --------------------------------------------------------------------------

  ASMJIT_INLINE RAPass* getRA() const noexcept { return _ra; }
  ASMJIT_INLINE ZoneHeap* getHeap() const noexcept;

  ASMJIT_INLINE uint32_t getBlockId() const noexcept { return _blockId; }
  ASMJIT_INLINE uint32_t getFlags() const noexcept { return _flags; }

  ASMJIT_INLINE bool hasFlag(uint32_t flag) const noexcept { return (_flags & flag) != 0; }
  ASMJIT_INLINE void addFlags(uint32_t flags) noexcept { _flags |= flags; }

  ASMJIT_INLINE bool isConstructed() const noexcept { return hasFlag(kFlagIsConstructed); }
  ASMJIT_INLINE void makeConstructed(const RARegStats& regStats) noexcept {
    _flags |= kFlagIsConstructed;
    _regStats.combineWith(regStats);
  }

  ASMJIT_INLINE const RARegStats& getRegStats() const noexcept { return _regStats; }

  ASMJIT_INLINE bool isSinglePass() const noexcept { return hasFlag(kFlagIsSinglePass); }
  ASMJIT_INLINE bool isEntryBlock() const noexcept { return _predecessors.isEmpty(); }
  ASMJIT_INLINE bool isExitBlock() const noexcept { return _successors.isEmpty(); }

  ASMJIT_INLINE bool hasPredecessors() const noexcept { return !_predecessors.isEmpty(); }
  ASMJIT_INLINE bool hasSuccessors() const noexcept { return !_successors.isEmpty(); }

  ASMJIT_INLINE const RABlocks& getPredecessors() const noexcept { return _predecessors; }
  ASMJIT_INLINE const RABlocks& getSuccessors() const noexcept { return _successors; }

  ASMJIT_INLINE CBNode* getFirst() const noexcept { return _first; }
  ASMJIT_INLINE void setFirst(CBNode* node) noexcept { _first = node; }

  ASMJIT_INLINE CBNode* getLast() const noexcept { return _last; }
  ASMJIT_INLINE void setLast(CBNode* node) noexcept { _last = node; }

  ASMJIT_INLINE uint32_t getPovOrder() const noexcept { return _povOrder; }
  ASMJIT_INLINE uint64_t getTimestamp() const noexcept { return _timestamp; }
  ASMJIT_INLINE void setTimestamp(uint64_t mark) const noexcept { _timestamp = mark; }

  ASMJIT_INLINE bool hasIDom() const noexcept { return _idom != nullptr; }
  ASMJIT_INLINE RABlock* getIDom() noexcept { return _idom; }
  ASMJIT_INLINE const RABlock* getIDom() const noexcept { return _idom; }
  ASMJIT_INLINE void setIDom(RABlock* block) noexcept { _idom = block; }

  ASMJIT_INLINE LiveBits& getIn() noexcept { return _liveBits[kLiveIn]; }
  ASMJIT_INLINE const LiveBits& getIn() const noexcept { return _liveBits[kLiveIn]; }

  ASMJIT_INLINE LiveBits& getOut() noexcept { return _liveBits[kLiveOut]; }
  ASMJIT_INLINE const LiveBits& getOut() const noexcept { return _liveBits[kLiveOut]; }

  ASMJIT_INLINE LiveBits& getGen() noexcept { return _liveBits[kLiveGen]; }
  ASMJIT_INLINE const LiveBits& getGen() const noexcept { return _liveBits[kLiveGen]; }

  ASMJIT_INLINE LiveBits& getKill() noexcept { return _liveBits[kLiveKill]; }
  ASMJIT_INLINE const LiveBits& getKill() const noexcept { return _liveBits[kLiveKill]; }

  ASMJIT_INLINE Error resizeLiveBits(size_t size) noexcept {
    ZoneHeap* heap = getHeap();
    ASMJIT_PROPAGATE(_liveBits[kLiveIn  ].resize(heap, size));
    ASMJIT_PROPAGATE(_liveBits[kLiveOut ].resize(heap, size));
    ASMJIT_PROPAGATE(_liveBits[kLiveGen ].resize(heap, size));
    ASMJIT_PROPAGATE(_liveBits[kLiveKill].resize(heap, size));
    return kErrorOk;
  }

  // --------------------------------------------------------------------------
  // [Ops]
  // --------------------------------------------------------------------------

  //! Adds a successor to this block, and predecessor to `successor`, making
  //! connection on both sides.
  //!
  //! This API must be used to manage successors and predecessors, never manage
  //! it manually.
  Error appendSuccessor(RABlock* successor) noexcept;

  //! Similar to `appendSuccessor()`, but prepends it instead of appending it.
  //!
  //! This function is used to add a successor after a conditional jump
  //! destination has been added.
  Error prependSuccessor(RABlock* successor) noexcept;

  // --------------------------------------------------------------------------
  // [Members]
  // --------------------------------------------------------------------------

  RAPass* _ra;                           //!< Register allocator pass.
  uint32_t _blockId;                     //!< Block id (indexed from zero).
  uint32_t _flags;                       //!< Block flags, see \ref Flags.

  CBNode* _first;                        //!< First `CBNode` of this block (inclusive).
  CBNode* _last;                         //!< Last `CBNode` of this block (inclusive).

  uint32_t _weight;                      //!< Weight of this block (default 0, each loop adds one).
  uint32_t _povOrder;                    //!< Post-order view order, used during POV construction.
  RARegStats _regStats;                  //!< Basic statistics about registers.

  mutable uint64_t _timestamp;           //!< Timestamp (used by visitors).
  RALoop* _loop;                         //!< Inner-most loop of this block.
  RABlock* _idom;                        //!< Immediate dominator of this block.

  RABlocks _predecessors;                //!< Block predecessors.
  RABlocks _successors;                  //!< Block successors.

  LiveBits _liveBits[kLiveCount];        //!< Liveness in/out/use/kill.
};

// ============================================================================
// [asmjit::RALoop]
// ============================================================================

class RALoop {
public:
  enum Flags {
    kFlagHasNested        = 0x00000001U  //!< Has nested loops.
  };

  // --------------------------------------------------------------------------
  // [Construction / Destruction]
  // --------------------------------------------------------------------------

  ASMJIT_INLINE RALoop(RAPass* ra, uint32_t loopId) noexcept
    : _ra(ra),
      _loopId(loopId),
      _flags(0) {}

  // --------------------------------------------------------------------------
  // [Accessors]
  // --------------------------------------------------------------------------

  ASMJIT_INLINE RAPass* getRA() const noexcept { return _ra; }
  ASMJIT_INLINE ZoneHeap* getHeap() const noexcept;

  ASMJIT_INLINE uint32_t getLoopId() const noexcept { return _loopId; }
  ASMJIT_INLINE uint32_t getFlags() const noexcept { return _flags; }

  // --------------------------------------------------------------------------
  // [Members]
  // --------------------------------------------------------------------------

  RAPass* _ra;                           //!< Register allocator pass.
  uint32_t _loopId;                      //!< Loop id (indexed from zero).
  uint32_t _flags;                       //!< Loop flags.

  RALoop* _parent;                       //!< Parent loop or null.
};

// ============================================================================
// [asmjit::WorkReg]
// ============================================================================

class WorkReg {
public:
  ASMJIT_NONCOPYABLE(WorkReg)

  // --------------------------------------------------------------------------
  // [Construction / Destruction]
  // --------------------------------------------------------------------------

  ASMJIT_INLINE WorkReg(ZoneHeap* heap, VirtReg* vReg, uint32_t workId) noexcept
    : _workId(workId),
      _virtId(vReg->getId()),
      _kind(vReg->getKind()),
      _virtReg(vReg),
      _liveIn(),
      _liveOut(),
      _liveRange(),
      _refs() {}

  // --------------------------------------------------------------------------
  // [Accessors]
  // --------------------------------------------------------------------------

  ASMJIT_INLINE uint32_t getWorkId() const noexcept { return _workId; }
  ASMJIT_INLINE uint32_t getVirtId() const noexcept { return _virtId; }
  ASMJIT_INLINE uint32_t getKind() const noexcept { return _kind; }

  ASMJIT_INLINE VirtReg* getVirtReg() const noexcept { return _virtReg; }

  ASMJIT_INLINE LiveBits& getLiveIn() noexcept { return _liveIn; }
  ASMJIT_INLINE const LiveBits& getLiveIn() const noexcept { return _liveIn; }

  ASMJIT_INLINE LiveBits& getLiveOut() noexcept { return _liveOut; }
  ASMJIT_INLINE const LiveBits& getLiveOut() const noexcept { return _liveOut; }

  ASMJIT_INLINE LiveRange& getLiveRange() noexcept { return _liveRange; }
  ASMJIT_INLINE const LiveRange& getLiveRange() const noexcept { return _liveRange; }

  // --------------------------------------------------------------------------
  // [Members]
  // --------------------------------------------------------------------------

  uint32_t _workId;                      //!< Work id, used during register allocation.
  uint32_t _virtId;                      //!< Virtual id as used by `VirtReg`.

  uint8_t _kind;                         //!< Register kind.

  VirtReg* _virtReg;                     //!< `VirtReg` associated with this `WorkReg`.

  LiveBits _liveIn;                      //!< Live-in bits, each bit per node-id.
  LiveBits _liveOut;                     //!< Live-out bits, each bit per node-id.
  LiveRange _liveRange;                  //!< Live range of the `VirtReg`.
  ZoneVector<CBNode*> _refs;             //!< All nodes that use this `VirtReg`.
};

// ============================================================================
// [asmjit::TiedReg]
// ============================================================================

//! Tied register (CodeCompiler).
//!
//! Tied register is used to describe one ore more register operands that share
//! the same virtual register. Tied register contains all the data that is
//! essential for register allocation.
struct TiedReg {
  //! Flags.
  ASMJIT_ENUM(Flags) {
    kRReg        = 0x00000001U,          //!< Register read.
    kWReg        = 0x00000002U,          //!< Register write.
    kXReg        = 0x00000003U,          //!< Register read-write.

    kRMem        = 0x00000004U,          //!< Can be replaced by memory read.
    kWMem        = 0x00000008U,          //!< Can be replaced by memory write.
    kXMem        = 0x0000000CU,          //!< Can be replaced by memory read-write.

    kRFunc       = 0x00000010U,          //!< Function argument passed in register.
    kWFunc       = 0x00000020U,          //!< Function return value passed into register.
    kXFunc       = 0x00000030U,          //!< Function argument and return value.

    kWExclusive  = 0x00000080U           //!< Has an exclusive write operand.
  };

  // --------------------------------------------------------------------------
  // [Init / Reset]
  // --------------------------------------------------------------------------

  ASMJIT_INLINE void init(VirtReg* vReg, uint32_t flags, uint32_t allocableRegs, uint32_t rPhysId, uint32_t wPhysId) noexcept {
    this->vReg = vReg;
    this->flags = flags;
    this->allocableRegs = allocableRegs;
    this->refCount = 1;
    this->rPhysId = static_cast<uint8_t>(rPhysId);
    this->wPhysId = static_cast<uint8_t>(wPhysId);
    this->reserved = 0;
  }

  // --------------------------------------------------------------------------
  // [Accessors]
  // --------------------------------------------------------------------------

  //! Get allocation flags, see \ref Flags.
  ASMJIT_INLINE uint32_t getFlags() const noexcept { return flags; }

  ASMJIT_INLINE bool isReadOnly() const noexcept { return (flags & kXReg) == kRReg; }
  ASMJIT_INLINE bool isWriteOnly() const noexcept { return (flags & kXReg) == kWReg; }
  ASMJIT_INLINE bool isReadWrite() const noexcept { return (flags & kXReg) == kXReg; }

  //! Get whether the variable has to be allocated in a specific input register.
  ASMJIT_INLINE uint32_t hasRPhysId() const noexcept { return rPhysId != Globals::kInvalidRegId; }
  //! Get whether the variable has to be allocated in a specific output register.
  ASMJIT_INLINE uint32_t hasWPhysId() const noexcept { return wPhysId != Globals::kInvalidRegId; }

  //! Set the input register index.
  ASMJIT_INLINE void setRPhysId(uint32_t index) noexcept { rPhysId = static_cast<uint8_t>(index); }
  //! Set the output register index.
  ASMJIT_INLINE void setWPhysId(uint32_t index) noexcept { wPhysId = static_cast<uint8_t>(index); }

  // --------------------------------------------------------------------------
  // [Operator Overload]
  // --------------------------------------------------------------------------

  ASMJIT_INLINE TiedReg& operator=(const TiedReg& other) noexcept {
    ::memcpy(this, &other, sizeof(TiedReg));
    return *this;
  }

  // --------------------------------------------------------------------------
  // [Members]
  // --------------------------------------------------------------------------

  //! Pointer to the associated \ref VirtReg.
  VirtReg* vReg;

  //! Allocation flags.
  uint32_t flags;

  //! Allocable input registers.
  //!
  //! Optional input registers is a mask of all allocable registers for a given
  //! variable where we have to pick one of them. This mask is usually not used
  //! when _inRegs is set. If both masks are used then the register
  //! allocator tries first to find an intersection between these and allocates
  //! an extra slot if not found.
  uint32_t allocableRegs;

  union {
    struct {
      //! How many times the variable is referenced by the instruction / node.
      uint8_t refCount;
      //! Input register id or `Globals::kInvalidRegId` if it's not given.
      //!
      //! Even if the input register id is not given (i.e. it may by any
      //! register), register allocator should assign some id that will be
      //! used to persist a virtual register into this specific id. It's
      //! helpful in situations where one virtual register has to be allocated
      //! in multiple registers to determine the register which will be persistent.
      uint8_t rPhysId;
      //! Output register index or `Globals::kInvalidRegId` if it's not given.
      //!
      //! Typically `Globals::kInvalidRegId` if variable is only used on input.
      uint8_t wPhysId;
      //! \internal
      uint8_t reserved;
    };

    //! \internal
    //!
    //! Packed data #0.
    uint32_t packed;
  };
};

// ============================================================================
// [asmjit::RAData]
// ============================================================================

//! Register allocator's data associated with each \ref CBNode.
struct RAData {
  ASMJIT_INLINE RAData(uint32_t tiedTotal) noexcept {
    this->tiedTotal = tiedTotal;
    this->inRegs.reset();
    this->outRegs.reset();
    this->clobberedRegs.reset();
    this->tiedIndex.reset();
    this->tiedCount.reset();
  }

  // --------------------------------------------------------------------------
  // [Accessors]
  // --------------------------------------------------------------------------

  //! Get `TiedReg` array.
  ASMJIT_INLINE TiedReg* getTiedArray() const noexcept {
    return const_cast<TiedReg*>(tiedArray);
  }

  //! Get `TiedReg` array for a given register `kind`.
  ASMJIT_INLINE TiedReg* getTiedArrayByKind(uint32_t kind) const noexcept {
    return const_cast<TiedReg*>(tiedArray) + tiedIndex.get(kind);
  }

  //! Get `TiedReg` index for a given register `kind`.
  ASMJIT_INLINE uint32_t getTiedStart(uint32_t kind) const noexcept {
    return tiedIndex.get(kind);
  }

  //! Get count of all tied registers.
  ASMJIT_INLINE uint32_t getTiedCount() const noexcept {
    return tiedTotal;
  }

  //! Get count of tied registers of a given `kind`.
  ASMJIT_INLINE uint32_t getTiedCountByKind(uint32_t kind) const noexcept {
    return tiedCount.get(kind);
  }

  //! Get `TiedReg` at the specified `index`.
  ASMJIT_INLINE TiedReg* getTiedAt(uint32_t index) const noexcept {
    ASMJIT_ASSERT(index < tiedTotal);
    return getTiedArray() + index;
  }

  //! Get TiedReg at the specified index for a given register `kind`.
  ASMJIT_INLINE TiedReg* getTiedAtByKind(uint32_t kind, uint32_t index) const noexcept {
    ASMJIT_ASSERT(index < tiedCount._regs[kind]);
    return getTiedArrayByKind(kind) + index;
  }

  ASMJIT_INLINE void setTiedAt(uint32_t index, TiedReg& tied) noexcept {
    ASMJIT_ASSERT(index < tiedTotal);
    tiedArray[index] = tied;
  }

  // --------------------------------------------------------------------------
  // [Utils]
  // --------------------------------------------------------------------------

  //! Find TiedReg.
  ASMJIT_INLINE TiedReg* findTied(VirtReg* vReg) const noexcept {
    TiedReg* tiedArray = getTiedArray();
    uint32_t tiedCount = tiedTotal;

    for (uint32_t i = 0; i < tiedCount; i++)
      if (tiedArray[i].vReg == vReg)
        return &tiedArray[i];

    return nullptr;
  }

  //! Find TiedReg (by class).
  ASMJIT_INLINE TiedReg* findTiedByKind(uint32_t kind, VirtReg* vReg) const noexcept {
    TiedReg* tiedArray = getTiedArrayByKind(kind);
    uint32_t tiedCount = getTiedCountByKind(kind);

    for (uint32_t i = 0; i < tiedCount; i++)
      if (tiedArray[i].vReg == vReg)
        return &tiedArray[i];

    return nullptr;
  }

  // --------------------------------------------------------------------------
  // [Members]
  // --------------------------------------------------------------------------

  uint32_t tiedTotal;                    //!< Total count of \ref TiedReg regs.

  //! Special registers on input.
  //!
  //! Special register(s) restricted to one or more physical register. If there
  //! is more than one special register it means that we have to duplicate the
  //! variable content to all of them (it means that the same variable was used
  //! by two or more operands). We forget about duplicates after the register
  //! allocation finishes and marks all duplicates as non-assigned.
  RARegMask inRegs;

  //! Special registers on output.
  //!
  //! Special register(s) used on output. Each variable can have only one
  //! special register on the output, 'RAData' contains all registers from
  //! all 'TiedReg's.
  RARegMask outRegs;

  //! Clobbered registers (by a function call).
  RARegMask clobberedRegs;

  //! Start indexes of `TiedReg`s per register kind.
  RARegCount tiedIndex;
  //! Count of variables per register kind.
  RARegCount tiedCount;

  //! Linked registers.
  TiedReg tiedArray[1];
};

// ============================================================================
// [asmjit::RAState]
// ============================================================================

//! Variables' state.
struct RAState {
  //! Cell.
  struct Cell {
    ASMJIT_INLINE void reset() noexcept { _state = 0; }

    ASMJIT_INLINE uint32_t getState() const noexcept { return _state; }
    ASMJIT_INLINE void setState(uint32_t state) noexcept { _state = static_cast<uint8_t>(state); }

    uint8_t _state;
  };

  // --------------------------------------------------------------------------
  // [Reset]
  // --------------------------------------------------------------------------

  ASMJIT_INLINE void reset(size_t numCells) noexcept {
    ::memset(this, 0, sizeof(_allocatedRegs) + sizeof(_allocatedMask) + numCells * sizeof(Cell));
  }

  // --------------------------------------------------------------------------
  // [Accessors]
  // --------------------------------------------------------------------------

  ASMJIT_INLINE VirtReg** getAllocatedRegs() noexcept { return _allocatedRegs; }
  ASMJIT_INLINE VirtReg* const* getAllocatedRegs() const noexcept { return _allocatedRegs; }

  ASMJIT_INLINE RARegMask& getAllocatedMask() noexcept { return _allocatedMask; }
  ASMJIT_INLINE const RARegMask& getAllocatedMask() const noexcept { return _allocatedMask; }

  // --------------------------------------------------------------------------
  // [Members]
  // --------------------------------------------------------------------------

  //! Allocated registers array.
  VirtReg* _allocatedRegs[Globals::kMaxPhysRegs];

  //! Allocated registers mask.
  RARegMask _allocatedMask;

  //! Variables data, the length is stored in `X86RAPass`.
  Cell _cells[1];
};

// ============================================================================
// [asmjit::RAPass]
// ============================================================================

//! \internal
//!
//! Register allocation pass (abstract) used by \ref CodeCompiler.
class RAPass : public CCFuncPass {
public:
  ASMJIT_NONCOPYABLE(RAPass)
  typedef CCFuncPass Base;

  enum Limits {
    kMaxVRegKinds = Globals::kMaxVRegKinds
  };

  // Shortcuts...
  enum {
    kAnyReg = Globals::kInvalidRegId
  };

  // --------------------------------------------------------------------------
  // [Construction / Destruction]
  // --------------------------------------------------------------------------

  RAPass() noexcept;
  virtual ~RAPass() noexcept;

  // --------------------------------------------------------------------------
  // [Accessors]
  // --------------------------------------------------------------------------

  //! Get the associated `CodeCompiler`.
  ASMJIT_INLINE CodeCompiler* cc() const noexcept { return static_cast<CodeCompiler*>(_cb); }

  //! Get if the logging is enabled, in that case `getLogger()` returns a valid `Logger` instance.
  ASMJIT_INLINE bool hasLogger() const noexcept { return _logger != nullptr; }
  //! Get `Logger` instance or null.
  ASMJIT_INLINE Logger* getLogger() const noexcept { return _logger; }

  //! Get `Zone` passed to `runOnFunction()`.
  ASMJIT_INLINE Zone* getZone() const { return _heap.getZone(); }
  //! Get `ZoneHeap` used by the register allocator.
  ASMJIT_INLINE ZoneHeap* getHeap() const { return const_cast<ZoneHeap*>(&_heap); }

  //! Get function node.
  ASMJIT_INLINE CCFunc* getFunc() const noexcept { return _func; }
  //! Get stop node.
  ASMJIT_INLINE CBNode* getStop() const noexcept { return _stop; }

  //! Get extra block.
  ASMJIT_INLINE CBNode* getExtraBlock() const noexcept { return _extraBlock; }
  //! Set extra block.
  ASMJIT_INLINE void setExtraBlock(CBNode* node) noexcept { _extraBlock = node; }

  ASMJIT_INLINE RABlock* getEntryBlock() noexcept {
    ASMJIT_ASSERT(!_blocks.isEmpty());
    return _blocks[0];
  }
  ASMJIT_INLINE const RABlock* getEntryBlock() const noexcept {
    ASMJIT_ASSERT(!_blocks.isEmpty());
    return _blocks[0];
  }

  ASMJIT_INLINE uint64_t nextTimestamp() const noexcept { return ++_timestampGenerator; }

  // --------------------------------------------------------------------------
  // [RunOnFunction]
  // --------------------------------------------------------------------------

  //! Run the register allocator for the given `func`.
  Error runOnFunction(Zone* zone, CCFunc* func) noexcept override;

  // --------------------------------------------------------------------------
  // [Init / Done]
  // --------------------------------------------------------------------------

  //! Called by `runOnFunction()` to initialize an architecture-specific data
  //! used by the register allocator. It initialize everything as it's called
  //! per function.
  virtual void onInit() noexcept = 0;

  //! Called after `compile()` to clean everything up, no matter if `compile()`
  //! succeeded or failed.
  virtual void onDone() noexcept = 0;

  // --------------------------------------------------------------------------
  // [Registers]
  // --------------------------------------------------------------------------

  Error _addToWorkRegs(VirtReg* vReg) noexcept;

  //! Creates a `WorkReg` data for the given `vReg`. The function does nothing
  //! if `vReg` already contains link to `WorkReg`. Called by `constructCFG()`.
  ASMJIT_INLINE Error addToWorkRegs(VirtReg* vReg) noexcept {
    // Likely as one virtual register should be used more than once.
    if (ASMJIT_LIKELY(vReg->_workReg))
      return kErrorOk;
    return _addToWorkRegs(vReg);
  }

  // --------------------------------------------------------------------------
  // [Blocks]
  // --------------------------------------------------------------------------

  //! Creates a new `RABlock` and returns it.
  RABlock* newBlock(CBNode* initialNode = nullptr) noexcept;

  //! Tries to find a neighboring CBLabel (without going through code) that is
  //! already connected with `RABlock`. If no label is found then a new RABlock
  //! is created and assigned to all labels in backward direction.
  RABlock* newBlockOrMergeWith(CBLabel* cbLabel) noexcept;

  //! Returns `node` or some node after that is ideal for beginning a new block.
  //! This function is mostly used after a conditional or unconditional jump to
  //! select the successor node. In some cases the next node could be a label,
  //! which means it could have assigned the block already.
  CBNode* findSuccessorStartingAt(CBNode* node) noexcept;

  //! \internal
  bool _strictlyDominates(const RABlock* a, const RABlock* b) const noexcept;
  //! Get whether the block `a` dominates `b`
  //!
  //! This is a strict check, returns false if `a` == `b`.
  ASMJIT_INLINE bool strictlyDominates(const RABlock* a, const RABlock* b) const noexcept {
    if (a == b) return false;
    return _strictlyDominates(a, b);
  }
  //! Get whether the block `a` dominates `b`
  //!
  //! This is a non-strict check, returns true if `a` == `b`.
  ASMJIT_INLINE bool dominates(const RABlock* a, const RABlock* b) const noexcept {
    if (a == b) return true;
    return _strictlyDominates(a, b);
  }

  //! \internal
  const RABlock* _nearestCommonDominator(const RABlock* a, const RABlock* b) const noexcept;
  //! Get a nearest common dominator of `a` and `b`.
  ASMJIT_INLINE RABlock* nearestCommonDominator(RABlock* a, RABlock* b) const noexcept {
    return const_cast<RABlock*>(_nearestCommonDominator(a, b));
  }
  //! \overload
  ASMJIT_INLINE const RABlock* nearestCommonDominator(const RABlock* a, const RABlock* b) const noexcept {
    return _nearestCommonDominator(a, b);
  }

  // --------------------------------------------------------------------------
  // [Loops]
  // --------------------------------------------------------------------------

  //! Creates a new `RALoop` and returns it.
  RALoop* newLoop() noexcept;

  // --------------------------------------------------------------------------
  // [Steps]
  // --------------------------------------------------------------------------

  //! STEP 1:
  //!
  //! Traverse the whole function and do the following:
  //!
  //!   1. Construct CFG (represented by RABlock) by populating `_blocks` and
  //!      `_exits`. Blocks describe the control flow of the function and contain
  //!      some additional information that is used by the register allocator.
  //!   2. Remove unreachable code immediately. This is not strictly necessary
  //!      for CodeCompiler itself as the register allocator cannot reach such
  //!      nodes, but keeping virtual registers would fail during emitting to
  //!      the Assembler.
  virtual Error constructCFG() noexcept = 0;

  //! STEP 2:
  //!
  //! Construct post-order-view (POV).
  Error constructPOV() noexcept;

  //! STEP 3:
  //!
  //! Construct a dominator-tree from CFG.
  //!
  //! Terminology:
  //!   - A node `X` dominates a node `Z` if any path from the entry point to
  //!     `Z` has to go through `X`.
  //!   - A node `Z` post-dominates a node `X` if any path from `X` to the end
  //!     of the graph has to go through `Z`.
  Error constructDOM() noexcept;

  //! STEP 4:
  //!
  //! Construct a loops.
  Error constructLoops() noexcept;

  //! STEP 5:
  //!
  //!   Calculate liveness of virtual registers across blocks.
  Error constructLiveness() noexcept;

  // --------------------------------------------------------------------------
  // [Logging]
  // --------------------------------------------------------------------------

#if !defined(ASMJIT_DISABLE_LOGGING)
  Error _logBlockIds(const RABlocks& blocks) noexcept;
  Error _dumpBlockLiveness(StringBuilder& sb, const RABlock* block) noexcept;

  ASMJIT_INLINE Error logSuccessors(const RABlock* block) noexcept {
    return hasLogger() ? _logBlockIds(block->getSuccessors()) : static_cast<Error>(kErrorOk);
  }
#else
  ASMJIT_INLINE Error logSuccessors(const RABlock* block) noexcept { return kErrorOk; }
#endif

  // --------------------------------------------------------------------------
  // [Members]
  // --------------------------------------------------------------------------

  ZoneHeap _heap;                        //!< ZoneHeap that uses zone passed to `runOnFunction()`.
  Logger* _logger;                       //!< Pass loggins is enabled and logger valid if non-null.

  CCFunc* _func;                         //!< Function being processed.
  CBNode* _stop;                         //!< Stop node.
  CBNode* _extraBlock;                   //!< Node that is used to insert extra code after the function body.

  RABlocks _blocks;                      //!< Blocks (first block is the entry, always exists).
  RABlocks _exits;                       //!< Function exit blocks (usually one, but can contain more).
  RABlocks _pov;                         //!< Post order view (POV) of all `_blocks`.
  RALoops _loops;                        //!< Loops (empty if there are no loops).
  WorkRegs _workRegs;                    //!< Work registers (referenced by the function).

  WorkRegs _workRegsOfKind[Globals::kMaxVRegKinds];
  ZoneBitVector _workSetOfKind[Globals::kMaxVRegKinds];

  RAStackManager _stack;                 //!< Stack manager.

  RARegCount _archRegCount;              //!< Count of machine registers.
  RARegMask _allocableRegs;              //!< Allocable registers (global).
  RARegMask _clobberedRegs;              //!< Clobbered registers of all blocks.
  uint32_t _nodesCount;                  //!< Count of nodes, for allocating liveness bits.
  mutable uint64_t _timestampGenerator;  //!< Timestamp generator.
};

ASMJIT_INLINE ZoneHeap* RABlock::getHeap() const noexcept { return _ra->getHeap(); }
ASMJIT_INLINE ZoneHeap* RALoop::getHeap() const noexcept { return _ra->getHeap(); }

//! \}

} // asmjit namespace

// [Api-End]
#include "../asmjit_apiend.h"

// [Guard]
#endif // !ASMJIT_DISABLE_COMPILER
#endif // _ASMJIT_BASE_RAPASS_P_H
