#ifndef SHARED_CONF_H
#define SHARED_CONF_H

#include <zephyr/kernel.h>
#include <stdint.h>

/* Configurações do Display */
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320
#define BUF_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT * 2)

/* Configurações do Game of Life */
#define CELL_SIZE 10 
#define GRID_W (SCREEN_WIDTH / CELL_SIZE)
#define GRID_H (SCREEN_HEIGHT / CELL_SIZE)

/* Cores */
#define COLOR_ALIVE 0xFFFF
#define COLOR_DEAD  0x0000

/* Variáveis Compartilhadas */
/* O grid é definido na logic_task.c, mas o display precisa ler */
extern uint8_t grid[GRID_H][GRID_W];

/* Mutex para evitar que o desenho leia enquanto a lógica escreve */
extern struct k_mutex game_mutex;

#endif