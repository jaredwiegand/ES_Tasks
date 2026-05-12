/*
 * client.c  –  mathdev C client
 * ==============================
 * Implements the same length-prefixed JSON protocol as the Python client
 * over a Unix-domain socket.  Presents a matching terminal UI.
 *
 * Depends on: cJSON (bundled via CMake FetchContent or system package)
 *
 * Build:  see CMakeLists.txt  (or: gcc client.c cJSON.c -o mathdev-client)
 */

#define _GNU_SOURCE
#include <arpa/inet.h>    /* htonl / ntohl                */
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "cJSON.h"        /* bundled or installed          */

/* ── compile-time defaults ─────────────────────────────────────────────────── */
#ifndef DEFAULT_SOCKET_PATH
#  define DEFAULT_SOCKET_PATH  "/tmp/mathdev.sock"
#endif

#define PROTOCOL_VERSION  "1.0"
#define BUFFER_SIZE       8192

/* ── ANSI colour helpers ───────────────────────────────────────────────────── */
#define COL_RESET   "\033[0m"
#define COL_BOLD    "\033[1m"
#define COL_RED     "\033[31m"
#define COL_GREEN   "\033[32m"
#define COL_YELLOW  "\033[33m"
#define COL_CYAN    "\033[36m"

static bool use_colour = true;

#define PRINT_COL(col, fmt, ...) \
    do { if (use_colour) printf(col fmt COL_RESET, ##__VA_ARGS__); \
         else printf(fmt, ##__VA_ARGS__); } while (0)

/* ── low-level framing ─────────────────────────────────────────────────────── */

/*
 * Send a length-prefixed message (4-byte big-endian uint32 + JSON bytes).
 */
static int send_msg(int fd, const char *json_str)
{
    size_t   len    = strlen(json_str);
    uint32_t netLen = htonl((uint32_t)len);

    if (write(fd, &netLen, 4) != 4) return -1;
    ssize_t written = write(fd, json_str, len);
    return (written == (ssize_t)len) ? 0 : -1;
}

/*
 * Receive a length-prefixed message.
 * Returns malloc'd buffer (caller must free), or NULL on error / EOF.
 */
static char *recv_msg(int fd)
{
    uint32_t netLen;
    ssize_t  n;
    size_t   remaining, got = 0;
    char    *buf;

    /* Read 4-byte header */
    n = 0;
    while (n < 4) {
        ssize_t r = read(fd, (char *)&netLen + n, 4 - n);
        if (r <= 0) return NULL;   /* EOF or error */
        n += r;
    }

    remaining = ntohl(netLen);
    if (remaining == 0) return strdup("");

    buf = malloc(remaining + 1);
    if (!buf) return NULL;

    while (got < remaining) {
        n = read(fd, buf + got, remaining - got);
        if (n <= 0) { free(buf); return NULL; }
        got += (size_t)n;
    }
    buf[remaining] = '\0';
    return buf;
}

/* ── JSON helpers ──────────────────────────────────────────────────────────── */

static char *build_hello(const char *client_id)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type",      "HELLO");
    cJSON_AddStringToObject(root, "version",   PROTOCOL_VERSION);
    cJSON_AddStringToObject(root, "client_id", client_id);
    char *str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return str;
}

static char *build_calc(int req_id, const char *op, long long a, long long b)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type",   "CALC");
    cJSON_AddNumberToObject(root, "req_id", req_id);
    cJSON_AddStringToObject(root, "op",     op);
    /* JSON numbers are doubles; for large integers we still use them
       but note that ints > 2^53 may lose precision in standard JSON. */
    cJSON_AddNumberToObject(root, "a", (double)a);
    cJSON_AddNumberToObject(root, "b", (double)b);
    char *str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return str;
}

static char *build_bye(const char *client_id)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type",      "BYE");
    cJSON_AddStringToObject(root, "client_id", client_id);
    char *str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return str;
}

/* ── Operation table (populated from HELLO_ACK) ────────────────────────────── */

#define MAX_OPS     8
#define OP_NAME_LEN 16
#define OP_DESC_LEN 64

typedef struct {
    int  op_code;
    char name[OP_NAME_LEN];
    char description[OP_DESC_LEN];
} OpEntry;

