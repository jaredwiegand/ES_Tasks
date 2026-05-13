/**
 * @file test_client_proto.c
 * @brief Unit tests for the C client protocol framing layer.
 *
 * Tests the send_msg / recv_msg framing functions using a real
 * socketpair(2) so that actual kernel socket buffering is exercised.
 * No running server or kernel module is required.
 *
 * @par Build
 * @code
 *   gcc test_client_proto.c ../../../build/_deps/cjson-src/cJSON.c \
 *       -I../../../build/_deps/cjson-src \
 *       -o test_client_proto && ./test_client_proto
 * @endcode
 *
 * Or via CMake:
 * @code
 *   cmake --build build --target test_client_proto
 * @endcode
 */

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cJSON.h"

/* ── Minimal reimplementation of send_msg / recv_msg from client.c ────────── */

/**
 * @brief Send a length-prefixed JSON message over a socket.
 *
 * Writes a 4-byte big-endian payload length followed by the JSON bytes.
 * This is a simplified reimplementation of the production send_msg() in
 * client.c, without the MISRA restrictions, used only in tests.
 *
 * @param fd        Socket file descriptor to write to.
 * @param json_str  NUL-terminated JSON string to send.
 *
 * @return 0 on success, -1 if either write call fails.
 */
static int send_msg(int fd, const char *json_str)
{
    size_t   len    = strlen(json_str);
    uint32_t netLen = htonl((uint32_t)len);  /* htonl converts host to big-endian */
    if (write(fd, &netLen, 4) != 4)    return -1;
    if (write(fd, json_str, len) != (ssize_t)len) return -1;
    return 0;
}

/**
 * @brief Receive one length-prefixed JSON message from a socket.
 *
 * Reads the 4-byte big-endian header, then reads exactly that many payload
 * bytes.  The returned buffer is heap-allocated and NUL-terminated; the
 * caller is responsible for calling free().
 *
 * @param fd  Socket file descriptor to read from.
 *
 * @return Heap-allocated NUL-terminated string on success, NULL on any
 *         I/O error or if the peer closed the connection.
 */
static char *recv_msg(int fd)
{
    uint32_t netLen;
    ssize_t  n = 0;

    /* Read exactly 4 header bytes; handles partial reads. */
    while (n < 4) {
        ssize_t r = read(fd, (char *)&netLen + n, 4 - n);
        if (r <= 0) return NULL;
        n += r;
    }
    size_t remaining = ntohl(netLen);
    if (remaining == 0) return strdup("");  /* empty payload is valid */
    char *buf = malloc(remaining + 1);
    if (!buf) return NULL;
    size_t got = 0;
    while (got < remaining) {
        n = read(fd, buf + got, remaining - got);
        if (n <= 0) { free(buf); return NULL; }
        got += (size_t)n;
    }
    buf[remaining] = '\0';
    return buf;
}

/* ── Test infrastructure ─────────────────────────────────────────────────── */

static int tests_run    = 0;  /**< Total tests attempted. */
static int tests_passed = 0;  /**< Tests that completed without assertion failures. */
static int tests_failed = 0;  /**< Tests that hit a failing ASSERT. */

/**
 * @brief Declare a test function.
 *
 * Expands to a static void function with the given name.  The body follows
 * immediately after the macro invocation.
 */
#define TEST(name) static void name(void)

/**
 * @brief Run a previously declared test function and record the result.
 *
 * Increments tests_run, executes the function, then increments tests_passed
 * if it returns normally (i.e. no ASSERT fired).
 */
#define RUN(name)  do { \
    tests_run++; \
    printf("  %-50s", #name); \
    fflush(stdout); \
    name(); \
    tests_passed++; \
    printf("PASS\n"); \
} while (0)

/**
 * @brief Assert that @p cond is true; mark the test as failed and return if not.
 *
 * The early return exits only the enclosing test function, not main().
 * tests_passed is decremented because RUN() pre-increments it optimistically.
 */
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    Assertion failed: %s  (line %d)\n", #cond, __LINE__); \
        tests_failed++; \
        tests_passed--; \
        return; \
    } \
} while (0)

/**
 * @brief Create a connected Unix-domain socket pair.
 *
 * Both ends are full-duplex stream sockets.  @p a is used as the writer
 * and @p b as the reader in most tests (though either end can do both).
 *
 * @param a  Output: first socket file descriptor.
 * @param b  Output: second socket file descriptor.
 */
