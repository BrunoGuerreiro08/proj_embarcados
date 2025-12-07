#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <math.h>
#include <stdlib.h>

/* Estruturas para 3D */
typedef struct { float x, y, z; } Point3D;
typedef struct { int16_t x, y; } Point2D;

/* Vértices do Cubo */
Point3D vertices[8] = {
    {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
    {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1}
};

/* Arestas */
int edges[12][2] = {
    {0,1}, {1,2}, {2,3}, {3,0}, {4,5}, {5,6}, {6,7}, {7,4},
    {0,4}, {1,5}, {2,6}, {3,7}
};

/* Buffer simples para desenhar (ajustado para o tamanho do display 240x320) */
/* Nota: Em RGB565, cada pixel ocupa 2 bytes */
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320
#define BUF_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT * 2)

/* Alocar buffer na memória (pode precisar da SDRAM configurada no overlay se não couber na RAM interna) */
static uint8_t frame_buffer[BUF_SIZE]; 

/* Função auxiliar para desenhar pixel no buffer (formato RGB565) */
void draw_pixel(int x, int y, uint16_t color) {
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
    
    int index = (y * SCREEN_WIDTH + x) * 2;
    frame_buffer[index] = color >> 8;     // Byte alto
    frame_buffer[index + 1] = color & 0xFF; // Byte baixo
}

/* Algoritmo de Bresenham para linhas */
void draw_line(Point2D p1, Point2D p2, uint16_t color) {
    int x0 = p1.x, y0 = p1.y, x1 = p2.x, y1 = p2.y;
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    while (1) {
        draw_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

Point2D project(Point3D p) {
    float fov = 200.0;
    float viewer_distance = 3.5;
    float factor = fov / (viewer_distance + p.z);
    return (Point2D){
        .x = (int16_t)(p.x * factor + SCREEN_WIDTH / 2),
        .y = (int16_t)(p.y * factor + SCREEN_HEIGHT / 2)
    };
}

void rotate(Point3D *p, float angleX, float angleY) {
    float radX = angleX, radY = angleY;
    float newX = p->x * cos(radY) - p->z * sin(radY);
    float newZ = p->x * sin(radY) + p->z * cos(radY);
    p->x = newX; p->z = newZ;
    float newY = p->y * cos(radX) - p->z * sin(radX);
    newZ = p->y * sin(radX) + p->z * cos(radX);
    p->y = newY; p->z = newZ;
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

    float angle = 0.05;

    while (1) {
        /* 1. Limpar buffer (pintar de preto) */
        memset(frame_buffer, 0, BUF_SIZE);

        /* 2. Calcular e desenhar */
        Point2D projected[8];
        for (int i = 0; i < 8; i++) {
            Point3D p = vertices[i];
            rotate(&p, angle, angle * 0.5);
            projected[i] = project(p);
            // Rotacionar vértices originais para animação
            rotate(&vertices[i], 0.02, 0.01);
        }

        for (int i = 0; i < 12; i++) {
            draw_line(projected[edges[i][0]], projected[edges[i][1]], 0xFFFF); // Branco
        }

        /* 3. Enviar buffer para a tela */
        display_write(display_dev, 0, 0, &desc, frame_buffer);

        k_sleep(K_MSEC(50));
    }
}