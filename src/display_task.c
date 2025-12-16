#include <shared_conf.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <string.h>

/* Definição das prioridades e stack */
#define DISPLAY_PRIORITY 5 /* Média prioridade (número maior que a lógica) */
#define DISPLAY_STACK_SIZE 4096 /* Display buffer consome stack se não for static, aumentei por segurança */

/* Buffer de Memória (Exclusivo desta tarefa) */
static uint8_t frame_buffer[BUF_SIZE]; 

/* --- Funções de Desenho (Originais) --- */

void draw_pixel(int x, int y, uint16_t color) {
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
    
    int index = (y * SCREEN_WIDTH + x) * 2;
    frame_buffer[index] = color >> 8;     
    frame_buffer[index + 1] = color & 0xFF; 
}

void draw_cell(int grid_x, int grid_y, uint16_t color) {
    int start_x = grid_x * CELL_SIZE;
    int start_y = grid_y * CELL_SIZE;

    for (int y = 0; y < CELL_SIZE; y++) {
        for (int x = 0; x < CELL_SIZE; x++) {
            if (x < CELL_SIZE - 1 && y < CELL_SIZE - 1) {
                draw_pixel(start_x + x, start_y + y, color);
            }
        }
    }
}

/* Função da Thread */
void display_entry_point(void *p1, void *p2, void *p3) {
    const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

    if (!device_is_ready(display_dev)) return;
    
    display_blanking_off(display_dev);

    struct display_buffer_descriptor desc;
    desc.buf_size = BUF_SIZE;
    desc.width = SCREEN_WIDTH;
    desc.height = SCREEN_HEIGHT;
    desc.pitch = SCREEN_WIDTH;

    while (1) {
        /* 1. Limpar buffer */
        //memset(frame_buffer, 0, BUF_SIZE);

        /* 2. Desenhar o estado atual */
        /* BLOQUEIA: Precisamos garantir que o grid não mude no meio do desenho */
        k_mutex_lock(&game_mutex, K_FOREVER);
        
        for (int y = 0; y < GRID_H; y++) {
            for (int x = 0; x < GRID_W; x++) {
                if (grid[y][x] == 1) {
                    draw_cell(x, y, COLOR_ALIVE);
                }else{
                    draw_cell(x, y, 0x0000);
                }
            }
        }
        
        k_mutex_unlock(&game_mutex);
        /* DESBLOQUEIA: Já copiamos o estado para o frame_buffer */

        /* 3. Enviar buffer para a tela */
        /* Isso pode demorar, mas como soltamos o mutex, a lógica pode calcular o próximo frame enquanto isso ocorre */
        display_write(display_dev, 0, 0, &desc, frame_buffer);

        /* Pequeno delay para evitar starvation se o display for muito rápido */
        k_sleep(K_MSEC(10)); 
    }
}

/* Define e inicia a thread automaticamente no boot */
K_THREAD_DEFINE(display_tid, DISPLAY_STACK_SIZE, display_entry_point, NULL, NULL, NULL,
                DISPLAY_PRIORITY, 0, 0);