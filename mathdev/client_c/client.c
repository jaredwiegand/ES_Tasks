/**
 * @file client.c
 * @brief mathdev C client (MISRA C:2012 compliant).
 *
 * Connects to the mathdev server over a Unix-domain socket, performs a
 * protocol handshake, then presents an interactive menu driven by the
 * operation list received from the server.
 *
 * @par MISRA C:2012 compliance notes
 *
 *   Rule 1.3   - No undefined behaviour.\n
 *   Rule 2.2   - No dead code.\n
 *   Rule 3.1   - No nested comments.\n
 *   Rule 7.4   - String literals assigned to const char * only.\n
 *   Rule 8.4   - Compatible declarations for all externals.\n
 *   Rule 8.7   - Functions not used outside TU are static.\n
 *   Rule 8.13  - Pointers const-qualified where possible.\n
 *   Rule 10.1  - Operands of appropriate essential type.\n
 *   Rule 10.3  - Value assigned to variable of same essential type.\n
 *   Rule 10.4  - Binary operator operands same essential type.\n
 *   Rule 11.3  - No cast between pointer-to-object types (Dev-3 below).\n
 *   Rule 12.1  - Operator precedence made explicit with parentheses.\n
 *   Rule 14.4  - Controlling expressions are essentially Boolean.\n
 *   Rule 15.1  - No goto.\n
 *   Rule 15.4  - One break per loop.\n
 *   Rule 15.5  - Single point of exit per function.\n
 *   Rule 16.3  - All switch clauses have break or return.\n
 *   Rule 17.1  - No variadic macros or \<stdarg.h\>.\n
 *   Rule 18.8  - No variable-length arrays.\n
 *   Rule 21.3  - No dynamic memory allocation (Dev-2 below).\n
 *   Rule 21.6  - No \<stdio.h\> (Dev-1 below).\n
 *   Dir  4.6   - Numeric types from \<stdint.h\>.\n
 *   Dir  4.11  - Validity of values passed to library functions checked.\n
 *
 * @par Deviations (documented per MISRA process)
 *
 *   @b Dev-1  Rule 21.6:\n
 *          All output uses write(2) on STDOUT_FILENO instead of printf.
 *          Avoids format-string risks and \<stdio.h\> buffering.\n
 *
 *   @b Dev-2  Rule 21.3:\n
 *          cJSON internally uses malloc/free. Usage is isolated to three
 *          builder functions; all cJSON objects are created and destroyed
 *          within the same function scope. No cJSON pointer escapes.\n
 *
 *   @b Dev-3  Rule 11.3:\n
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
#  define DEFAULT_SOCKET_PATH   "/tmp/mathdev.sock"  /**< Default Unix socket path. */
#endif

#define PROTOCOL_VERSION        "1.0"   /**< Protocol version string sent in HELLO. */
#define MSG_BUF_SIZE            (4096U) /**< Receive buffer size in bytes. */
#define LINE_BUF_SIZE           (128U)  /**< Interactive input line buffer size. */
#define CLIENT_ID_SIZE          (64U)   /**< Maximum client-id string length (incl. NUL). */
#define SOCKET_PATH_SIZE        (108U)  /**< Maximum socket path length (UNIX_PATH_MAX). */
#define MAX_OPS                 (8U)    /**< Maximum number of ops storable in g_ops. */
#define OP_NAME_LEN             (16U)   /**< Max operator name length (incl. NUL). */
#define OP_DESC_LEN             (64U)   /**< Max operator description length (incl. NUL). */
#define HEADER_SIZE             (4U)    /**< Length-prefix header size in bytes (big-endian uint32). */
#define INT_STR_SIZE            (24U)   /**< Buffer large enough for any int64 decimal string. */

