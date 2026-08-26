cmake_minimum_required(VERSION 3.16)
project(egz-solver CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Progress logging to stdout while the search runs. On by default; the tables
# written to disk are identical either way.
option(EGZ_DEBUG "Print search progress to stdout" ON)

# Search bounds compiled into the solver. See "Search bounds" in README.md --
# these are correctness invariants, not just loop limits.
set(EGZ_M_MAX 20 CACHE STRING "Exclusive upper bound on m")
set(EGZ_T_MAX 25 CACHE STRING "Exclusive upper bound on t (also the sequence hash radix)")

add_library(egz-core INTERFACE)
target_include_directories(egz-core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(egz-core INTERFACE cxx_std_17)

add_executable(egz-solver main.cpp)
target_link_libraries(egz-solver PRIVATE egz-core)
target_compile_definitions(egz-solver PRIVATE EGZ_M_MAX=${EGZ_M_MAX} EGZ_T_MAX=${EGZ_T_MAX})
if(EGZ_DEBUG)
  target_compile_definitions(egz-solver PRIVATE DEBUG)
endif()

# The regression suite replays values from Experimental tables/, which reach
# m=37 and large multiplicities, so it needs bounds well above the defaults.
add_executable(run_regression tests/run_regression.cpp)
target_link_libraries(run_regression PRIVATE egz-core)
target_compile_definitions(run_regression PRIVATE EGZ_M_MAX=60 EGZ_T_MAX=300)

# Dumps a ring's operation tables; used by the ring structure test below and
# for regenerating tests/ring_goldens.tsv.
add_executable(dump_ring tests/dump_ring.cpp)
target_link_libraries(dump_ring PRIVATE egz-core)

# Development tool for regenerating tests/regression_cases.tsv.
add_executable(table_probe EXCLUDE_FROM_ALL tests/table_probe.cpp)
target_link_libraries(table_probe PRIVATE egz-core)
target_compile_definitions(table_probe PRIVATE EGZ_M_MAX=60 EGZ_T_MAX=300)

enable_testing()
add_test(NAME regression
         COMMAND run_regression ${CMAKE_CURRENT_SOURCE_DIR}/tests/regression_cases.tsv)

# Checks the committed tables against the published theorems. Pure data check,
# so it needs only Python; the solver is passed in for the one check (Thm 2.2)
# that computes rather than reads.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME thesis_verification
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_against_thesis.py
                   --tables "${CMAKE_CURRENT_SOURCE_DIR}/Experimental tables"
                   --solver $<TARGET_FILE:egz-solver>)

  # Drives the --skip expressions through the CLI.
  add_test(NAME skip_rules
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_skip.py
                   $<TARGET_FILE:egz-solver>)

  # Checks that --max-work only ever costs answers, never changes them.
  add_test(NAME max_work
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_max_work.py
                   $<TARGET_FILE:egz-solver>)

  # Ring axioms, the goldens that say which rings must come out identical, and
  # the runtime Z_n[x]/(P) quotients that have to reproduce them.
  add_test(NAME rings
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_rings.py
                   $<TARGET_FILE:dump_ring>
                   --goldens ${CMAKE_CURRENT_SOURCE_DIR}/tests/ring_goldens.tsv
                   --solver $<TARGET_FILE:egz-solver>)
else()
  message(STATUS "Python3 not found; skipping the thesis_verification test")
endif()

foreach(t egz-solver run_regression table_probe dump_ring)
  set_target_properties(${t} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR})
endforeach()
