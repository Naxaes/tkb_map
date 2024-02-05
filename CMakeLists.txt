cmake_minimum_required(VERSION 3.27)
project(generic_map C)

set(CMAKE_C_STANDARD 99)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic -Werror -fsanitize=address -fno-omit-frame-pointer -Wno-newline-eof -Wno-unused-function")
add_executable(generic_map main.c)


set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O1 -fno-omit-frame-pointer -fsanitize=fuzzer,address,leak  -Wall -Wextra -Wpedantic -Werror -fsanitize=address -fno-omit-frame-pointer -Wno-newline-eof -Wno-unused-function")
add_executable(fuzzing fuzzing.c)