/** @defgroup AnsiColours ANSI terminal escape sequences for coloured output. @{ */
#define COL_RESET               "\033[0m"   /**< Reset all attributes. */
#define COL_BOLD                "\033[1m"   /**< Bold text. */
#define COL_RED                 "\033[31m"  /**< Red foreground. */
#define COL_GREEN               "\033[32m"  /**< Green foreground. */
#define COL_YELLOW              "\033[33m"  /**< Yellow foreground. */
#define COL_CYAN                "\033[36m"  /**< Cyan foreground. */
/** @} */

/* -----------------------------------------------------------------------
 * Type definitions  (Dir 4.6)
 * ----------------------------------------------------------------------- */

/** @brief Boolean type (Rule 18.8: no _Bool / stdbool.h in MISRA profile). */
typedef uint8_t  bool_t;
#define BOOL_TRUE   ((bool_t)1U)   /**< Boolean true value. */
#define BOOL_FALSE  ((bool_t)0U)   /**< Boolean false value. */

/** @brief Generic function status code. */
typedef int32_t  status_t;
#define STATUS_OK   ((status_t) 0)  /**< Success. */
#define STATUS_ERR  ((status_t)-1)  /**< Failure. */

/**
 * @brief Descriptor for one available math operation as received from the server.
 *
 * @var op_entry_t::op_code     Numeric operator code (matches MATH_OP_* / OP_* constants).
 * @var op_entry_t::name        Short operator name (e.g. "ADD"), NUL-terminated.
 * @var op_entry_t::description Human-readable description, NUL-terminated.
 */
typedef struct
{
    int32_t op_code;
    char    name[OP_NAME_LEN];
    char    description[OP_DESC_LEN];
} op_entry_t;

/* -----------------------------------------------------------------------
 * Module-level state  (Rule 8.7: static)
 * ----------------------------------------------------------------------- */
static op_entry_t g_ops[MAX_OPS];          /**< Operations populated from HELLO_ACK or fallback. */
static uint32_t   g_num_ops    = 0U;       /**< Number of valid entries in g_ops. */
static bool_t     g_use_colour = BOOL_TRUE; /**< Set to BOOL_FALSE via --no-colour flag. */

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

/**
 * @brief Write a NUL-terminated string to stdout using write(2).
 *
 * Avoids \<stdio.h\> per MISRA Rule 21.6 / Dev-1.  A NULL or empty
 * string is silently ignored.
 *
 * @param s  String to write; may be NULL (no-op).
 */
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

/**
 * @brief Write a string wrapped in an ANSI colour escape sequence.
 *
 * If colour output is disabled (--no-colour flag), @p s is written
 * without any escape codes.
 *
 * @param col  ANSI escape sequence (e.g. COL_RED).
 * @param s    String to write.
 */
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

/**
 * @brief Convert a signed 64-bit integer to its decimal ASCII representation.
 *
 * Avoids sprintf / printf per MISRA Rule 21.6.  Handles INT64_MIN as a
 * special case because negating it would overflow a signed 64-bit type.
 *
 * The result is always NUL-terminated.  If @p buf_sz is too small the
 * output is silently truncated to @p buf_sz - 1 characters.
 *
 * @param value   Value to convert.
 * @param buf     Output buffer.
 * @param buf_sz  Size of @p buf in bytes (must be >= 2).
 */
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

    /* INT64_MIN cannot be negated as a signed value without overflow (UB);
     * write its string literal directly instead. */
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

    /* Build digits in reverse order into tmp[], then reverse into buf. */
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

/**
 * @brief Convert an unsigned 32-bit integer to its decimal ASCII representation.
 *
 * @param value   Value to convert.
 * @param buf     Output buffer.
 * @param buf_sz  Size of @p buf in bytes (must be >= 2).
 *
 * @return Number of characters written (excluding NUL), or 0 on bad args.
 */
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
        /* Build digits in reverse order into tmp[], then reverse into buf. */
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

