/**
 * @file protocol.h
 * @brief mathdev C client protocol constants.
 *
 * Mirrors the definitions in proto/protocol.py.  Both the C client and the
 * Python server use these string tokens to identify message types and error
 * codes exchanged over the Unix-domain socket.
 *
 * Wire format: each message is preceded by a 4-byte big-endian length header
 * followed by a UTF-8 JSON payload of that many bytes.
 */

#ifndef MATHDEV_PROTOCOL_H
#define MATHDEV_PROTOCOL_H

/** @brief Protocol version string sent in the HELLO handshake. */
#define PROTO_VERSION    "1.0"

/**
 * @defgroup MsgTypes Message type strings
 * @brief JSON "type" field values used in every protocol message.
 * @{
 */
#define MSG_HELLO        "HELLO"      /**< Client → server: initiate session. */
#define MSG_HELLO_ACK    "HELLO_ACK"  /**< Server → client: session accepted; includes ops list. */
#define MSG_CALC         "CALC"       /**< Client → server: submit a calculation request. */
#define MSG_CALC_ACK     "CALC_ACK"   /**< Server → client: calculation result. */
#define MSG_ERROR        "ERROR"      /**< Server → client: request failed; includes error_code. */
#define MSG_BYE          "BYE"        /**< Client → server: graceful disconnect. */
#define MSG_BYE_ACK      "BYE_ACK"   /**< Server → client: disconnect acknowledged. */
/** @} */

/**
 * @defgroup ErrCodes Error code strings
 * @brief Values for the "error_code" field in an ERROR message.
 * @{
 */
#define ERR_UNKNOWN_OP   "ERR_UNKNOWN_OP"   /**< The requested operator is not recognised. */
#define ERR_DIV_ZERO     "ERR_DIV_ZERO"     /**< Division by zero was attempted. */
#define ERR_BAD_REQUEST  "ERR_BAD_REQUEST"  /**< Malformed or missing JSON fields. */
#define ERR_DEVICE       "ERR_DEVICE"       /**< Kernel device returned an error. */
#define ERR_INTERNAL     "ERR_INTERNAL"     /**< Unexpected server-side error. */
/** @} */

/**
 * @defgroup OpCodes Operator code values
 * @brief Numeric operator codes placed in the "op_code" field of each op entry.
 *
 * These must match the MATH_OP_* constants defined in the kernel's mathdev.h.
 * @{
 */
#define OP_ADD  1   /**< Addition operator code. */
#define OP_SUB  2   /**< Subtraction operator code. */
#define OP_MUL  3   /**< Multiplication operator code. */
#define OP_DIV  4   /**< Division operator code. */
/** @} */

#endif /* MATHDEV_PROTOCOL_H */
