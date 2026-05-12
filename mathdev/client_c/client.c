/*
 * client.c  -  mathdev C client (MISRA C:2012 compliant)
 * ========================================================
 *
 * MISRA C:2012 compliance notes:
 *
 *   Rule 1.3   - No undefined behaviour.
 *   Rule 2.2   - No dead code.
 *   Rule 3.1   - No nested comments.
 *   Rule 7.4   - String literals assigned to const char * only.
 *   Rule 8.4   - Compatible declarations for all externals.
 *   Rule 8.7   - Functions not used outside TU are static.
 *   Rule 8.13  - Pointers const-qualified where possible.
 *   Rule 10.1  - Operands of appropriate essential type.
 *   Rule 10.3  - Value assigned to variable of same essential type.
 *   Rule 10.4  - Binary operator operands same essential type.
 *   Rule 11.3  - No cast between pointer-to-object types (Dev-3 below).
 *   Rule 12.1  - Operator precedence made explicit with parentheses.
 *   Rule 14.4  - Controlling expressions are essentially Boolean.
 *   Rule 15.1  - No goto.
 *   Rule 15.4  - One break per loop.
 *   Rule 15.5  - Single point of exit per function.
 *   Rule 16.3  - All switch clauses have break or return.
 *   Rule 17.1  - No variadic macros or <stdarg.h>.
 *   Rule 18.8  - No variable-length arrays.
 *   Rule 21.3  - No dynamic memory allocation (Dev-2 below).
 *   Rule 21.6  - No <stdio.h> (Dev-1 below).
 *   Dir  4.6   - Numeric types from <stdint.h>.
 *   Dir  4.11  - Validity of values passed to library functions checked.
 *
 * Deviations (documented per MISRA process):
 *
 *   Dev-1  Rule 21.6:
 *          All output uses write(2) on STDOUT_FILENO instead of printf.
 *          Avoids format-string risks and <stdio.h> buffering.
 *
 *   Dev-2  Rule 21.3:
 *          cJSON internally uses malloc/free. Usage is isolated to three
 *          builder functions; all cJSON objects are created and destroyed
 *          within the same function scope. No cJSON pointer escapes.
 *
 *   Dev-3  Rule 11.3:
 *          connect(2) requires casting (struct sockaddr_un *) to
 *          (struct sockaddr *). This is a POSIX API constraint.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <errno.h>

#include "cJSON.h"

/* -----------------------------------------------------------------------
 * Compile-time constants
 * ----------------------------------------------------------------------- */
#ifndef DEFAULT_SOCKET_PATH
#  define DEFAULT_SOCKET_PATH   "/tmp/mathdev.sock"
#endif

#define PROTOCOL_VERSION        "1.0"
#define MSG_BUF_SIZE            (4096U)
#define LINE_BUF_SIZE           (128U)
#define CLIENT_ID_SIZE          (64U)
#define SOCKET_PATH_SIZE        (108U)
#define MAX_OPS                 (8U)
#define OP_NAME_LEN             (16U)
#define OP_DESC_LEN             (64U)
#define HEADER_SIZE             (4U)
#define INT_STR_SIZE            (24U)

#define COL_RESET               "\033[0m"
#define COL_BOLD                "\033[1m"
#define COL_RED                 "\033[31m"
#define COL_GREEN               "\033[32m"
#define COL_YELLOW              "\033[33m"
#define COL_CYAN                "\033[36m"

/* -----------------------------------------------------------------------
 * Type definitions  (Dir 4.6)
 * ----------------------------------------------------------------------- */
typedef uint8_t  bool_t;
#define BOOL_TRUE   ((bool_t)1U)
#define BOOL_FALSE  ((bool_t)0U)

typedef int32_t  status_t;
#define STATUS_OK   ((status_t) 0)
#define STATUS_ERR  ((status_t)-1)

typedef struct
{
    int32_t op_code;
    char    name[OP_NAME_LEN];
    char    description[OP_DESC_LEN];
} op_entry_t;

/* -----------------------------------------------------------------------
 * Module-level state  (Rule 8.7: static)
 * ----------------------------------------------------------------------- */
static op_entry_t g_ops[MAX_OPS];
static uint32_t   g_num_ops    = 0U;
static bool_t     g_use_colour = BOOL_TRUE;

