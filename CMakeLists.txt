cmake_minimum_required(VERSION 3.24)
project(mvo_accuracy VERSION 2.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(ENABLE_XLS "Read XLS with FreeXL" ON)
option(MVO_VALIDATE_SOLVERS "Cross-check with NLopt SLSQP" OFF)
include(cmake/Dependencies.cmake)
add_library(mvo_core
  src/dates.cpp src/data.cpp src/io.cpp src/preprocessing.cpp
  src/moments.cpp src/moment_cache.cpp
  src/constraints.cpp src/optimization_model.cpp
  src/highs_solver.cpp src/slsqp_solver.cpp src/solver.cpp src/compatibility.cpp
  src/evaluation.cpp src/reporting.cpp src/portfolio.cpp)
target_include_directories(mvo_core PUBLIC include)
target_link_libraries(mvo_core PUBLIC Eigen3::Eigen)
if(TARGET highs::highs)
  target_link_libraries(mvo_core PRIVATE highs::highs)
else()
  target_link_libraries(mvo_core PRIVATE highs)
endif()
if(TARGET NLopt::nlopt)
  target_link_libraries(mvo_core PRIVATE NLopt::nlopt)
else()
  target_link_libraries(mvo_core PRIVATE nlopt)
endif()
include(cmake/FreeXL.cmake)
if(MVO_VALIDATE_SOLVERS)
  target_compile_definitions(mvo_core PRIVATE MVO_VALIDATE_SOLVERS)
endif()
add_library(mvo_app src/app.cpp src/cli.cpp)
target_link_libraries(mvo_app PUBLIC mvo_core)
add_executable(mvo main.cpp)
target_link_libraries(mvo PRIVATE mvo_app)
add_executable(mvo_tests tests/test_main.cpp)
target_link_libraries(mvo_tests PRIVATE mvo_app)
if(WIN32 AND ENABLE_XLS AND FREEXL_RUNTIME)
  foreach(runtime_target mvo mvo_tests)
    add_custom_command(TARGET ${runtime_target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              ${MVO_XLS_RUNTIME_FILES}
              "$<TARGET_FILE_DIR:${runtime_target}>"
      COMMAND_EXPAND_LISTS VERBATIM)
  endforeach()
endif()
foreach(target mvo_core mvo_app mvo mvo_tests)
  if(MSVC)
    target_compile_options(${target} PRIVATE /utf-8 /W4 /permissive- /fp:precise /EHsc)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endforeach()
enable_testing()
add_test(NAME numerical_regression COMMAND mvo_tests "${CMAKE_SOURCE_DIR}/tests/fixtures")
if(ENABLE_XLS AND EXISTS "${CMAKE_SOURCE_DIR}/data.xls")
  add_test(NAME xls_runtime COMMAND mvo "${CMAKE_SOURCE_DIR}/data.xls" --compat)
  set_tests_properties(xls_runtime PROPERTIES TIMEOUT 10)
  foreach(mode high compat)
    set(mode_flag "")
    if(mode STREQUAL "compat")
      set(mode_flag "--compat")
    endif()
    add_test(NAME output_${mode}
      COMMAND ${CMAKE_COMMAND}
        "-DPROGRAM=$<TARGET_FILE:mvo>"
        "-DDATA=${CMAKE_SOURCE_DIR}/data.xls"
        "-DEXPECTED=${CMAKE_SOURCE_DIR}/tests/fixtures/${mode}.txt"
        "-DMODE=${mode_flag}"
        -P "${CMAKE_SOURCE_DIR}/tests/compare_output.cmake")
  endforeach()
endif()
