cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(my_game_of_life)

# 1. Adiciona os arquivos fonte (.c) ao alvo 'app'

zephyr_include_directories(includes)
# 2. Diz ao CMake onde procurar o "shared_conf.h"
# (Assumindo que ele está na pasta src/ junto com os .c)

# Add all .c files inside src/
file(GLOB app_sources src/*.c)

target_sources(app PRIVATE ${app_sources})
