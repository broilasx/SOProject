cmake_minimum_required(VERSION 3.24)
project(projetoSO C)

set(CMAKE_C_STANDARD 11)

add_executable(SENSOR processes.h sensor.c)
add_executable(CONSOLE processes.h user_console.c)
add_executable(MANAGER processes.h system_manager.c)

add_compile_options(-Wall -Wextra)