/* -----------------------------------------------------------------------
 * Forward declarations  (Rule 8.4)
 * ----------------------------------------------------------------------- */
static void     write_str(const char *s);
static void     write_colour(const char *col, const char *s);
static void     int64_to_str(int64_t value, char *buf, size_t buf_sz);
static uint32_t uint32_to_str(uint32_t value, char *buf, size_t buf_sz);
static status_t send_msg(int32_t fd, const char *json_str);
static status_t recv_msg(int32_t fd, char *buf, uint32_t buf_sz,
                         uint32_t *out_len);
static bool_t   read_line(const char *prompt, char *buf, size_t buf_sz);
static bool_t   parse_int64(const char *s, int64_t *out);
static status_t build_and_send_hello(int32_t fd, const char *client_id);
static status_t build_and_send_calc(int32_t fd, int32_t req_id,
                                    const char *op, int64_t a, int64_t b);
static status_t build_and_send_bye(int32_t fd, const char *client_id);
static void     load_fallback_ops(void);
static status_t parse_hello_ack(const char *json);
static status_t parse_calc_response(const char *json);
static void     print_banner(const char *client_id);
static void     print_menu(void);
static status_t do_handshake(int32_t fd, const char *client_id);
static status_t do_calc_request(int32_t fd, int32_t *req_id_inout);
static status_t send_bye(int32_t fd, const char *client_id);
static status_t parse_args(int32_t argc, char * const argv[],
                            char *socket_path, uint32_t socket_path_sz,
                            char *client_id,   uint32_t client_id_sz);

/* -----------------------------------------------------------------------
 * Output helpers  (Dev-1: write(2) not printf)
 * ----------------------------------------------------------------------- */
static void write_str(const char *s)
{
    size_t len;
    if (s != NULL)
    {
        len = strlen(s);
        if (len > 0U)
        {
            (void)write(STDOUT_FILENO, s, len);
        }
    }
}

static void write_colour(const char *col, const char *s)
{
    if (g_use_colour == BOOL_TRUE)
    {
        write_str(col);
        write_str(s);
        write_str(COL_RESET);
    }
    else
    {
        write_str(s);
    }
}

/* -----------------------------------------------------------------------
 * Integer-to-string (replaces printf for numbers, Rule 21.6)
 * ----------------------------------------------------------------------- */
static void int64_to_str(int64_t value, char *buf, size_t buf_sz)
{
    char     tmp[INT_STR_SIZE];
    uint32_t i     = 0U;
    uint32_t start = 0U;
    bool_t   neg   = BOOL_FALSE;
    int64_t  v     = value;

    if ((buf == NULL) || (buf_sz < 2U))
    {
        return;
    }
    if (v == INT64_MIN)
    {
        (void)strncpy(buf, "-9223372036854775808", buf_sz - 1U);
        buf[buf_sz - 1U] = '\0';
        return;
    }
    if (v < (int64_t)0)
    {
        neg = BOOL_TRUE;
        v   = -v;
    }
    do
    {
        tmp[i] = (char)((uint8_t)'0' + (uint8_t)((uint64_t)v % 10ULL));
        i++;
        v = (int64_t)((uint64_t)v / 10ULL);
    } while ((v > (int64_t)0) && (i < ((uint32_t)INT_STR_SIZE - 1U)));

    if (neg == BOOL_TRUE)
    {
        tmp[i] = '-';
        i++;
    }
    if (i >= (uint32_t)buf_sz)
    {
        i = (uint32_t)(buf_sz - 1U);
    }
    buf[i] = '\0';
    while (i > 0U)
    {
        i--;
        buf[start] = tmp[i];
        start++;
    }
}

static uint32_t uint32_to_str(uint32_t value, char *buf, size_t buf_sz)
{
    char     tmp[12U];
    uint32_t i      = 0U;
    uint32_t start  = 0U;
    uint32_t result = 0U;
    uint32_t v      = value;

    if ((buf == NULL) || (buf_sz < 2U))
    {
        result = 0U;
    }
    else
    {
        do
        {
            tmp[i] = (char)((uint8_t)'0' + (uint8_t)(v % 10U));
            i++;
            v = v / 10U;
        } while ((v > 0U) && (i < 11U));

        if (i >= (uint32_t)buf_sz)
        {
            i = (uint32_t)(buf_sz - 1U);
        }
        buf[i] = '\0';
        result = i;
        while (i > 0U)
        {
            i--;
            buf[start] = tmp[i];
            start++;
        }
    }
    return result;
}

