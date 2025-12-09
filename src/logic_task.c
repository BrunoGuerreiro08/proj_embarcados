#include "shared_conf.h"
#include <zephyr/random/random.h>
#include <string.h>

/* Definição das prioridades e stack */
#define LOGIC_PRIORITY 1 /* Alta prioridade */
#define LOGIC_STACK_SIZE 1024

/* Definição real das variáveis compartilhadas */
uint8_t grid[GRID_H][GRID_W];
struct k_mutex game_mutex;

/* Buffer interno para cálculo (não precisa ser compartilhado) */
static uint8_t next_grid[GRID_H][GRID_W];

/* --- Lógica do Game of Life (Mantida idêntica) --- */

void init_grid() {
    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            grid[y][x] = (sys_rand32_get() % 2); 
        }
    }
}

int count_neighbors(int x, int y) {
    int sum = 0;
    for (int i = -1; i < 2; i++) {
        for (int j = -1; j < 2; j++) {
            if(i == 0 && j == 0) continue; 
            int col = (x + j + GRID_W) % GRID_W;
            int row = (y + i + GRID_H) % GRID_H;
            sum += grid[row][col];
        }
    }
    return sum;
}

void compute_next_generation() {
    /* Calcula tudo no buffer temporário primeiro (não precisa travar mutex aqui) */
    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            int state = grid[y][x];
            int neighbors = count_neighbors(x, y);

            if (state == 0 && neighbors == 3) {
                next_grid[y][x] = 1; 
            } else if (state == 1 && (neighbors < 2 || neighbors > 3)) {
                next_grid[y][x] = 0; 
            } else {
                next_grid[y][x] = state; 
            }
        }
    }

    /* BLOQUEIA para atualizar o grid oficial */
    k_mutex_lock(&game_mutex, K_FOREVER);
    memcpy(grid, next_grid, sizeof(grid));
    k_mutex_unlock(&game_mutex);
}

/* Função da Thread */
void logic_entry_point(void *p1, void *p2, void *p3) {
    k_mutex_init(&game_mutex); /* Inicializa o mutex uma vez */
    init_grid();

    while (1) {
        compute_next_generation();
        
        /* Dorme para liberar a CPU para a tarefa de desenho (prioridade menor) */
        /* Se não dormir, a tarefa de média prioridade nunca roda */
        k_sleep(K_MSEC(100)); 
    }
}

/* Define e inicia a thread automaticamente no boot */
K_THREAD_DEFINE(logic_tid, LOGIC_STACK_SIZE, logic_entry_point, NULL, NULL, NULL,
                LOGIC_PRIORITY, 0, 0);