cmake_minimum_required(VERSION 3.16)
project(HashJoinPRS LANGUAGES CXX)

# Postavljamo C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Obavezno -O3 za mjerenje realnih performansi (bez ovoga C++ je 10x sporiji)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Putanja do header fajlova
include_directories(include)

# Pomoć za CMake da pronađe Homebrew libomp na macOS-u (Apple Silicon / Intel)
if(APPLE)
    execute_process(COMMAND brew --prefix libomp OUTPUT_VARIABLE HOMEBREW_LIBOMP_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(HOMEBREW_LIBOMP_PREFIX)
        set(OpenMP_CXX_FLAGS "-Xpreprocessor -fopenmp -I${HOMEBREW_LIBOMP_PREFIX}/include")
        set(OpenMP_CXX_LIB_NAMES "omp")
        set(OpenMP_omp_LIBRARY "${HOMEBREW_LIBOMP_PREFIX}/lib/libomp.dylib")
    endif()
endif()

find_package(OpenMP REQUIRED)

# Izvršni fajl
add_executable(hash_join src/main.cpp
    include/data_generator.hpp
    include/types.hpp
    include/nested_loop_join.hpp
    include/seq_hash_join.hpp
    include/verification.hpp
    include/seq_hash_join_open_addr.hpp
    include/parallel_shared_hash_join.hpp
    include/parallel_partitioned_hash_join.hpp)

# Povezivanje OpenMP-a
target_link_libraries(hash_join PRIVATE OpenMP::OpenMP_CXX)