static void make_pair(int *a, int *b)
{
    int fds[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    *a = fds[0];
    *b = fds[1];
}

/* ── Tests ────────────────────────────────────────────────────────────────── */

/**
 * @brief A single message sent and received equals the original string.
 */
TEST(test_simple_round_trip)
{
    int a, b;
    make_pair(&a, &b);
    const char *msg = "{\"type\":\"HELLO\",\"version\":\"1.0\"}";
    ASSERT(send_msg(a, msg) == 0);
    char *got = recv_msg(b);
    ASSERT(got != NULL);
    ASSERT(strcmp(got, msg) == 0);
    free(got);
    close(a); close(b);
}

/**
 * @brief An empty string ("") round-trips correctly (zero-length payload).
 */
TEST(test_empty_string)
{
    int a, b;
    make_pair(&a, &b);
    ASSERT(send_msg(a, "") == 0);
    char *got = recv_msg(b);
    ASSERT(got != NULL);
    ASSERT(strlen(got) == 0);
    free(got);
    close(a); close(b);
}

/**
 * @brief Three sequentially sent messages are received in FIFO order.
 *
 * Verifies that the length prefix correctly delimits back-to-back messages
 * with no interleaving.
 */
TEST(test_multiple_messages)
{
    int a, b;
    make_pair(&a, &b);
    const char *msgs[] = {
        "{\"type\":\"HELLO\"}",
        "{\"type\":\"CALC\",\"op\":\"ADD\",\"a\":1,\"b\":2}",
        "{\"type\":\"BYE\"}",
    };
    for (int i = 0; i < 3; i++)
        ASSERT(send_msg(a, msgs[i]) == 0);
    for (int i = 0; i < 3; i++) {
        char *got = recv_msg(b);
        ASSERT(got != NULL);
        ASSERT(strcmp(got, msgs[i]) == 0);
        free(got);
    }
    close(a); close(b);
}

/**
 * @brief A 64 KB payload is transmitted without corruption.
 *
 * Exercises the partial-read loop in recv_msg(), since the kernel is
 * unlikely to deliver 64 KB in a single read() call.
 */
TEST(test_large_message)
{
    int a, b;
    make_pair(&a, &b);
    size_t sz  = 65536;
    char  *buf = malloc(sz + 32);
    memset(buf + 1, 'x', sz);
    buf[0]    = '"';
    buf[sz+1] = '"';
    buf[sz+2] = '\0';
    /* Wrap the large string in a JSON object so the payload is valid JSON. */
    char *obj = malloc(sz + 64);
    snprintf(obj, sz + 64, "{\"data\":%s}", buf);
    ASSERT(send_msg(a, obj) == 0);
    char *got = recv_msg(b);
    ASSERT(got != NULL);
    ASSERT(strcmp(got, obj) == 0);
    free(got); free(buf); free(obj);
    close(a); close(b);
}

/**
 * @brief recv_msg() returns NULL when the peer closes the connection.
 *
 * Ensures the receive loop terminates cleanly on EOF rather than blocking.
 */
TEST(test_eof_returns_null)
{
    int a, b;
    make_pair(&a, &b);
    close(a);  /* close the writer; b will see EOF on next read */
    char *got = recv_msg(b);
    ASSERT(got == NULL);
    close(b);
}

/**
 * @brief cJSON correctly builds a HELLO message with all required fields.
 */
TEST(test_cjson_build_hello)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type",      "HELLO");
    cJSON_AddStringToObject(root, "version",   "1.0");
    cJSON_AddStringToObject(root, "client_id", "test-c");
    char *str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    cJSON *parsed = cJSON_Parse(str);
    ASSERT(parsed != NULL);
    cJSON *type = cJSON_GetObjectItemCaseSensitive(parsed, "type");
    ASSERT(cJSON_IsString(type));
    ASSERT(strcmp(type->valuestring, "HELLO") == 0);
    cJSON *ver = cJSON_GetObjectItemCaseSensitive(parsed, "version");
    ASSERT(strcmp(ver->valuestring, "1.0") == 0);
    free(str);
    cJSON_Delete(parsed);
}

/**
 * @brief cJSON correctly builds a CALC message with all required fields.
 */