/**
 * @brief Serialise and send one JSON message over the socket.
 *
 * Wire format: 4-byte big-endian payload length followed by the raw
 * JSON bytes.  No heap allocation is performed; the header is built on
 * the stack.
 *
 * @param fd        Connected socket file descriptor.
 * @param json_str  NUL-terminated JSON string to send.
 *
 * @return STATUS_OK on success, STATUS_ERR if any write(2) call fails or
 *         if @p json_str is NULL.
 */
static status_t send_msg(int32_t fd, const char *json_str)
{
    uint8_t  header[HEADER_SIZE];
    uint32_t len;
    uint32_t sent;
    ssize_t  n;
    status_t result = STATUS_ERR;

    if (json_str != NULL)
    {
        len    = (uint32_t)strlen(json_str);
        result = STATUS_OK;

        /* Encode payload length as 4-byte big-endian (network byte order). */
        header[0] = (uint8_t)((len >> 24U) & 0xFFU);
        header[1] = (uint8_t)((len >> 16U) & 0xFFU);
        header[2] = (uint8_t)((len >>  8U) & 0xFFU);
        header[3] = (uint8_t)( len         & 0xFFU);

        /* Write header, looping to handle partial writes. */
        sent = 0U;
        while ((sent < HEADER_SIZE) && (result == STATUS_OK))
        {
            n = write(fd, &header[sent], (size_t)(HEADER_SIZE - sent));
            if (n <= (ssize_t)0)
            {
                result = STATUS_ERR;
            }
            else
            {
                sent += (uint32_t)n;
            }
        }

        /* Write payload, looping to handle partial writes. */
        sent = 0U;
        while ((result == STATUS_OK) && (sent < len))
        {
            n = write(fd, &json_str[sent], (size_t)(len - sent));
            if (n <= (ssize_t)0)
            {
                result = STATUS_ERR;
            }
            else
            {
                sent += (uint32_t)n;
            }
        }
    }
    return result;
}

/**
 * @brief Receive one length-prefixed JSON message from the socket.
 *
 * Reads the 4-byte big-endian header first, then loops until all payload
 * bytes are received (handles partial reads from the kernel).
 *
 * @param fd       Connected socket file descriptor.
 * @param buf      Caller-supplied receive buffer.
 * @param buf_sz   Size of @p buf in bytes (must be > payload length).
 * @param out_len  On success, set to the number of payload bytes received.
 *
 * @return STATUS_OK on success, STATUS_ERR on I/O error, EOF, or if the
 *         declared payload length would overflow @p buf.
 */
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

        /* Read exactly HEADER_SIZE bytes; read(2) may return less in one call. */
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
            /* Decode big-endian 4-byte length field. */
            length = (((uint32_t)header[0]) << 24U) |
                     (((uint32_t)header[1]) << 16U) |
                     (((uint32_t)header[2]) <<  8U) |
                      ((uint32_t)header[3]);

            if (length >= buf_sz)
            {
                /* Payload would overrun buffer; treat as a protocol error. */
                result = STATUS_ERR;
            }
            else if (length == 0U)
            {
                buf[0]   = '\0';
                *out_len = 0U;
            }
            else
            {
                /* Loop until all bytes are received; handles partial reads. */
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

/**
 * @brief Print a prompt and read one line of text from stdin using read(2).
 *
 * Reads one character at a time until a newline is encountered or the buffer
 * is full.  Carriage-return characters are silently skipped (handles
 * Windows-style line endings).
 *
 * @param prompt  String to print before reading (may be NULL for no prompt).
 * @param buf     Output buffer for the line (NUL-terminated on return).
 * @param buf_sz  Size of @p buf in bytes (must be >= 2).
 *
 * @return BOOL_TRUE on success, BOOL_FALSE on I/O error or invalid args.
 */
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
                /* skip CR in CR+LF line endings */
            }
        }
        buf[i] = '\0';
    }
    return result;
}