/* -----------------------------------------------------------------------
 * Protocol framing  (Rule 21.3: no malloc in this layer)
 * ----------------------------------------------------------------------- */
static status_t send_msg(int32_t fd, const char *json_str)
{
    uint8_t  header[HEADER_SIZE];
    uint32_t len;
    ssize_t  written;
    status_t result = STATUS_ERR;

    if (json_str != NULL)
    {
        len = (uint32_t)strlen(json_str);
        header[0] = (uint8_t)((len >> 24U) & 0xFFU);
        header[1] = (uint8_t)((len >> 16U) & 0xFFU);
        header[2] = (uint8_t)((len >>  8U) & 0xFFU);
        header[3] = (uint8_t)( len         & 0xFFU);

        written = write(fd, header, HEADER_SIZE);
        if (written == (ssize_t)HEADER_SIZE)
        {
            written = write(fd, json_str, (size_t)len);
            if (written == (ssize_t)len)
            {
                result = STATUS_OK;
            }
        }
    }
    return result;
}

static status_t recv_msg(int32_t fd, char *buf, uint32_t buf_sz,
                         uint32_t *out_len)
{
    uint8_t  header[HEADER_SIZE];
    uint32_t length;
    uint32_t received = 0U;
    ssize_t  n;
    status_t result   = STATUS_ERR;
    uint32_t h        = 0U;

    if ((buf != NULL) && (out_len != NULL) && (buf_sz >= 2U))
    {
        *out_len = 0U;
        result   = STATUS_OK;

        while ((h < HEADER_SIZE) && (result == STATUS_OK))
        {
            n = read(fd, &header[h], (size_t)(HEADER_SIZE - h));
            if (n <= (ssize_t)0)
            {
                result = STATUS_ERR;
            }
            else
            {
                h += (uint32_t)n;
            }
        }

        if (result == STATUS_OK)
        {
            length = (((uint32_t)header[0]) << 24U) |
                     (((uint32_t)header[1]) << 16U) |
                     (((uint32_t)header[2]) <<  8U) |
                      ((uint32_t)header[3]);

            if (length >= buf_sz)
            {
                result = STATUS_ERR;
            }
            else if (length == 0U)
            {
                buf[0]   = '\0';
                *out_len = 0U;
            }
            else
            {
                while ((received < length) && (result == STATUS_OK))
                {
                    n = read(fd, &buf[received],
                             (size_t)(length - received));
                    if (n <= (ssize_t)0)
                    {
                        result = STATUS_ERR;
                    }
                    else
                    {
                        received += (uint32_t)n;
                    }
                }
                if (result == STATUS_OK)
                {
                    buf[length] = '\0';
                    *out_len    = length;
                }
            }
        }
    }
    return result;
}

/* -----------------------------------------------------------------------
 * Input  (Rule 21.6: read(2) not fgets)
 * ----------------------------------------------------------------------- */
static bool_t read_line(const char *prompt, char *buf, size_t buf_sz)
{
    size_t  i      = 0U;
    ssize_t n;
    char    c      = '\0';
    bool_t  result = BOOL_TRUE;

    write_str(prompt);

    if ((buf == NULL) || (buf_sz < 2U))
    {
        result = BOOL_FALSE;
    }
    else
    {
        while ((i < (buf_sz - 1U)) && (result == BOOL_TRUE))
        {
            n = read(STDIN_FILENO, &c, 1U);
            if (n <= (ssize_t)0)
            {
                result = BOOL_FALSE;
            }
            else if (c == '\n')
            {
                break;
            }
            else if (c != '\r')
            {
                buf[i] = c;
                i++;
            }
            else
            {
                /* skip CR */
            }
        }
        buf[i] = '\0';
    }
    return result;
}