TEST(test_cjson_build_calc)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type",   "CALC");
    cJSON_AddNumberToObject(root, "req_id", 42);
    cJSON_AddStringToObject(root, "op",     "ADD");
    cJSON_AddNumberToObject(root, "a",      10);
    cJSON_AddNumberToObject(root, "b",      20);
    char *str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    cJSON *parsed = cJSON_Parse(str);
    ASSERT(parsed != NULL);
    cJSON *op = cJSON_GetObjectItemCaseSensitive(parsed, "op");
    ASSERT(strcmp(op->valuestring, "ADD") == 0);
    cJSON *rid = cJSON_GetObjectItemCaseSensitive(parsed, "req_id");
    ASSERT((int)rid->valuedouble == 42);
    free(str);
    cJSON_Delete(parsed);
}

/**
 * @brief A CALC_ACK JSON string is parsed and the "result" field is extracted.
 */
TEST(test_cjson_parse_calc_ack)
{
    const char *json = "{\"type\":\"CALC_ACK\",\"req_id\":1,"
                       "\"op\":\"ADD\",\"a\":42,\"b\":37,\"result\":79}";
    cJSON *msg = cJSON_Parse(json);
    ASSERT(msg != NULL);
    cJSON *type   = cJSON_GetObjectItemCaseSensitive(msg, "type");
    cJSON *result = cJSON_GetObjectItemCaseSensitive(msg, "result");
    ASSERT(strcmp(type->valuestring, "CALC_ACK") == 0);
    ASSERT((long long)result->valuedouble == 79LL);
    cJSON_Delete(msg);
}

/**
 * @brief An ERROR JSON string is parsed and the "error_code" field is extracted.
 */
TEST(test_cjson_parse_error)
{
    const char *json = "{\"type\":\"ERROR\",\"error_code\":\"ERR_DIV_ZERO\","
                       "\"message\":\"Division by zero is undefined\"}";
    cJSON *msg = cJSON_Parse(json);
    ASSERT(msg != NULL);
    cJSON *code = cJSON_GetObjectItemCaseSensitive(msg, "error_code");
    ASSERT(strcmp(code->valuestring, "ERR_DIV_ZERO") == 0);
    cJSON_Delete(msg);
}

/**
 * @brief The 4-byte length prefix is encoded in big-endian (network) byte order.
 *
 * Reads the raw header bytes and reconstructs the length manually to verify
 * the byte ordering, independent of the htonl() / ntohl() used in the
 * implementation.
 */
TEST(test_length_prefix_big_endian)
{
    int a, b;
    make_pair(&a, &b);
    const char *msg = "{\"x\":1}";
    send_msg(a, msg);

    /* Read raw header bytes and verify big-endian order. */
    uint8_t header[4];
    ssize_t n = 0;
    while (n < 4) n += read(b, header + n, 4 - n);

    /* Reconstruct length from individual bytes to avoid relying on ntohl. */
    uint32_t length = ((uint32_t)header[0] << 24) |
                      ((uint32_t)header[1] << 16) |
                      ((uint32_t)header[2] <<  8) |
                      ((uint32_t)header[3]);
    ASSERT(length == strlen(msg));

    /* Drain the body to leave the socket clean. */
    char *body = malloc(length + 1);
    n = 0;
    while ((size_t)n < length) n += read(b, body + n, length - n);
    body[length] = '\0';
    ASSERT(strcmp(body, msg) == 0);
    free(body);
    close(a); close(b);
}

/* ── main ─────────────────────────────────────────────────────────────────── */

/**
 * @brief Test runner entry point.
 *
 * Executes all registered tests in order and prints a summary.
 *
 * @return 0 if all tests passed, 1 if any test failed.
 */
int main(void)
{
    printf("\nmathdev C client protocol tests\n");
    printf("================================\n\n");

    RUN(test_simple_round_trip);
    RUN(test_empty_string);
    RUN(test_multiple_messages);
    RUN(test_large_message);
    RUN(test_eof_returns_null);
    RUN(test_cjson_build_hello);
    RUN(test_cjson_build_calc);
    RUN(test_cjson_parse_calc_ack);
    RUN(test_cjson_parse_error);
    RUN(test_length_prefix_big_endian);

    printf("\n================================\n");
    printf("Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf("  (%d FAILED)", tests_failed);
    printf("\n\n");

    return tests_failed > 0 ? 1 : 0;
}
