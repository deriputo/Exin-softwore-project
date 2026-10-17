#include "protocol.h"
#include <string.h>
#include <stdio.h>

static uint8_t Digit(char c) { return (uint8_t)(c >= '0' && c <= '9'); }

static uint8_t ParseTime(const char *text, ProtocolCommand *command)
{
    if (strlen(text) != 10u || text[0] != '(' || text[3] != ':' ||
        text[6] != ':' || text[9] != ')' || !Digit(text[1]) || !Digit(text[2]) ||
        !Digit(text[4]) || !Digit(text[5]) || !Digit(text[7]) || !Digit(text[8])) {
        return 0;
    }
    command->hour = (uint8_t)((text[1] - '0') * 10 + text[2] - '0');
    command->minute = (uint8_t)((text[4] - '0') * 10 + text[5] - '0');
    command->second = (uint8_t)((text[7] - '0') * 10 + text[8] - '0');
    return (uint8_t)(command->hour < 24 && command->minute < 60 && command->second < 60);
}

uint8_t Protocol_Parse(const char *line, ProtocolCommand *command)
{
    const char *p;
    uint16_t angle = 0;
    uint8_t digits = 0;
    size_t length;
    if (!line || !command) { return 0; }
    memset(command, 0, sizeof(*command));
    length = strlen(line);
    if (strcmp(line, "HELP") == 0 || strcmp(line, "help") == 0) {
        command->type = PROTOCOL_CMD_HELP;
    } else if (strcmp(line, "SYS RESET ?") == 0) {
        command->type = PROTOCOL_CMD_SYS_RESET_QUERY;
    } else if (length >= 4u && strncmp(line, "TIME", 4) == 0 && ParseTime(line + 4, command)) {
        command->type = PROTOCOL_CMD_TIME_SET;
    } else if (strcmp(line, "ALARM ?") == 0) {
        command->type = PROTOCOL_CMD_ALARM_QUERY;
    } else if (length >= 6u && strncmp(line, "ALARM", 5) == 0 && line[5] >= '1' && line[5] <= '3') {
        command->alarm_number = (uint8_t)(line[5] - '0');
        if (ParseTime(line + 6, command)) {
            command->type = PROTOCOL_CMD_ALARM_SET;
        } else if (strcmp(line + 6, " OFF") == 0) {
            command->type = PROTOCOL_CMD_ALARM_OFF;
        } else if (strcmp(line + 6, " DEL") == 0) {
            command->type = PROTOCOL_CMD_ALARM_DELETE;
        }
    } else if (strcmp(line, "IMU ?") == 0) {
        command->type = PROTOCOL_CMD_IMU_QUERY;
    } else if (strcmp(line, "IMU RAW ?") == 0) {
        command->type = PROTOCOL_CMD_IMU_RAW_QUERY;
    } else if (strcmp(line, "IMU FAULT ?") == 0) {
        command->type = PROTOCOL_CMD_IMU_FAULT_QUERY;
    } else if (strcmp(line, "SERVO ?") == 0) {
        command->type = PROTOCOL_CMD_SERVO_QUERY;
    } else if (length > 10u && strncmp(line, "SERVO SET ", 10) == 0) {
        p = line + 10;
        while (*p && digits < 3u && Digit(*p)) {
            angle = (uint16_t)(angle * 10u + (uint16_t)(*p - '0'));
            ++p;
            ++digits;
        }
        if (*p == '\0' && digits && angle <= 180u) {
            command->angle_deg = (int16_t)angle;
            command->type = PROTOCOL_CMD_SERVO_SET;
        }
    } else if (strcmp(line, "SERVO HOLD ON") == 0) {
        command->type = PROTOCOL_CMD_HOLD_ON;
    } else if (strcmp(line, "SERVO HOLD OFF") == 0) {
        command->type = PROTOCOL_CMD_HOLD_OFF;
    } else if (strcmp(line, "SERVO HOLD ?") == 0) {
        command->type = PROTOCOL_CMD_HOLD_QUERY;
    } else if (strcmp(line, "SERVO TRACE ON") == 0) {
        command->type = PROTOCOL_CMD_TRACE_ON;
    } else if (strcmp(line, "SERVO TRACE OFF") == 0) {
        command->type = PROTOCOL_CMD_TRACE_OFF;
    } else if (strcmp(line, "SERVO TRACE ?") == 0) {
        command->type = PROTOCOL_CMD_TRACE_QUERY;
    }
    return (uint8_t)(command->type != PROTOCOL_CMD_INVALID);
}

void Protocol_Init(Protocol *protocol, ProtocolHandler handler, ProtocolWriter writer, void *context)
{
    if (!protocol) { return; }
    memset(protocol, 0, sizeof(*protocol));
    protocol->handler = handler;
    protocol->writer = writer;
    protocol->context = context;
}

static void FinishLine(Protocol *protocol)
{
    ProtocolCommand command;
    if (!protocol->line_length && !protocol->line_invalid) { return; }
    memset(&command, 0, sizeof(command));
    protocol->line[protocol->line_length] = '\0';
    if (!protocol->line_invalid) { (void)Protocol_Parse(protocol->line, &command); }
    if (protocol->queue_count == PROTOCOL_QUEUE_CAPACITY || protocol->overflow_blocked) {
        protocol->overflow_blocked = 1;
        if (protocol->dropped_lines != UINT32_MAX) { ++protocol->dropped_lines; }
        if (protocol->pending_overflow != UINT32_MAX) { ++protocol->pending_overflow; }
    } else {
        protocol->queue[protocol->queue_head] = command;
        protocol->queue_head = (uint8_t)((protocol->queue_head + 1u) % PROTOCOL_QUEUE_CAPACITY);
        ++protocol->queue_count;
    }
    protocol->line_length = 0;
    protocol->line_invalid = 0;
}