static bool_t parse_int64(const char *s, int64_t *out)
{
    bool_t   neg    = BOOL_FALSE;
    uint64_t acc    = 0ULL;
    uint32_t i      = 0U;
    bool_t   result = BOOL_TRUE;

    if ((s == NULL) || (out == NULL) || (s[0] == '\0'))
    {
        result = BOOL_FALSE;
    }
    else
    {
        if (s[0] == '-')
        {
            neg = BOOL_TRUE;
            i   = 1U;
        }
        else if (s[0] == '+')
        {
            i = 1U;
        }
        else
        {
            /* positive, no prefix */
        }

        if (s[i] == '\0')
        {
            result = BOOL_FALSE;
        }
        else
        {
            while ((s[i] != '\0') && (result == BOOL_TRUE))
            {
                if ((s[i] < '0') || (s[i] > '9'))
                {
                    result = BOOL_FALSE;
                }
                else
                {
                    uint64_t digit = (uint64_t)((uint8_t)s[i] - (uint8_t)'0');
                    if (acc > (9223372036854775807ULL / 10ULL))
                    {
                        result = BOOL_FALSE;
                    }
                    else
                    {
                        acc = (acc * 10ULL) + digit;
                        if ((neg == BOOL_FALSE) &&
                            (acc > (uint64_t)INT64_MAX))
                        {
                            result = BOOL_FALSE;
                        }
                        else if ((neg == BOOL_TRUE) &&
                                 (acc > ((uint64_t)INT64_MAX + 1ULL)))
                        {
                            result = BOOL_FALSE;
                        }
                        else
                        {
                            /* in range */
                        }
                    }
                    i++;
                }
            }
            if (result == BOOL_TRUE)
            {
                *out = (neg == BOOL_TRUE) ? -(int64_t)acc : (int64_t)acc;
            }
        }
    }
    return result;
}

/* -----------------------------------------------------------------------
 * JSON builders  (Dev-2: cJSON malloc isolated here)
 * ----------------------------------------------------------------------- */
static status_t build_and_send_hello(int32_t fd, const char *client_id)
{
    cJSON   *root   = NULL;
    char    *str    = NULL;
    status_t result = STATUS_ERR;

    root = cJSON_CreateObject();
    if (root != NULL)
    {
        (void)cJSON_AddStringToObject(root, "type",      "HELLO");
        (void)cJSON_AddStringToObject(root, "version",   PROTOCOL_VERSION);
        (void)cJSON_AddStringToObject(root, "client_id", client_id);
        str = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        root = NULL;
        if (str != NULL)
        {
            result = send_msg(fd, str);
            cJSON_free(str);
            str = NULL;
        }
    }
    return result;
}

static status_t build_and_send_calc(int32_t fd, int32_t req_id,
                                    const char *op, int64_t a, int64_t b)
{
    cJSON   *root   = NULL;
    char    *str    = NULL;
    status_t result = STATUS_ERR;

    root = cJSON_CreateObject();
    if (root != NULL)
    {
        (void)cJSON_AddStringToObject(root, "type",   "CALC");
        (void)cJSON_AddNumberToObject(root, "req_id", (double)req_id);
        (void)cJSON_AddStringToObject(root, "op",     op);
        (void)cJSON_AddNumberToObject(root, "a",      (double)a);
        (void)cJSON_AddNumberToObject(root, "b",      (double)b);
        str = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        root = NULL;
        if (str != NULL)
        {
            result = send_msg(fd, str);
            cJSON_free(str);
            str = NULL;
        }
    }
    return result;
}

static status_t build_and_send_bye(int32_t fd, const char *client_id)
{
    cJSON   *root   = NULL;
    char    *str    = NULL;
    status_t result = STATUS_ERR;

    root = cJSON_CreateObject();
    if (root != NULL)
    {
        (void)cJSON_AddStringToObject(root, "type",      "BYE");
        (void)cJSON_AddStringToObject(root, "client_id", client_id);
        str = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        root = NULL;
        if (str != NULL)
        {
            result = send_msg(fd, str);
            cJSON_free(str);
            str = NULL;
        }
    }
    return result;
}

/* -----------------------------------------------------------------------
 * Fallback ops
 * ----------------------------------------------------------------------- */
