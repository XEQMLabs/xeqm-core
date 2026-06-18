# Copyright (c) 2014-2018, The Monero Project
# 
# All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without modification, are
# permitted provided that the following conditions are met:
# 
# 1. Redistributions of source code must retain the above copyright notice, this list of
#    conditions and the following disclaimer.
# 
# 2. Redistributions in binary form must reproduce the above copyright notice, this list
#    of conditions and the following disclaimer in the documentation and/or other
#    materials provided with the distribution.
# 
# 3. Neither the name of the copyright holder nor the names of its contributors may be
#    used to endorse or promote products derived from this software without specific
#    prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
# THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
# THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

oxen_add_library(lmdb
  mdb.c
  midl.c)

if(FREEBSD)
  target_compile_definitions(lmdb PRIVATE MDB_DSYNC=O_SYNC)
endif()
if(ANDROID)
  target_compile_definitions(lmdb PRIVATE ANDROID=1)
endif()
if(${ARCH_WIDTH} EQUAL 32)
  target_compile_definitions(lmdb PUBLIC MDB_VL32)
endif()

target_include_directories(lmdb PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(lmdb PRIVATE Threads::Threads)
set_property(TARGET lmdb PROPERTY POSITION_INDEPENDENT_CODE ON)

# GUI/libwallet install target
if (BUILD_GUI_DEPS)
    if(IOS)
        set(lib_folder lib-${ARCH})
    else()
        set(lib_folder lib)
    endif()
    install(TARGETS lmdb
        ARCHIVE DESTINATION ${lib_folder}
        LIBRARY DESTINATION ${lib_folder})
endif()

# LMDB hot-backup / inspection tools, built from this exact vendored source so
# their on-disk format always matches xeqm-d's database. A stock distro mdb_copy
# (e.g. lmdb-utils) fails against this fork with MDB_VERSION_MISMATCH; linking the
# `lmdb` target above means these tools inherit the same compile definitions
# (MDB_VL32 on 32-bit builds, etc.) automatically on every platform. Shipped as
# xeqm-mdb_copy / xeqm-mdb_stat so operators can take consistent live snapshots:
#   xeqm-mdb_copy -c <data-dir>/lmdb <dest>
foreach(_lmdb_tool mdb_copy mdb_stat)
  add_executable(${_lmdb_tool} ${_lmdb_tool}.c)
  target_link_libraries(${_lmdb_tool} PRIVATE lmdb Threads::Threads)
  set_target_properties(${_lmdb_tool} PROPERTIES
    OUTPUT_NAME "xeqm-${_lmdb_tool}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endforeach()
