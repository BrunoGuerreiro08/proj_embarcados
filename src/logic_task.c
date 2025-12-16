#include <shared_conf.h>
#include <zephyr/random/random.h>
#include <string.h>

// Definição das prioridades e stack
#define LOGIC_PRIORITY -1 // Alta prioridade 
#define LOGIC_STACK_SIZE 1024
#define LOGIC_PERIOD_MS 200
//#define EVENT_RESET_GRID_BIT (1 << 0)

uint8_t grid[GRID_H][GRID_W];
struct k_mutex game_mutex;
struct k_event game_events;
static uint8_t next_grid[GRID_H][GRID_W];
static uint32_t alive_count = 0;

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

// --- Contagem e Status ---
uint32_t gol_get_alive_count() {
    return alive_count;
}

void compute_next_generation() {
    uint32_t count = 0;

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

    // BLOQUEIA para atualizar o grid oficial
    k_mutex_lock(&game_mutex, K_FOREVER);
    memcpy(grid, next_grid, sizeof(grid));

    // Calcula a contagem de vivos após a atualização (aproveitando o mutex)
    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            if (grid[y][x] == 1) {
                count++;
            }
        }
    }

    alive_count = count;

    k_mutex_unlock(&game_mutex);
}

void logic_entry_point(void *p1, void *p2, void *p3) {
    k_mutex_init(&game_mutex); // Inicializa o mutex uma vez
    k_event_init(&game_events);

    k_mutex_lock(&game_mutex, K_FOREVER);
    init_grid();
    k_mutex_unlock(&game_mutex);

    int64_t next_run_time = k_uptime_get();

    while (1) {
        //compute_next_generation();

        next_run_time += LOGIC_PERIOD_MS;

        uint32_t events = k_event_wait(&game_events, EVENT_RESET_GRID_BIT, true, K_MSEC(100));
        if (events & EVENT_RESET_GRID_BIT) {
            // Se recebeu o evento: Reseta
            k_mutex_lock(&game_mutex, K_FOREVER);
            init_grid();
            k_mutex_unlock(&game_mutex);
        } else {
            compute_next_generation();
        }

        int64_t now = k_uptime_get();
        int32_t remaining = (int32_t)(next_run_time - now);

        if (remaining > 0) {
            k_sleep(K_MSEC(remaining));
            
        } else {
            //printk("ALERTA: Prazo perdido! Atraso de %d ms\n", remaining);
            next_run_time = now; 
        }
    }
}

// Define e inicia a thread automaticamente no boot
K_THREAD_DEFINE(logic_tid, LOGIC_STACK_SIZE, logic_entry_point, NULL, NULL, NULL,
                LOGIC_PRIORITY, 0, 0);