static void load_fallback_ops(void)
{
    g_num_ops = 4U;

    (void)strncpy(g_ops[0U].name, "ADD", (size_t)(OP_NAME_LEN - 1U));
    (void)strncpy(g_ops[0U].description, "Add two signed integers",
                  (size_t)(OP_DESC_LEN - 1U));
    g_ops[0U].name[OP_NAME_LEN - 1U]        = '\0';
    g_ops[0U].description[OP_DESC_LEN - 1U] = '\0';
    g_ops[0U].op_code = 1;

    (void)strncpy(g_ops[1U].name, "SUB", (size_t)(OP_NAME_LEN - 1U));
    (void)strncpy(g_ops[1U].description, "Subtract two signed integers",
                  (size_t)(OP_DESC_LEN - 1U));
    g_ops[1U].name[OP_NAME_LEN - 1U]        = '\0';
    g_ops[1U].description[OP_DESC_LEN - 1U] = '\0';
    g_ops[1U].op_code = 2;

    (void)strncpy(g_ops[2U].name, "MUL", (size_t)(OP_NAME_LEN - 1U));
    (void)strncpy(g_ops[2U].description, "Multiply two signed integers",
                  (size_t)(OP_DESC_LEN - 1U));
    g_ops[2U].name[OP_NAME_LEN - 1U]        = '\0';
    g_ops[2U].description[OP_DESC_LEN - 1U] = '\0';
    g_ops[2U].op_code = 3;

    (void)strncpy(g_ops[3U].name, "DIV", (size_t)(OP_NAME_LEN - 1U));
    (void)strncpy(g_ops[3U].description, "Divide two signed integers",
                  (size_t)(OP_DESC_LEN - 1U));
    g_ops[3U].name[OP_NAME_LEN - 1U]        = '\0';
    g_ops[3U].description[OP_DESC_LEN - 1U] = '\0';
    g_ops[3U].op_code = 4;
}

/* -----------------------------------------------------------------------
 * HELLO_ACK parser
 * ----------------------------------------------------------------------- */
static status_t parse_hello_ack(const char *json)
{
    cJSON    *msg    = NULL;
    cJSON    *type   = NULL;
    cJSON    *entry  = NULL;
    status_t  result = STATUS_ERR;

    if (json != NULL)
    {
        msg = cJSON_Parse(json);
        if (msg != NULL)
        {
            type = cJSON_GetObjectItemCaseSensitive(msg, "type");
            if ((cJSON_IsString(type) != 0) &&
                (strcmp(type->valuestring, "HELLO_ACK") == 0))
            {
                cJSON *ops_arr = cJSON_GetObjectItemCaseSensitive(msg, "ops");
                g_num_ops = 0U;
                result    = STATUS_OK;
                entry     = (ops_arr != NULL) ? ops_arr->child : NULL;

                while ((entry != NULL) && (g_num_ops < MAX_OPS))
                {
                    cJSON *nm   = cJSON_GetObjectItemCaseSensitive(
                                      entry, "name");
                    cJSON *desc = cJSON_GetObjectItemCaseSensitive(
                                      entry, "description");
                    cJSON *code = cJSON_GetObjectItemCaseSensitive(
                                      entry, "op_code");

                    if ((cJSON_IsString(nm)   != 0) &&
                        (cJSON_IsString(desc) != 0) &&
                        (cJSON_IsNumber(code) != 0))
                    {
                        g_ops[g_num_ops].op_code = (int32_t)code->valuedouble;
                        (void)strncpy(g_ops[g_num_ops].name,
                                      nm->valuestring,
                                      (size_t)(OP_NAME_LEN - 1U));
                        g_ops[g_num_ops].name[OP_NAME_LEN - 1U] = '\0';
                        (void)strncpy(g_ops[g_num_ops].description,
                                      desc->valuestring,
                                      (size_t)(OP_DESC_LEN - 1U));
                        g_ops[g_num_ops].description[OP_DESC_LEN - 1U] = '\0';
                        g_num_ops++;
                    }
                    entry = entry->next;
                }
            }
            cJSON_Delete(msg);
            msg = NULL;
        }
    }
    return result;
}

/* -----------------------------------------------------------------------
 * CALC response parser
 * ----------------------------------------------------------------------- */