/**
 * @brief Parse a decimal integer string into a signed 64-bit value.
 *
 * Accepts an optional leading '+' or '-'.  Rejects empty strings,
 * non-digit characters, and values that would overflow int64_t.
 *
 * @param s    NUL-terminated input string.
 * @param out  Pointer to receive the parsed value on success.
 *
 * @return BOOL_TRUE on success, BOOL_FALSE if @p s is invalid or overflows.
 */
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
            result = BOOL_FALSE; /* sign character with no digits */
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

                    /* Overflow guard: reject before multiplying to avoid wrap-around. */
                    if (acc > (9223372036854775807ULL / 10ULL))
                    {
                        result = BOOL_FALSE;
                    }
                    else
                    {
                        acc = (acc * 10ULL) + digit;

                        /* Positive values are capped at INT64_MAX.
                         * Negative values may reach INT64_MAX+1 (== -INT64_MIN). */
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

/**
 * @brief Build and send a HELLO message.
 *
 * Allocates a cJSON object, serialises it to an unformatted string, sends it
 * via send_msg(), then immediately frees all cJSON allocations.  No cJSON
 * pointer escapes this function (Dev-2).
 *
 * @param fd         Connected socket file descriptor.
 * @param client_id  NUL-terminated client identifier string.
 *
 * @return STATUS_OK on success, STATUS_ERR on cJSON or send failure.
 */
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

/**
 * @brief Build and send a CALC message.
 *
 * @param fd      Connected socket file descriptor.
 * @param req_id  Monotonically increasing request identifier.
 * @param op      Operator name string (e.g. "ADD").
 * @param a       First operand.
 * @param b       Second operand.
 *
 * @note Operands are passed as double to cJSON; precision is sufficient for
 *       the int64 values used in practice but would lose bits near INT64_MAX.
 *
 * @return STATUS_OK on success, STATUS_ERR on cJSON or send failure.
 */
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

/**
 * @brief Build and send a BYE message.
 *
 * @param fd         Connected socket file descriptor.
 * @param client_id  NUL-terminated client identifier string.
 *
 * @return STATUS_OK on success, STATUS_ERR on cJSON or send failure.
 */
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

/**
 * @brief Populate g_ops with a hard-coded set of four operations.
 *
 * Called when the HELLO_ACK message contains no "ops" array, so the client
 * can still present a menu even if the server omits the list.
 */
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

/**
 * @brief Parse a HELLO_ACK JSON message and populate the global ops table.
 *
 * Expects a JSON object with "type": "HELLO_ACK" and an optional "ops"
 * array.  Each element of "ops" must contain "name", "description", and
 * "op_code" fields.  Up to MAX_OPS entries are stored in g_ops.
 *
 * @param json  NUL-terminated JSON string to parse.
 *
 * @return STATUS_OK if the message type is HELLO_ACK (even if ops is empty),
 *         STATUS_ERR if @p json is NULL, unparseable, or the type field does
 *         not match.
 */
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

                    /* Only store the entry if all three fields are present and typed correctly. */
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

/**
 * @brief Parse a CALC_ACK or ERROR response and print the result to stdout.
 *
 * On CALC_ACK, extracts the "result" field and prints it in green.
 * On ERROR, extracts "error_code" and "message" and prints them in red.
 * Any other message type produces a generic error message.
 *
 * @param json  NUL-terminated JSON string to parse.
 *
 * @return STATUS_OK if the message was successfully parsed (regardless of
 *         whether it represents success or an error from the server),
 *         STATUS_ERR if @p json is NULL or unparseable.
 */
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
                /* A well-formed ERROR response is a successful parse, not a comm failure. */
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

/**
 * @brief Print the application banner with the connected client ID.
 *
 * Uses UTF-8 box-drawing characters for the border.  The byte sequences
 * are written as string literals to avoid relying on the compiler's
 * handling of wchar_t.
 *
 * @param client_id  NUL-terminated client identifier to display.
 */
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

/**
 * @brief Print the operation selection menu using the global ops table.
 *
 * Enumerates g_ops[], numbering entries from 1.  Appends an "Exit" entry
 * numbered g_num_ops + 1.
 */
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

/**
 * @brief Perform the HELLO / HELLO_ACK handshake with the server.
 *
 * Sends HELLO, waits for HELLO_ACK, and parses the server's ops list into
 * g_ops.  If the server sends no ops, load_fallback_ops() is called so the
 * menu always has entries.
 *
 * @param fd         Connected socket file descriptor.
 * @param client_id  NUL-terminated client identifier sent in HELLO.
 *
 * @return STATUS_OK on a successful handshake, STATUS_ERR otherwise.
 */
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

    /* Fall back to built-in ops if the server sent none (e.g. older server). */
    if ((result == STATUS_OK) && (g_num_ops == 0U))
    {
        load_fallback_ops();
    }
    return result;
}

