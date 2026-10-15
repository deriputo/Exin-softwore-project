#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#define PROTOCOL_LINE_CAPACITY 96u
#define PROTOCOL_QUEUE_CAPACITY 8u
#define PROTOCOL_REPLY_CAPACITY 384u

typedef enum {
    PROTOCOL_CMD_INVALID = 0,
    PROTOCOL_CMD_HELP,
    PROTOCOL_CMD_TIME_SET,
    PROTOCOL_CMD_ALARM_SET,
    PROTOCOL_CMD_ALARM_OFF,
    PROTOCOL_CMD_ALARM_DELETE,
    PROTOCOL_CMD_ALARM_QUERY,
    PROTOCOL_CMD_IMU_QUERY,
    PROTOCOL_CMD_SERVO_QUERY,
    PROTOCOL_CMD_SERVO_SET,
    PROTOCOL_CMD_HOLD_ON,
    PROTOCOL_CMD_HOLD_OFF,
    PROTOCOL_CMD_HOLD_QUERY,
    PROTOCOL_CMD_TRACE_ON,
    PROTOCOL_CMD_TRACE_OFF,
    PROTOCOL_CMD_TRACE_QUERY,
    PROTOCOL_CMD_IMU_RAW_QUERY
} ProtocolCommandType;

typedef struct {
    ProtocolCommandType type;
    uint8_t alarm_number;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    int16_t angle_deg;
} ProtocolCommand;

typedef enum { PROTOCOL_DONE = 0, PROTOCOL_WAIT = 1 } ProtocolHandlerResult;
/* DONE: NUL-terminated complete response including CRLF, cached until transmitted.
   WAIT: called again with first_call=0; no later commands overtake this command.
   TIME SET must return WAIT until RTCClock is READY after committing the write. */
typedef ProtocolHandlerResult (*ProtocolHandler)(const ProtocolCommand *command,
    uint8_t first_call, uint32_t now_ms, char *reply, size_t capacity, void *context);
/* All-or-none, nonblocking writer; return 0 to retry unchanged response later. */
typedef uint8_t (*ProtocolWriter)(const char *bytes, uint16_t length, void *context);

typedef struct {
    ProtocolCommand queue[PROTOCOL_QUEUE_CAPACITY];
    char line[PROTOCOL_LINE_CAPACITY];
    char reply[PROTOCOL_REPLY_CAPACITY];
    ProtocolHandler handler;
    ProtocolWriter writer;
    void *context;
    uint32_t dropped_lines;
    uint32_t pending_overflow;
    uint8_t queue_head;
    uint8_t queue_tail;
    uint8_t queue_count;
    uint8_t line_length;
    uint8_t line_invalid;
    uint8_t previous_cr;
    uint8_t command_started;
    uint8_t reply_ready;
    uint8_t overflow_blocked;
    uint8_t sending_overflow;
} Protocol;

/* Exact uppercase grammar except HELP/help; no trimming/aliases/trailing junk. */
uint8_t Protocol_Parse(const char *line, ProtocolCommand *command);
void Protocol_Init(Protocol *protocol, ProtocolHandler handler, ProtocolWriter writer, void *context);
/* Feed only from the main loop. CR, LF and CRLF terminate a line; blank lines ignored. */
void Protocol_FeedByte(Protocol *protocol, uint8_t byte);
/* Mark current incoming command invalid after USART overrun/transport byte loss. */
void Protocol_InvalidateInput(Protocol *protocol);
/* Bounded work: one handler invocation or one response transmission per call. */
void Protocol_Process(Protocol *protocol, uint32_t now_ms);
uint8_t Protocol_IsIdle(const Protocol *protocol);
uint8_t Protocol_IsBusy(const Protocol *protocol);
uint8_t Protocol_IsWaiting(const Protocol *protocol);
/* Fixed-point helpers avoid printf floating-point linkage on the embedded target. */
int32_t Protocol_FloatToTenths(float value);
uint8_t Protocol_FormatTenths(char *out, size_t capacity, int32_t value, uint8_t force_sign);

#endif