static OpEntry g_ops[MAX_OPS];
static int     g_num_ops = 0;

/* ── UI helpers ────────────────────────────────────────────────────────────── */

static void print_banner(const char *client_id)
{
    puts("");
    PRINT_COL(COL_BOLD COL_CYAN, "╔══════════════════════════════════════╗\n");
    PRINT_COL(COL_BOLD COL_CYAN, "║     mathdev — kernel math ops (C)    ║\n");
    PRINT_COL(COL_BOLD COL_CYAN, "╚══════════════════════════════════════╝\n");
    PRINT_COL(COL_YELLOW,        "  Connected as %s\n\n", client_id);
}

static void print_menu(void)
{
    for (int i = 0; i < g_num_ops; i++)
        printf("  " COL_BOLD "%d" COL_RESET ") %s\n", i + 1, g_ops[i].description);
    printf("  " COL_BOLD "%d" COL_RESET ") Exit\n\n", g_num_ops + 1);
}

/*
 * Read a trimmed line from stdin into buf (up to buf_sz-1 chars).
 * Returns false on EOF.
 */
static bool read_line(const char *prompt, char *buf, size_t buf_sz)
{
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(buf, (int)buf_sz, stdin)) return false;
    buf[strcspn(buf, "\r\n")] = '\0';
    return true;
}

static bool parse_longlong(const char *s, long long *out)
{
    char *end;
    errno = 0;
    *out = strtoll(s, &end, 10);
    return (errno == 0 && end != s && *end == '\0');
}

