cmake_minimum_required(VERSION 3.27)
project(generic_map C)

set(CMAKE_C_STANDARD 99)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic -Werror -fsanitize=address -fno-omit-frame-pointer -Wno-newline-eof -Wno-unused-function")
add_executable(generic_map main.c)
