#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>
#include <zephyr/kernel/thread.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <shared_conf.h> 

#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

/* ---------------- Configuration ---------------- */

#define PRINT_MSG_MAXLEN 256
#define PRINT_MSGQ_DEPTH 32

/* * DEFINIÇÃO DE PRIORIDADE CRÍTICA:
 * Lógica: -1 (Máxima)
 * Terminal: 4 (Alta - Interrompe o display para imprimir rápido)
 * Display: 5 (Média - Roda quando ninguém mais precisa)
 */
#define TERMINAL_PRIORITY 4 
#define TERMINAL_STACK_SIZE 1024

/* ---------------- Internal Types ---------------- */

struct print_msg {
    uint32_t len;
    char payload[PRINT_MSG_MAXLEN];
};

/* ---------------- Internal State ---------------- */

K_MSGQ_DEFINE(print_msgq, sizeof(struct print_msg), PRINT_MSGQ_DEPTH, 4);

static atomic_t dropped_msgs = ATOMIC_INIT(0);

K_MUTEX_DEFINE(print_mutex); 

/* REMOVIDO: k_work e k_timer (causadores do travamento) */

/* ---------------- Internal Forward Decls ---------------- */
/* A função handler agora é o loop da thread dedicada */
void terminal_thread_entry(void *p1, void *p2, void *p3);

/* ---------------- Public Print API ---------------- */

void term_print(const char *fmt, ...)
{
    struct print_msg msg;
    va_list ap;
    int needed;

    va_start(ap, fmt);
    needed = vsnprintf(msg.payload, sizeof(msg.payload), fmt, ap);
    va_end(ap);

    if (needed < 0) {
        return;
    }

    if ((size_t)needed >= sizeof(msg.payload)) {
        msg.len = sizeof(msg.payload) - 1;
        msg.payload[msg.len] = '\0';
    } else {
        msg.len = needed;
    }

    /* Coloca na fila. A thread dedicada acordará imediatamente se tiver prioridade. */
    if (k_msgq_put(&print_msgq, &msg, K_NO_WAIT) != 0) {
        atomic_inc(&dropped_msgs);
        return;
    }
}

/* ---------------- Thread Logic (Substitui Work/Timer) ---------------- */

void terminal_thread_entry(void *p1, void *p2, void *p3)
{
    struct print_msg msg;

    while (1) {
        /* * K_FOREVER: A thread fica suspensa aqui consumindo 0% de CPU 
         * até que k_msgq_put seja chamado em term_print.
         */
        if (k_msgq_get(&print_msgq, &msg, K_FOREVER) == 0) {
            
            k_mutex_lock(&print_mutex, K_FOREVER);
            // Saída real para o console
            printk("%.*s", msg.len, msg.payload);
            k_mutex_unlock(&print_mutex);
        }
    }
}

/* Inicializa a thread automaticamente com Prioridade 4 */
K_THREAD_DEFINE(terminal_tid, TERMINAL_STACK_SIZE, terminal_thread_entry, NULL, NULL, NULL,
                TERMINAL_PRIORITY, 0, 0);


/* ---------------- Auxiliary Funcs (Threads Info) ---------------- */

static void print_single_thread(const struct k_thread *thread, void *dummy)
{
    static char state_buf[20]; 
    struct k_thread *t = (struct k_thread *)thread; 
    size_t unused_stack_size;
    uint32_t total_stack_size = 0;

    const char *thread_name = k_thread_name_get(t); 
    const char *state_str;

    state_str = k_thread_state_str(t, state_buf, sizeof(state_buf));

    if (t->stack_info.size > 0) {
        total_stack_size = t->stack_info.size;
    }

    if (k_thread_stack_space_get(t, &unused_stack_size) == 0) {
        term_print("  %-16s (0x%p)", thread_name ? thread_name : "N/A", (void *)t);
        term_print("    Estado: %s | Prio: %d", state_str, t->base.prio); 
        if (total_stack_size > 0) {
            uint32_t used_stack = total_stack_size - (uint32_t)unused_stack_size;
            term_print("    Stack Total: %u | Usado: %u | Livre: %u\n",
                    total_stack_size, used_stack, (uint32_t)unused_stack_size);
        } else {
            term_print("    Stack Info: Não disponível ou thread de kernel.\n");
        }
    } else {
        term_print("  %-16s (0x%p) | Estado: %s | Prio: %d | Stack Info: Desconhecida\n", 
                thread_name ? thread_name : "N/A", (void *)t, state_str, t->base.prio);
    }
}

static int cmd_golinfo(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(sh);
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    uint32_t alive = gol_get_alive_count();
    uint32_t total = GRID_H * GRID_W;
    
    uint64_t density_x10 = (uint64_t)alive * 1000ULL / total;
    uint32_t density_int = (uint32_t)(density_x10 / 10);
    uint32_t density_dec = (uint32_t)(density_x10 % 10);
    
    term_print("--- Conway's GoL Status (Somente Dados do Jogo) ---\n");
    term_print("Células Vivas Atuais: %u / %u\n", alive, total);
    term_print("Densidade Média: %u.%u %%\n", density_int, density_dec);
    term_print("-------------------------------------------------\n");

    return 0;
}

/* ---------------- Shell Commands ---------------- */

static int cmd_echo(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(sh);
    if (argc < 2) {
        term_print("Usage: echo <text>\n");
        return 0;
    }
    for (size_t i = 1; i < argc; i++) {
        term_print("%s%s", argv[i], (i + 1 < argc) ? " " : "\n");
    }
    return 0;
}

static int cmd_uptime(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(sh);
    term_print("Uptime: %u ms\n", k_uptime_get_32());
    return 0;
}

static int cmd_showdrop(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(sh);
    term_print("Dropped messages: %d\n", atomic_get(&dropped_msgs));
    return 0;
}

static int cmd_restart(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(sh);
    k_event_post(&game_events, EVENT_RESET_GRID_BIT);
    return 0;
}

static int cmd_sysinfo(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(sh);
    term_print("--- Informações de Tarefas Instaladas e Runtime ---\n");
    k_thread_foreach(print_single_thread, NULL);
    term_print("---------------------------------------------------\n");
    return 0;
}

/* Register commands */
SHELL_CMD_REGISTER(echo, NULL, "Echo back text using terminal", cmd_echo);
SHELL_CMD_REGISTER(uptime, NULL, "Show uptime (ms)", cmd_uptime);
SHELL_CMD_REGISTER(showdrop, NULL, "Dropped terminal messages", cmd_showdrop);
SHELL_CMD_REGISTER(restart, NULL, "Restart the Game", cmd_restart);
SHELL_CMD_REGISTER(sysinfo, NULL, "Mostra informações das tarefas (threads) e runtime.", cmd_sysinfo);
SHELL_CMD_REGISTER(golinfo, NULL, "Mostra status e runtime da tarefa GoL (Game of Life).", cmd_golinfo);

/* ---------------- Public Initialization ---------------- */

void terminal_init(void)
{
    /* A thread já inicia via K_THREAD_DEFINE, apenas notificamos o boot */
    term_print("Terminal initialized (Priority %d).\n", TERMINAL_PRIORITY);
}