/* ── Main client logic ─────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    const char *socket_path = DEFAULT_SOCKET_PATH;
    char        client_id[64];

    /* Simple argument parsing */
    snprintf(client_id, sizeof(client_id), "c-client-%d", getpid());

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--socket") && i + 1 < argc)
            socket_path = argv[++i];
        else if (!strcmp(argv[i], "--client-id") && i + 1 < argc)
            snprintf(client_id, sizeof(client_id), "%s", argv[++i]);
        else if (!strcmp(argv[i], "--no-colour") || !strcmp(argv[i], "--no-color"))
            use_colour = false;
        else {
            fprintf(stderr, "Usage: %s [--socket PATH] [--client-id ID] [--no-colour]\n",
                    argv[0]);
            return 1;
        }
    }

    /* Connect */
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        PRINT_COL(COL_RED, "Cannot connect to server at %s\n", socket_path);
        puts("Make sure the server is running.");
        close(fd);
        return 1;
    }

    /* ── HELLO handshake ── */
    {
        char *hello = build_hello(client_id);
        if (send_msg(fd, hello) < 0) {
            perror("send HELLO"); free(hello); close(fd); return 1;
        }
        free(hello);

        char *raw = recv_msg(fd);
        if (!raw) {
            PRINT_COL(COL_RED, "No HELLO_ACK received\n");
            close(fd); return 1;
        }

        cJSON *msg = cJSON_Parse(raw);
        free(raw);
        if (!msg) { PRINT_COL(COL_RED, "Invalid HELLO_ACK JSON\n"); close(fd); return 1; }

        cJSON *type = cJSON_GetObjectItemCaseSensitive(msg, "type");
        if (!cJSON_IsString(type) || strcmp(type->valuestring, "HELLO_ACK")) {
            PRINT_COL(COL_RED, "Expected HELLO_ACK\n");
            cJSON_Delete(msg); close(fd); return 1;
        }

        /* Parse ops list */
        cJSON *ops_arr = cJSON_GetObjectItemCaseSensitive(msg, "ops");
        cJSON *entry;
        cJSON_ArrayForEach(entry, ops_arr) {
            if (g_num_ops >= MAX_OPS) break;
            cJSON *nm   = cJSON_GetObjectItemCaseSensitive(entry, "name");
            cJSON *desc = cJSON_GetObjectItemCaseSensitive(entry, "description");
            cJSON *code = cJSON_GetObjectItemCaseSensitive(entry, "op_code");
            if (cJSON_IsString(nm) && cJSON_IsString(desc) && cJSON_IsNumber(code)) {
                g_ops[g_num_ops].op_code = (int)code->valuedouble;
                strncpy(g_ops[g_num_ops].name,        nm->valuestring,   OP_NAME_LEN - 1);
                strncpy(g_ops[g_num_ops].description, desc->valuestring, OP_DESC_LEN - 1);
                g_num_ops++;
            }
        }
        cJSON_Delete(msg);
    }

    if (g_num_ops == 0) {
        /* Fallback static ops if server didn't send any */
        g_num_ops = 4;
        snprintf(g_ops[0].name, OP_NAME_LEN, "ADD"); snprintf(g_ops[0].description, OP_DESC_LEN, "Add two signed integers");
        snprintf(g_ops[1].name, OP_NAME_LEN, "SUB"); snprintf(g_ops[1].description, OP_DESC_LEN, "Subtract two signed integers");
        snprintf(g_ops[2].name, OP_NAME_LEN, "MUL"); snprintf(g_ops[2].description, OP_DESC_LEN, "Multiply two signed integers");
        snprintf(g_ops[3].name, OP_NAME_LEN, "DIV"); snprintf(g_ops[3].description, OP_DESC_LEN, "Divide two signed integers");
    }

    print_banner(client_id);

    /* ── Main UI loop ── */
    int req_id = 0;
    char line[128];

    while (true) {
        print_menu();

        if (!read_line(COL_BOLD "Enter command: " COL_RESET, line, sizeof(line)))
            break;

        int choice = atoi(line);
        if (choice == g_num_ops + 1) {
            PRINT_COL(COL_GREEN, "Bye!\n");
            break;
        }
        if (choice < 1 || choice > g_num_ops) {
            PRINT_COL(COL_RED, "  ✗ Invalid choice.\n\n");
            continue;
        }

        /* Read operands */
        long long a, b;
        while (true) {
            if (!read_line("Enter operand 1: ", line, sizeof(line))) goto done;
            if (parse_longlong(line, &a)) break;
            PRINT_COL(COL_RED, "  ✗ Not a valid integer.\n");
        }

        while (true) {
            if (!read_line("Enter operand 2: ", line, sizeof(line))) goto done;
            if (parse_longlong(line, &b)) break;
            PRINT_COL(COL_RED, "  ✗ Not a valid integer.\n");
        }

        const char *op = g_ops[choice - 1].name;
        req_id++;

        PRINT_COL(COL_YELLOW, "Sending request...\n");

        char *calc = build_calc(req_id, op, a, b);
        if (send_msg(fd, calc) < 0) {
            perror("send CALC"); free(calc); break;
        }
        free(calc);

        char *raw = recv_msg(fd);
        if (!raw) {
            PRINT_COL(COL_RED, "Connection lost\n");
            break;
        }

        cJSON *msg = cJSON_Parse(raw);
        free(raw);
        if (!msg) { PRINT_COL(COL_RED, "Invalid response JSON\n"); break; }

        cJSON *type = cJSON_GetObjectItemCaseSensitive(msg, "type");
        if (cJSON_IsString(type) && !strcmp(type->valuestring, "CALC_ACK")) {
            PRINT_COL(COL_GREEN, "Request OKAY...\n");
            PRINT_COL(COL_YELLOW, "Receiving response...\n");
            cJSON *res = cJSON_GetObjectItemCaseSensitive(msg, "result");
            long long result = (long long)res->valuedouble;
            PRINT_COL(COL_BOLD COL_GREEN, "Result is %lld!\n", result);
        } else if (cJSON_IsString(type) && !strcmp(type->valuestring, "ERROR")) {
            cJSON *code = cJSON_GetObjectItemCaseSensitive(msg, "error_code");
            cJSON *emsg = cJSON_GetObjectItemCaseSensitive(msg, "message");
            PRINT_COL(COL_RED, "Request FAILED (%s): %s\n",
                      cJSON_IsString(code) ? code->valuestring : "?",
                      cJSON_IsString(emsg) ? emsg->valuestring : "");
        } else {
            PRINT_COL(COL_RED, "Unexpected response type: %s\n",
                      cJSON_IsString(type) ? type->valuestring : "?");
        }
        cJSON_Delete(msg);
        puts("");
    }

done:
    /* ── BYE ── */
    {
        char *bye = build_bye(client_id);
        send_msg(fd, bye);
        free(bye);
        char *raw = recv_msg(fd);
        free(raw);
    }
    close(fd);
    return 0;
}
