######################################################################
#
# DESCRIPTION: Verilator CMake Example: Small CMakeLists.txt with SystemC tracing
#
# This is an example cmake script to build a verilog to SystemC project
# using CMake and Verilator.
#
# This file ONLY is placed under the Creative Commons Public Domain, for
# any use, without warranty, 2020 by Wilson Snyder.
# SPDX-License-Identifier: CC0-1.0
#
######################################################################

# This example builds the tracing_sc example using CMake
# To use it, run the following:

# cd /path/to/verilator/examples/cmake_tracing_sc
# rm -rf build && mkdir build && cd build
# cmake ..
# cmake --build .

cmake_minimum_required(VERSION 3.12)
cmake_policy(SET CMP0074 NEW)

project(cmake_tracing_sc_example CXX)

find_package(verilator HINTS $ENV{VERILATOR_ROOT} ${VERILATOR_ROOT})
if(NOT verilator_FOUND)
    message(
        FATAL_ERROR
        "Verilator was not found. Either install it, or set the VERILATOR_ROOT environment variable"
    )
endif()

# SystemC dependencies
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Find SystemC using SystemC's CMake integration
find_package(SystemCLanguage QUIET)

# Create a new executable target that will contain all your sources
add_executable(example ../make_tracing_sc/sc_main.cpp)
target_compile_features(example PUBLIC cxx_std_14)

set_property(TARGET example PROPERTY CXX_STANDARD ${SystemC_CXX_STANDARD})

# Add the Verilated circuit to the target
verilate(example COVERAGE SYSTEMC TRACE_VCD
  INCLUDE_DIRS "../make_tracing_sc"
  VERILATOR_ARGS -f ../make_tracing_sc/input.vc -x-assign fast
  SOURCES ../make_tracing_sc/top.v
)

verilator_link_systemc(example)
