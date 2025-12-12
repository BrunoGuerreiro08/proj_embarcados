/*
 * terminal.c – isolated terminal module for Zephyr
 *
 * Features:
 * - non-reentrant print via msgq + timer + work
 * - shell commands (echo, uptime, showdrop, sysinfo)
 * - simple public init + print API
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>       // Utilitários gerais
#include <zephyr/kernel/thread.h>  // Necessário para k_thread_foreach_all
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <shared_conf.h> // Assumindo que este define k_event_post e EVENT_RESET_GRID_BIT

#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

/* ---------------- Configuration ---------------- */

#define PRINT_MSG_MAXLEN 256
#define PRINT_MSGQ_DEPTH 32
#define PRINT_FLUSH_MS   10

/* ---------------- Internal Types ---------------- */

struct print_msg {
    uint32_t len;
    char payload[PRINT_MSG_MAXLEN];
};

/* ---------------- Internal State ---------------- */

K_MSGQ_DEFINE(print_msgq, sizeof(struct print_msg), PRINT_MSGQ_DEPTH, 4);

static atomic_t dropped_msgs = ATOMIC_INIT(0);

K_MUTEX_DEFINE(print_mutex); // Tornando o mutex acessível (se necessário)

static struct k_work print_work;
static struct k_timer print_timer;

/* ---------------- Internal Forward Decls ---------------- */
static void print_work_handler(struct k_work *work);
static void print_timer_handler(struct k_timer *timer);

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

    if (k_msgq_put(&print_msgq, &msg, K_NO_WAIT) != 0) {
        atomic_inc(&dropped_msgs);
        return;
  	}

    /* arm timer so flush happens soon */
    k_timer_start(&print_timer, K_MSEC(PRINT_FLUSH_MS), K_NO_WAIT);
}

/* ---------------- Internal Work/Timer ---------------- */

static void print_work_handler(struct k_work *work)
{
    struct print_msg msg;

    k_mutex_lock(&print_mutex, K_FOREVER);

    while (k_msgq_get(&print_msgq, &msg, K_NO_WAIT) == 0) {
        // Usa printk para a saída real no console/shell
        printk("%.*s", msg.len, msg.payload);
    }

    k_mutex_unlock(&print_mutex);
}

static void print_timer_handler(struct k_timer *timer)
{
    k_work_submit(&print_work);
}

/* ---------------- Auxiliary Funcs (Threads Info) ---------------- */

static void print_single_thread(const struct k_thread *thread, void *dummy)
{
    // [NOVA MUDANÇA] Buffer estático para armazenar a string de estado da thread
    // O Zephyr define K_THREAD_STATE_STR_MAX_LEN (cerca de 10)
    static char state_buf[20]; 

    // Crie um ponteiro não-const para a thread para satisfazer as APIs do kernel
    struct k_thread *t = (struct k_thread *)thread; 
    size_t unused_stack_size;
    uint32_t total_stack_size = 0;

    const char *thread_name = k_thread_name_get(t); 
    const char *state_str; // Ponteiro para a string de estado final

    // [NOVA MUDANÇA] Obter a string de estado usando o buffer
    state_str = k_thread_state_str(t, state_buf, sizeof(state_buf));

    // Obtém o tamanho total da stack (se disponível)
    if (t->stack_info.size > 0) {
        total_stack_size = t->stack_info.size;
    }

    // Obtém o espaço de stack não utilizado (Requer CONFIG_THREAD_MONITOR=y)
    if (k_thread_stack_space_get(t, &unused_stack_size) == 0) {
        // Linha 1: Nome da Thread e Endereço
        term_print("  %-16s (0x%p)", 
                   thread_name ? thread_name : "N/A", 
                   (void *)t);

        // Linha 2: Estado e Prioridade
        term_print("    Estado: %s | Prio: %d", 
                state_str, // Uso do ponteiro corrigido
                t->base.prio); 

        // Linha 3: Uso da Stack
        if (total_stack_size > 0) {
            uint32_t used_stack = total_stack_size - (uint32_t)unused_stack_size;
            term_print("    Stack Total: %u | Usado: %u | Livre: %u\n",
                    total_stack_size, used_stack, (uint32_t)unused_stack_size);
        } else {
            term_print("    Stack Info: Não disponível ou thread de kernel.\n");
        }
    } else {
        // Fallback (também usando o state_str corrigido)
        term_print("  %-16s (0x%p) | Estado: %s | Prio: %d | Stack Info: Desconhecida\n", 
                thread_name ? thread_name : "N/A", 
                (void *)t,
                state_str, 
                t->base.prio);
    }
}

static int cmd_golinfo(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(sh);
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    uint32_t alive = gol_get_alive_count();
    uint32_t total = GRID_H * GRID_W;
    
    // 1. Cálculo da Densidade em Milésimos (Fixed-Point)
    // Multiplicamos por 1000 (100 para percentual, 10 para 1 casa decimal)
    // Usamos um tipo maior (uint64_t) para evitar overflow durante a multiplicação
    uint64_t density_x10 = (uint64_t)alive * 1000ULL / total;

    // 2. Separação:
    uint32_t density_int = (uint32_t)(density_x10 / 10);    // Parte Inteira (XX)
    uint32_t density_dec = (uint32_t)(density_x10 % 10);    // Primeiro Decimal (Y)
    
    // --- CONWAY'S GOL STATUS ---
    
    term_print("--- Conway's GoL Status (Somente Dados do Jogo) ---\n");
    
    // Informações da Aplicação (Células Vivas)
    term_print("Células Vivas Atuais: %u / %u\n", alive, total);
    
    // Impressão da Densidade Sem Float: (XX.Y %)
    term_print("Densidade Média: %u.%u %%\n", 
               density_int, density_dec);

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
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    term_print("Uptime: %u ms\n", k_uptime_get_32());
    return 0;
}

static int cmd_showdrop(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(sh);
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    term_print("Dropped messages: %d\n", atomic_get(&dropped_msgs));
    return 0;
}

static int cmd_restart(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(sh);
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    k_event_post(&game_events, EVENT_RESET_GRID_BIT);
    return 0;
}

static int cmd_sysinfo(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(sh);
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    // A seção de Heap foi ignorada conforme solicitado

    // 2. Exibir Informações de Tarefas
    term_print("--- Informações de Tarefas Instaladas e Runtime ---\n");

    // Itera sobre todas as threads e chama a função de impressão
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
    /* init work + timer */
    k_work_init(&print_work, print_work_handler);
    k_timer_init(&print_timer, print_timer_handler, NULL);

    term_print("Terminal initialized.\n");
}