/*
 * protocol.h  –  mathdev C protocol constants
 * Mirrors proto/protocol.py
 */

#ifndef MATHDEV_PROTOCOL_H
#define MATHDEV_PROTOCOL_H

#define PROTO_VERSION    "1.0"

/* Message type strings */
#define MSG_HELLO        "HELLO"
#define MSG_HELLO_ACK    "HELLO_ACK"
#define MSG_CALC         "CALC"
#define MSG_CALC_ACK     "CALC_ACK"
#define MSG_ERROR        "ERROR"
#define MSG_BYE          "BYE"
#define MSG_BYE_ACK      "BYE_ACK"

/* Error code strings */
#define ERR_UNKNOWN_OP   "ERR_UNKNOWN_OP"
#define ERR_DIV_ZERO     "ERR_DIV_ZERO"
#define ERR_BAD_REQUEST  "ERR_BAD_REQUEST"
#define ERR_DEVICE       "ERR_DEVICE"
#define ERR_INTERNAL     "ERR_INTERNAL"

/* Operator code values (must match kernel MATH_OP_*) */
#define OP_ADD  1
#define OP_SUB  2
#define OP_MUL  3
#define OP_DIV  4

#endif /* MATHDEV_PROTOCOL_H */
