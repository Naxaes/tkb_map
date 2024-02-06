cmake_minimum_required(VERSION 3.27)
project(generic_map C)

set(CMAKE_C_STANDARD 99)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic -Werror -fsanitize=address -fno-omit-frame-pointer -Wno-newline-eof -Wno-unused-function -Wno-unused-variable")
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")
endif()

add_executable(generic_map main.c)


if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O1 -fno-omit-frame-pointer -fsanitize=fuzzer,address,leak  -Wall -Wextra -Wpedantic -Werror -fsanitize=address -fno-omit-frame-pointer -Wno-newline-eof -Wno-unused-function -Wno-unused-variable")
else()
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O1 -fno-omit-frame-pointer -fsanitize=fuzzer,address,leak  -Wall -Wextra -Wpedantic -Werror -fsanitize=address -fno-omit-frame-pointer -Wno-newline-eof -Wno-unused-function")
endif()
add_executable(fuzzing fuzzing.c)