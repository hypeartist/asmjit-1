cmake_minimum_required(VERSION 3.1)

# Don't create a project if it was already created by another CMakeLists.txt.
# This allows one library to embed another library without a project collision.
if(NOT CMAKE_PROJECT_NAME OR "${CMAKE_PROJECT_NAME}" STREQUAL "asmjit")
  project(asmjit C CXX)
endif()

# =============================================================================
# [AsmJit - Configuration]
# =============================================================================

set(ASMJIT_DIR        "${CMAKE_CURRENT_LIST_DIR}" CACHE PATH "Location of 'asmjit'")

set(ASMJIT_EMBED      FALSE CACHE BOOLEAN "Embed 'asmjit' library (no targets)")
set(ASMJIT_STATIC     FALSE CACHE BOOLEAN "Build 'asmjit' library as static")
set(ASMJIT_BUILD_X86  FALSE CACHE BOOLEAN "Build X86/X64 backends")
set(ASMJIT_BUILD_ARM  FALSE CACHE BOOLEAN "Build ARM/Thumb backends")
set(ASMJIT_BUILD_A64  FALSE CACHE BOOLEAN "Build ARM64 (AArch64) backend")
set(ASMJIT_BUILD_TEST FALSE CACHE BOOLEAN "Build 'asmjit_test' applications")

# =============================================================================
# [AsmJit - Project]
# =============================================================================

include("${ASMJIT_DIR}/CxxProject.cmake")
cxx_project(asmjit)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
  set(ASMJIT_PRIVATE_LFLAGS "/OPT:REF /OPT:ICF")

  list(APPEND ASMJIT_PRIVATE_CFLAGS /GF)
  list(APPEND ASMJIT_PRIVATE_CFLAGS_DBG /GS /GR-)
  list(APPEND ASMJIT_PRIVATE_CFLAGS_REL /Oi /Oy /GS- /GR-)

  # Enable multi-process compilation.
  if(NOT MSVC60 AND NOT MSVC70 AND NOT MSVC71)
    list(APPEND ASMJIT_PRIVATE_CFLAGS /MP)
  endif()
endif()

if("${CMAKE_CXX_COMPILER_ID}" MATCHES "^(GNU|Clang)$")
  cxx_detect_standard(ASMJIT_PRIVATE_CFLAGS)
  cxx_detect_cflags(ASMJIT_PRIVATE_CFLAGS
    "-fno-tree-vectorize"
    "-fvisibility=hidden"
    "-Winconsistent-missing-override")
  cxx_detect_cflags(ASMJIT_PRIVATE_CFLAGS_REL
    "-O2" # CMake by default uses -O3, which does nothing useful.
    "-fno-keep-static-consts"
    "-fmerge-all-constants")
endif()

if(WIN32)
  list(APPEND ASMJIT_PRIVATE_CFLAGS "${CXX_DEFINE}_UNICODE")
else()
  list(APPEND ASMJIT_DEPS pthread)
endif()

if("${CMAKE_SYSTEM_NAME}" MATCHES "Linux")
  list(APPEND ASMJIT_DEPS rt)
endif()

set(ASMJIT_LIBS ${ASMJIT_DEPS})
if(NOT ASMJIT_EMBED)
  list(INSERT ASMJIT_LIBS 0 asmjit)
endif()

foreach(BUILD_OPTION
    ASMJIT_BUILD_X86
    ASMJIT_BUILD_ARM
    ASMJIT_BUILD_A64
    ASMJIT_DISABLE_BUILDER
    ASMJIT_DISABLE_COMPILER
    ASMJIT_DISABLE_TEXT
    ASMJIT_DISABLE_LOGGING
    ASMJIT_DISABLE_VALIDATION)
  if(${BUILD_OPTION})
    List(APPEND ASMJIT_CFLAGS         "${CXX_DEFINE}${BUILD_OPTION}")
    List(APPEND ASMJIT_PRIVATE_CFLAGS "${CXX_DEFINE}${BUILD_OPTION}")
  endif()
endforeach()

cxx_project_info(asmjit)

# =============================================================================
# [AsmJit - Source]
# =============================================================================

set(ASMJIT_SRC "")

cxx_add_source(asmjit ASMJIT_SRC asmjit
  asmjit.h
  asmjit_apibegin.h
  asmjit_apiend.h
  asmjit_build.h
  base.h
  arm.h
  x86.h
)

