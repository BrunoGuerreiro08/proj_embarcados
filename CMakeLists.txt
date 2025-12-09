cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(my_game_of_life)

# 1. Adiciona os arquivos fonte (.c) ao alvo 'app'
target_sources(app PRIVATE 
    src/logic_task.c
    src/display_task.c
)

# 2. Diz ao CMake onde procurar o "shared_conf.h"
# (Assumindo que ele está na pasta src/ junto com os .c)
target_include_directories(app PRIVATE src)