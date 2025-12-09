#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/random/random.h> // Necessário para gerar a semente inicial
#include <string.h>
#include <stdlib.h>

/* Configurações do Display */
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320
#define BUF_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT * 2) // RGB565 = 2 bytes por pixel

/* Configurações do Game of Life */
#define CELL_SIZE 10 // Tamanho visual da célula em pixels (ajuste para ver melhor)
#define GRID_W (SCREEN_WIDTH / CELL_SIZE)
#define GRID_H (SCREEN_HEIGHT / CELL_SIZE)

/* Cores (RGB565) */
#define COLOR_ALIVE 0xFFFF // Branco
#define COLOR_DEAD  0x0000 // Preto

/* Buffers de Memória */
static uint8_t frame_buffer[BUF_SIZE]; 

/* Grades de estado: 0 = Morto, 1 = Vivo */
/* Usamos static para não estourar a stack */
static uint8_t grid[GRID_H][GRID_W];
static uint8_t next_grid[GRID_H][GRID_W];

/* --- Funções de Desenho (Baseadas no seu código) --- */

/* Desenha um pixel único no buffer RGB565 */
void draw_pixel(int x, int y, uint16_t color) {
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
    
    int index = (y * SCREEN_WIDTH + x) * 2;
    frame_buffer[index] = color >> 8;     // Byte alto
    frame_buffer[index + 1] = color & 0xFF; // Byte baixo
}

/* Desenha um quadrado preenchido (Célula) */
void draw_cell(int grid_x, int grid_y, uint16_t color) {
    int start_x = grid_x * CELL_SIZE;
    int start_y = grid_y * CELL_SIZE;

    for (int y = 0; y < CELL_SIZE; y++) {
        for (int x = 0; x < CELL_SIZE; x++) {
            // Pequena borda de 1px para ver a grade (opcional, remova o -1 para sólido)
            if (x < CELL_SIZE - 1 && y < CELL_SIZE - 1) {
                draw_pixel(start_x + x, start_y + y, color);
            }
        }
    }
}

/* --- Lógica do Game of Life --- */

/* Inicializa a grade com valores aleatórios */
void init_grid() {
    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            // 50% de chance de começar vivo
            grid[y][x] = (sys_rand32_get() % 2); 
        }
    }
}

/* Conta vizinhos (Lógica Toroidal/Wrap-around: bordas se conectam) */
int count_neighbors(int x, int y) {
    int sum = 0;
    for (int i = -1; i < 2; i++) {
        for (int j = -1; j < 2; j++) {
            if(i == 0 && j == 0) continue; // Não contar a própria célula

            // Modulo aritmético para bordas infinitas
            int col = (x + j + GRID_W) % GRID_W;
            int row = (y + i + GRID_H) % GRID_H;
            
            sum += grid[row][col];
        }
    }
    return sum;
}

/* Calcula a próxima geração */
void compute_next_generation() {
    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            int state = grid[y][x];
            int neighbors = count_neighbors(x, y);

            if (state == 0 && neighbors == 3) {
                next_grid[y][x] = 1; // Reprodução
            } else if (state == 1 && (neighbors < 2 || neighbors > 3)) {
                next_grid[y][x] = 0; // Morte por solidão ou superpopulação
            } else {
                next_grid[y][x] = state; // Permanece igual (estase)
            }
        }
    }

    // Copia next_grid para grid atual
    memcpy(grid, next_grid, sizeof(grid));
}

void main(void) {
    const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

    if (!device_is_ready(display_dev)) return;

    /* Liga o display */
    display_blanking_off(display_dev);

    struct display_buffer_descriptor desc;
    desc.buf_size = BUF_SIZE;
    desc.width = SCREEN_WIDTH;
    desc.height = SCREEN_HEIGHT;
    desc.pitch = SCREEN_WIDTH;

    /* Inicializa o jogo */
    init_grid();

    while (1) {
        /* 1. Limpar buffer (fundo preto) */
        memset(frame_buffer, 0, BUF_SIZE);

        /* 2. Desenhar o estado atual no buffer */
        for (int y = 0; y < GRID_H; y++) {
            for (int x = 0; x < GRID_W; x++) {
                if (grid[y][x] == 1) {
                    draw_cell(x, y, COLOR_ALIVE);
                }
            }
        }

        /* 3. Enviar buffer para a tela */
        display_write(display_dev, 0, 0, &desc, frame_buffer);

        /* 4. Calcular a lógica para o próximo frame */
        compute_next_generation();

        /* Controle de velocidade da simulação */
        k_sleep(K_MSEC(100));
    }
}