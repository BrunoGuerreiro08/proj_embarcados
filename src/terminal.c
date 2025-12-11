/*
 * terminal.c – isolated terminal module for Zephyr
 *
 * Features:
 *  - non-reentrant print via msgq + timer + work
 *  - shell commands (echo, uptime, showdrop)
 *  - simple public init + print API
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include "shared_conf.h"

#include <stdarg.h>
#include <string.h>

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

static K_MUTEX_DEFINE(print_mutex);

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
        printk("%.*s", msg.len, msg.payload);
    }

    k_mutex_unlock(&print_mutex);
}

static void print_timer_handler(struct k_timer *timer)
{
    k_work_submit(&print_work);
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

// static int cmd_cpu_usage(const struct shell *sh, size_t argc, char **argv)
// {
//     ARG_UNUSED(argc);
//     ARG_UNUSED(argv);

//     struct k_thread *thread;
//     k_tid_t tid = NULL;

//     shell_print(sh, "=== Thread CPU Usage ===");

//     /* Iterate over all threads */
//     while ((tid = k_thread_foreach(tid, false)) != NULL) {

//         thread = tid;

//         const char *name = thread->name ? thread->name : "unnamed";

//         //uint64_t runtime_us = k_thread_runtime_stats_get(thread);

//         // shell_print(sh,
//         //     "Thread: %-15s | CPU Time: %llu us | Priority: %d",
//         //     name, runtime_us, thread->base.prio
//         // );
//     }

//     return 0;
// }

/* Register commands */
SHELL_CMD_REGISTER(echo, NULL, "Echo back text using terminal", cmd_echo);
SHELL_CMD_REGISTER(uptime, NULL, "Show uptime (ms)", cmd_uptime);
SHELL_CMD_REGISTER(showdrop, NULL, "Dropped terminal messages", cmd_showdrop);
SHELL_CMD_REGISTER(restart, NULL, "Restart the Game", cmd_restart);
// SHELL_CMD_REGISTER(cpu, NULL, "Show CPU usage per thread", cmd_cpu_usage);

/* ---------------- Public Initialization ---------------- */

void terminal_init(void)
{
    /* init work + timer */
    k_work_init(&print_work, print_work_handler);
    k_timer_init(&print_timer, print_timer_handler, NULL);

    term_print("Terminal initialized.\n");
}