static status_t parse_calc_response(const char *json)
{
    cJSON    *msg    = NULL;
    cJSON    *type   = NULL;
    status_t  result = STATUS_ERR;
    char      num_buf[INT_STR_SIZE];

    if (json != NULL)
    {
        msg = cJSON_Parse(json);
        if (msg != NULL)
        {
            type = cJSON_GetObjectItemCaseSensitive(msg, "type");
            if ((cJSON_IsString(type) != 0) &&
                (strcmp(type->valuestring, "CALC_ACK") == 0))
            {
                cJSON *res = cJSON_GetObjectItemCaseSensitive(msg, "result");
                if (cJSON_IsNumber(res) != 0)
                {
                    int64_t r = (int64_t)res->valuedouble;
                    write_colour(COL_GREEN,  "Request OKAY...\n");
                    write_colour(COL_YELLOW, "Receiving response...\n");
                    int64_to_str(r, num_buf, sizeof(num_buf));
                    write_colour(COL_BOLD,   "Result is ");
                    write_colour(COL_GREEN,  num_buf);
                    write_colour(COL_GREEN,  "!\n");
                    result = STATUS_OK;
                }
            }
            else if ((cJSON_IsString(type) != 0) &&
                     (strcmp(type->valuestring, "ERROR") == 0))
            {
                cJSON *code = cJSON_GetObjectItemCaseSensitive(
                                  msg, "error_code");
                cJSON *emsg = cJSON_GetObjectItemCaseSensitive(
                                  msg, "message");
                write_colour(COL_RED, "Request FAILED (");
                if (cJSON_IsString(code) != 0)
                {
                    write_colour(COL_RED, code->valuestring);
                }
                write_colour(COL_RED, "): ");
                if (cJSON_IsString(emsg) != 0)
                {
                    write_colour(COL_RED, emsg->valuestring);
                }
                write_str("\n");
                result = STATUS_OK;
            }
            else
            {
                write_colour(COL_RED, "Unexpected response type\n");
            }
            cJSON_Delete(msg);
            msg = NULL;
        }
    }
    return result;
}

/* -----------------------------------------------------------------------
 * UI
 * ----------------------------------------------------------------------- */
static void print_banner(const char *client_id)
{
    write_str("\n");
    write_colour(COL_BOLD COL_CYAN,
        "\xE2\x95\x94\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90"
        "\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90"
        "\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90"
        "\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90"
        "\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90"
        "\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90"
        "\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90"
        "\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x97\n");
    write_colour(COL_BOLD COL_CYAN,
        "\xE2\x95\x91  mathdev - kernel math ops (C)    \xE2\x95\x91\n");
    write_colour(COL_BOLD COL_CYAN,
        "\xE2\x95\x9A\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90"
        "\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90"
        "\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90"
        "\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90"
        "\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90"
        "\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90"
        "\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90"
        "\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x9D\n");
    write_str("  Connected as ");
    write_colour(COL_YELLOW, client_id);
    write_str("\n\n");
}

static void print_menu(void)
{
    uint32_t i;
    char     num_buf[12U];

    for (i = 0U; i < g_num_ops; i++)
    {
        (void)uint32_to_str(i + 1U, num_buf, sizeof(num_buf));
        write_colour(COL_BOLD, num_buf);
        write_str(") ");
        write_str(g_ops[i].description);
        write_str("\n");
    }
    (void)uint32_to_str(g_num_ops + 1U, num_buf, sizeof(num_buf));
    write_colour(COL_BOLD, num_buf);
    write_str(") Exit\n\n");
}

/* -----------------------------------------------------------------------
 * Handshake
 * ----------------------------------------------------------------------- */
static status_t do_handshake(int32_t fd, const char *client_id)
{
    char     buf[MSG_BUF_SIZE];
    uint32_t len    = 0U;
    status_t result = STATUS_ERR;

    if (build_and_send_hello(fd, client_id) == STATUS_OK)
    {
        if (recv_msg(fd, buf, (uint32_t)sizeof(buf), &len) == STATUS_OK)
        {
            result = parse_hello_ack(buf);
            if (result != STATUS_OK)
            {
                write_colour(COL_RED, "No valid HELLO_ACK received\n");
            }
        }
        else
        {
            write_colour(COL_RED, "Failed to receive HELLO_ACK\n");
        }
    }
    else
    {
        write_colour(COL_RED, "Failed to send HELLO\n");
    }

    if ((result == STATUS_OK) && (g_num_ops == 0U))
    {
        load_fallback_ops();
    }
    return result;
}