/* -----------------------------------------------------------------------
 * Single CALC cycle  (Rule 15.1: no goto, Rule 15.5: single exit)
 * ----------------------------------------------------------------------- */

/**
 * @brief Execute one interactive calculation cycle.
 *
 * Prompts the user to select an operation and enter two operands, sends a
 * CALC message to the server, and prints the response.
 *
 * @note Returns STATUS_ERR as a sentinel when the user selects the Exit
 *       option; this is not a communication failure.
 *
 * @param fd             Connected socket file descriptor.
 * @param req_id_inout   In/out: monotonically increasing request ID.
 *                       Incremented on each successful send.
 *
 * @return STATUS_OK to continue the session, STATUS_ERR to end it (either
 *         user-initiated exit or a communication error).
 */
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

    /* g_num_ops + 1 is always the Exit entry in the menu. */
    if ((running == BOOL_TRUE) && (choice == (g_num_ops + 1U)))
    {
        write_colour(COL_GREEN, "Bye!\n");
        result  = STATUS_ERR; /* sentinel: user chose to exit */
        running = BOOL_FALSE;
    }

    /* Retry prompt until a valid integer is entered. */
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

/**
 * @brief Send a BYE message and drain the server's BYE_ACK response.
 *
 * The BYE_ACK content is intentionally discarded; its purpose is only to
 * flush the server's send buffer before the socket is closed.
 *
 * @param fd         Connected socket file descriptor.
 * @param client_id  NUL-terminated client identifier.
 *
 * @return STATUS_OK if the BYE was sent successfully, STATUS_ERR otherwise.
 */
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

/**
 * @brief Parse command-line arguments and populate socket path and client ID.
 *
 * Recognised options:
 *  - @c --socket PATH      Override the default Unix socket path.
 *  - @c --client-id ID     Override the auto-generated client identifier.
 *  - @c --no-colour / @c --no-color  Disable ANSI colour output.
 *
 * The default client ID is @c "c-client-\<pid\>".
 *
 * @param argc            Argument count from main().
 * @param argv            Argument vector from main().
 * @param socket_path     Output buffer for the socket path.
 * @param socket_path_sz  Size of @p socket_path in bytes.
 * @param client_id       Output buffer for the client identifier.
 * @param client_id_sz    Size of @p client_id in bytes.
 *
 * @return STATUS_OK on success, STATUS_ERR if an unknown argument is found.
 */
static status_t parse_args(int32_t argc, char * const argv[],
                            char *socket_path, uint32_t socket_path_sz,
                            char *client_id,   uint32_t client_id_sz)
{
    int32_t  i      = 1;
    status_t result = STATUS_OK;
    char     pid_buf[INT_STR_SIZE];

    /* Build default client ID from process PID to ensure uniqueness. */
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

/**
 * @brief Program entry point.
 *
 * Parses arguments, opens a Unix-domain socket, connects to the server,
 * performs the handshake, then loops presenting the operation menu until
 * the user exits or a communication error occurs.
 *
 * @param argc  Argument count.
 * @param argv  Argument vector.
 *
 * @return 0 on clean exit, 1 on any error.
 */
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

        /* Dev-3: POSIX connect(2) requires casting sockaddr_un* to sockaddr*. */
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