void Protocol_FeedByte(Protocol *protocol, uint8_t byte)
{
    if (!protocol) { return; }
    if (byte == '\n' && protocol->previous_cr) {
        protocol->previous_cr = 0;
        return;
    }
    protocol->previous_cr = (uint8_t)(byte == '\r');
    if (byte == '\n' || byte == '\r') {
        FinishLine(protocol);
    } else if (byte < 32u || byte > 126u) {
        protocol->line_invalid = 1;
    } else if (protocol->line_length + 1u < PROTOCOL_LINE_CAPACITY) {
        protocol->line[protocol->line_length++] = (char)byte;
    } else {
        /* Drop remainder until terminator; never parse a truncated command. */
        protocol->line_invalid = 1;
    }
}

void Protocol_InvalidateInput(Protocol *protocol)
{
    if (protocol) { protocol->line_invalid = 1; }
}

static uint8_t SendReply(Protocol *protocol)
{
    size_t length = strlen(protocol->reply);
    return (uint8_t)(length && protocol->writer &&
        protocol->writer(protocol->reply, (uint16_t)length, protocol->context));
}

void Protocol_Process(Protocol *protocol, uint32_t now_ms)
{
    ProtocolHandlerResult result;
    uint8_t first_call;
    ProtocolCommand *command;
    if (!protocol) { return; }
    if (protocol->reply_ready) {
        if (!SendReply(protocol)) { return; }
        protocol->reply_ready = 0;
        protocol->command_started = 0;
        if (protocol->sending_overflow) {
            protocol->sending_overflow = 0;
            if (!protocol->pending_overflow) { protocol->overflow_blocked = 0; }
        } else {
            protocol->queue_tail = (uint8_t)((protocol->queue_tail + 1u) % PROTOCOL_QUEUE_CAPACITY);
            --protocol->queue_count;
        }
        return;
    }
    if (!protocol->queue_count) {
        if (protocol->pending_overflow) {
            (void)snprintf(protocol->reply, sizeof(protocol->reply), "ERR OVERFLOW %lu\r\n",
                (unsigned long)protocol->pending_overflow);
            protocol->pending_overflow = 0;
            protocol->sending_overflow = 1;
            protocol->reply_ready = 1;
        }
        return;
    }
    command = &protocol->queue[protocol->queue_tail];
    first_call = (uint8_t)!protocol->command_started;
    protocol->command_started = 1;
    protocol->reply[0] = '\0';
    if (command->type == PROTOCOL_CMD_INVALID) {
        (void)snprintf(protocol->reply, sizeof(protocol->reply), "ERR SYNTAX\r\n");
        result = PROTOCOL_DONE;
    } else if (!protocol->handler) {
        (void)snprintf(protocol->reply, sizeof(protocol->reply), "ERR INTERNAL\r\n");
        result = PROTOCOL_DONE;
    } else {
        result = protocol->handler(command, first_call, now_ms,
            protocol->reply, sizeof(protocol->reply), protocol->context);
    }
    protocol->reply[sizeof(protocol->reply) - 1u] = '\0';
    if (result == PROTOCOL_DONE) {
        if (!protocol->reply[0]) {
            (void)snprintf(protocol->reply, sizeof(protocol->reply), "ERR INTERNAL\r\n");
        }
        protocol->reply_ready = 1;
    }
}

uint8_t Protocol_IsIdle(const Protocol *protocol)
{
    return (uint8_t)(protocol && !protocol->queue_count && !protocol->line_length &&
        !protocol->line_invalid && !protocol->reply_ready && !protocol->pending_overflow &&
        !protocol->sending_overflow && !protocol->overflow_blocked);
}

uint8_t Protocol_IsWaiting(const Protocol *protocol)
{
    return (uint8_t)(protocol && protocol->command_started && !protocol->reply_ready);
}

uint8_t Protocol_IsBusy(const Protocol *protocol)
{
    return (uint8_t)!Protocol_IsIdle(protocol);
}

int32_t Protocol_FloatToTenths(float value)
{
    if (!(value == value)) { return 0; }
    if (value >= 100000.0f) { return 1000000; }
    if (value <= -100000.0f) { return -1000000; }
    return (int32_t)(value * 10.0f + (value >= 0.0f ? 0.5f : -0.5f));
}

uint8_t Protocol_FormatTenths(char *out, size_t capacity, int32_t value, uint8_t force_sign)
{
    uint32_t magnitude;
    int written;
    const char *sign;
    if (!out || !capacity) { return 0; }
    magnitude = value < 0 ? (uint32_t)(-(value + 1)) + 1u : (uint32_t)value;
    sign = value < 0 ? "-" : (force_sign ? "+" : "");
    written = snprintf(out, capacity, "%s%lu.%lu", sign,
        (unsigned long)(magnitude / 10u), (unsigned long)(magnitude % 10u));
    return (uint8_t)(written >= 0 && (size_t)written < capacity);
}