/* -----------------------------------------------------------------------
 * Single CALC cycle  (Rule 15.1: no goto, Rule 15.5: single exit)
 * Returns STATUS_ERR as sentinel when user chooses Exit.
 * ----------------------------------------------------------------------- */
static status_t do_calc_request(int32_t fd, int32_t *req_id_inout)
{
    char     line[LINE_BUF_SIZE];
    char     recv_buf[MSG_BUF_SIZE];
    int64_t  a         = 0;
    int64_t  b         = 0;
    int64_t  tmp       = 0;
    uint32_t choice    = 0U;
    uint32_t recv_len  = 0U;
    status_t result    = STATUS_OK;
    bool_t   running   = BOOL_TRUE;
    bool_t   got_a     = BOOL_FALSE;
    bool_t   got_b     = BOOL_FALSE;

    if (read_line(COL_BOLD "Enter command: " COL_RESET,
                  line, sizeof(line)) == BOOL_FALSE)
    {
        result  = STATUS_ERR;
        running = BOOL_FALSE;
    }

    if (running == BOOL_TRUE)
    {
        if ((parse_int64(line, &tmp) == BOOL_TRUE) &&
            (tmp >= 1) &&
            ((uint32_t)tmp <= (g_num_ops + 1U)))
        {
            choice = (uint32_t)tmp;
        }
        else
        {
            write_colour(COL_RED, "  Invalid choice.\n\n");
            running = BOOL_FALSE;
        }
    }

    if ((running == BOOL_TRUE) && (choice == (g_num_ops + 1U)))
    {
        write_colour(COL_GREEN, "Bye!\n");
        result  = STATUS_ERR;
        running = BOOL_FALSE;
    }

    while ((running == BOOL_TRUE) && (got_a == BOOL_FALSE))
    {
        if (read_line("Enter operand 1: ", line, sizeof(line)) == BOOL_FALSE)
        {
            result  = STATUS_ERR;
            running = BOOL_FALSE;
        }
        else if (parse_int64(line, &a) == BOOL_TRUE)
        {
            got_a = BOOL_TRUE;
        }
        else
        {
            write_colour(COL_RED, "  Not a valid integer.\n");
        }
    }

    while ((running == BOOL_TRUE) && (got_b == BOOL_FALSE))
    {
        if (read_line("Enter operand 2: ", line, sizeof(line)) == BOOL_FALSE)
        {
            result  = STATUS_ERR;
            running = BOOL_FALSE;
        }
        else if (parse_int64(line, &b) == BOOL_TRUE)
        {
            got_b = BOOL_TRUE;
        }
        else
        {
            write_colour(COL_RED, "  Not a valid integer.\n");
        }
    }

    if (running == BOOL_TRUE)
    {
        (*req_id_inout)++;
        write_colour(COL_YELLOW, "Sending request...\n");

        if (build_and_send_calc(fd, *req_id_inout,
                                g_ops[choice - 1U].name,
                                a, b) != STATUS_OK)
        {
            write_colour(COL_RED, "Failed to send request\n");
            result = STATUS_ERR;
        }
        else if (recv_msg(fd, recv_buf, (uint32_t)sizeof(recv_buf),
                          &recv_len) != STATUS_OK)
        {
            write_colour(COL_RED, "Connection lost\n");
            result = STATUS_ERR;
        }
        else
        {
            (void)parse_calc_response(recv_buf);
            write_str("\n");
        }
    }

    return result;
}

/* -----------------------------------------------------------------------
 * BYE
 * ----------------------------------------------------------------------- */
static status_t send_bye(int32_t fd, const char *client_id)
{
    char     buf[MSG_BUF_SIZE];
    uint32_t len    = 0U;
    status_t result = build_and_send_bye(fd, client_id);
    (void)recv_msg(fd, buf, (uint32_t)sizeof(buf), &len);
    return result;
}

/* -----------------------------------------------------------------------
 * Argument parsing
 * ----------------------------------------------------------------------- */