cxx_add_source(asmjit ASMJIT_SRC asmjit/base
  arch.cpp
  arch.h
  assembler.cpp
  assembler.h
  codebuilder.cpp
  codebuilder.h
  codecompiler.cpp
  codecompiler.h
  codeemitter.cpp
  codeemitter.h
  codeholder.cpp
  codeholder.h
  constpool.cpp
  constpool.h
  cpuinfo.cpp
  cpuinfo.h
  func.cpp
  func.h
  globals.cpp
  globals.h
  logging.cpp
  logging.h
  operand.cpp
  operand.h
  osutils.cpp
  osutils.h
  rabuilders_p.h
  rapass.cpp
  rapass_p.h
  runtime.cpp
  runtime.h
  simdtypes.h
  string.cpp
  string.h
  utils.cpp
  utils.h
  vmem.cpp
  vmem.h
  zone.cpp
  zone.h
)

cxx_add_source(asmjit ASMJIT_SRC asmjit/arm
  armassembler.cpp
  armassembler.h
  armbuilder.cpp
  armbuilder.h
  armcompiler.cpp
  armcompiler.h
  armemitter.h
  armglobals.h
  arminternal.cpp
  arminternal_p.h
  arminst.cpp
  arminst.h
  armlogging.cpp
  armlogging_p.h
  armoperand.cpp
  armoperand_regs.cpp
  armoperand.h
  armrapass.cpp
  armrapass_p.h
)

cxx_add_source(asmjit ASMJIT_SRC asmjit/x86
  x86assembler.cpp
  x86assembler.h
  x86builder.cpp
  x86builder.h
  x86compiler.cpp
  x86compiler.h
  x86emitter.h
  x86globals.h
  x86internal.cpp
  x86internal_p.h
  x86inst.cpp
  x86inst.h
  x86logging.cpp
  x86logging_p.h
  x86operand.cpp
  x86operand_regs.cpp
  x86operand.h
  x86rapass.cpp
  x86rapass_p.h
  x86ssetoavxpass.cpp
  x86ssetoavxpass_p.h
)

# =============================================================================
# [AsmJit - Targets]
# =============================================================================

if(NOT ASMJIT_EMBED)
  # Add `asmjit` library.
  cxx_add_library(asmjit asmjit
    "${ASMJIT_SRC}"
    "${ASMJIT_DEPS}"
    "${ASMJIT_PRIVATE_CFLAGS}"
    "${ASMJIT_PRIVATE_CFLAGS_DBG}"
    "${ASMJIT_PRIVATE_CFLAGS_REL}")

  foreach(_src_file ${ASMJIT_SRC})
    get_filename_component(_src_dir ${_src_file} PATH)
    get_filename_component(_src_name ${_src_file} NAME)
    string(REGEX REPLACE "^${ASMJIT_SOURCE_DIR}/" "" targetpath "${_src_dir}")
    if("${_src_name}" MATCHES ".h$")
      if(NOT "${_src_name}" MATCHES "_p.h$")
        install(FILES ${_src_file} DESTINATION "include/${targetpath}")
      endif()
    endif()
  endforeach()

  # Add `asmjit` tests and samples.
  if(ASMJIT_BUILD_TEST)
    cxx_add_source(asmjit ASMJIT_TEST_SRC ../test asmjit_test_unit.cpp broken.cpp broken.h)
    cxx_add_executable(asmjit asmjit_test_unit
      "${ASMJIT_SRC};${ASMJIT_TEST_SRC}"
      "${ASMJIT_DEPS}"
      "${ASMJIT_PRIVATE_CFLAGS};${CXX_DEFINE}ASMJIT_TEST;${CXX_DEFINE}ASMJIT_EMBED"
      "${ASMJIT_PRIVATE_CFLAGS_DBG}"
      "${ASMJIT_PRIVATE_CFLAGS_REL}")

    foreach(_target asmjit_bench_x86 asmjit_test_opcode asmjit_test_x86_asm asmjit_test_x86_cc)
      cxx_add_executable(asmjit ${_target} "test/${_target}.cpp" "${ASMJIT_LIBS}" "${ASMJIT_CFLAGS}" "" "")
    endforeach()
  endif()
endif()
