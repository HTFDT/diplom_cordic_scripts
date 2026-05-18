cmake_minimum_required(VERSION 3.20)
project(cordic LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Складывать exe в build/bin
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Общие include-пути для всех утилит
set(PROJ_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/third_party
    ${CMAKE_SOURCE_DIR}/sin/include
    ${CMAKE_SOURCE_DIR}/ln/include
)

# Общие предупреждения
if(MINGW)
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---- Executables ----

add_executable(converter_sin
    sin/src/converter_sin.cpp
)
set_target_properties(converter_sin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/sin"
)
target_include_directories(converter_sin PRIVATE ${PROJ_INCLUDE_DIRS})

add_executable(const_generator_sin
    sin/src/const_generator_sin.cpp
)
set_target_properties(const_generator_sin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/sin"
)
target_include_directories(const_generator_sin PRIVATE ${PROJ_INCLUDE_DIRS})

add_executable(cordic_sin
    sin/src/cordic_sin.cpp
)
set_target_properties(cordic_sin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/sin"
)
target_include_directories(cordic_sin PRIVATE ${PROJ_INCLUDE_DIRS})

add_executable(ref_comparer_sin
    sin/src/ref_comparer_sin.cpp
)
set_target_properties(ref_comparer_sin PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/sin"
)
target_include_directories(ref_comparer_sin PRIVATE ${PROJ_INCLUDE_DIRS})

add_executable(converter_ln
    ln/src/converter_ln.cpp
)
set_target_properties(converter_ln PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/ln"
)
target_include_directories(converter_ln PRIVATE ${PROJ_INCLUDE_DIRS})

add_executable(const_generator_ln
    ln/src/const_generator_ln.cpp
)
set_target_properties(const_generator_ln PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/ln"
)
target_include_directories(const_generator_ln PRIVATE ${PROJ_INCLUDE_DIRS})

add_executable(cordic_ln
    ln/src/cordic_ln.cpp
)
set_target_properties(cordic_ln PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/ln"
)
target_include_directories(cordic_ln PRIVATE ${PROJ_INCLUDE_DIRS})

add_executable(ln_scan
    ln/src/ln_scan.cpp
)
set_target_properties(ln_scan PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/ln"
)
target_include_directories(ln_scan PRIVATE ${PROJ_INCLUDE_DIRS})


if(MINGW)
  foreach(tgt
      converter_sin
      const_generator_sin
      cordic_sin
      ref_comparer_sin
  )
    target_link_options(${tgt} PRIVATE -static -static-libgcc -static-libstdc++)
  endforeach()
endif()