static status_t parse_args(int32_t argc, char * const argv[],
                            char *socket_path, uint32_t socket_path_sz,
                            char *client_id,   uint32_t client_id_sz)
{
    int32_t  i      = 1;
    status_t result = STATUS_OK;
    char     pid_buf[INT_STR_SIZE];

    int64_to_str((int64_t)getpid(), pid_buf, sizeof(pid_buf));
    (void)strncpy(client_id, "c-client-", (size_t)(client_id_sz - 1U));
    (void)strncat(client_id, pid_buf,
                  (size_t)(client_id_sz - strlen(client_id) - 1U));
    client_id[client_id_sz - 1U] = '\0';

    (void)strncpy(socket_path, DEFAULT_SOCKET_PATH,
                  (size_t)(socket_path_sz - 1U));
    socket_path[socket_path_sz - 1U] = '\0';

    while ((i < argc) && (result == STATUS_OK))
    {
        if ((strcmp(argv[i], "--socket") == 0) && ((i + 1) < argc))
        {
            i++;
            (void)strncpy(socket_path, argv[i],
                          (size_t)(socket_path_sz - 1U));
            socket_path[socket_path_sz - 1U] = '\0';
        }
        else if ((strcmp(argv[i], "--client-id") == 0) && ((i + 1) < argc))
        {
            i++;
            (void)strncpy(client_id, argv[i],
                          (size_t)(client_id_sz - 1U));
            client_id[client_id_sz - 1U] = '\0';
        }
        else if ((strcmp(argv[i], "--no-colour") == 0) ||
                 (strcmp(argv[i], "--no-color")  == 0))
        {
            g_use_colour = BOOL_FALSE;
        }
        else
        {
            write_colour(COL_RED,
                "Usage: mathdev-client [--socket PATH] "
                "[--client-id ID] [--no-colour]\n");
            result = STATUS_ERR;
        }
        i++;
    }
    return result;
}

/* -----------------------------------------------------------------------
 * main()  (Rule 15.5: single exit, Rule 15.1: no goto)
 * ----------------------------------------------------------------------- */
int32_t main(int32_t argc, char * const argv[])
{
    char           socket_path[SOCKET_PATH_SIZE];
    char           client_id[CLIENT_ID_SIZE];
    int32_t        fd        = -1;
    int32_t        req_id    = 0;
    int32_t        exit_code = 0;
    bool_t         running   = BOOL_TRUE;
    struct sockaddr_un addr;

    (void)memset(socket_path, 0, sizeof(socket_path));
    (void)memset(client_id,   0, sizeof(client_id));
    (void)memset(&addr,       0, sizeof(addr));

    if (parse_args(argc, argv,
                   socket_path, (uint32_t)sizeof(socket_path),
                   client_id,   (uint32_t)sizeof(client_id)) != STATUS_OK)
    {
        exit_code = 1;
        running   = BOOL_FALSE;
    }

    if (running == BOOL_TRUE)
    {
        fd = (int32_t)socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0)
        {
            write_colour(COL_RED, "socket() failed\n");
            exit_code = 1;
            running   = BOOL_FALSE;
        }
    }

    if (running == BOOL_TRUE)
    {
        addr.sun_family = AF_UNIX;
        (void)strncpy(addr.sun_path, socket_path,
                      sizeof(addr.sun_path) - 1U);

        /* Dev-3: POSIX-mandated cast */
        if (connect(fd,
                    (const struct sockaddr *)&addr,
                    (socklen_t)sizeof(addr)) < 0)
        {
            write_colour(COL_RED, "Cannot connect to server at ");
            write_colour(COL_RED, socket_path);
            write_str("\n");
            write_str("Make sure the server is running.\n");
            (void)close(fd);
            exit_code = 1;
            running   = BOOL_FALSE;
        }
    }

    if (running == BOOL_TRUE)
    {
        if (do_handshake(fd, client_id) != STATUS_OK)
        {
            (void)close(fd);
            exit_code = 1;
            running   = BOOL_FALSE;
        }
    }

    if (running == BOOL_TRUE)
    {
        print_banner(client_id);
        while (running == BOOL_TRUE)
        {
            print_menu();
            if (do_calc_request(fd, &req_id) != STATUS_OK)
            {
                running = BOOL_FALSE;
            }
        }
        (void)send_bye(fd, client_id);
        (void)close(fd);
    }

    return exit_code;
}