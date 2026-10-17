#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "protocol.h"

typedef struct {
    char output[4096];
    unsigned calls;
    unsigned started;
    unsigned time_committed;
    uint8_t blocked;
} TestContext;

static ProtocolHandlerResult Handler(const ProtocolCommand *command, uint8_t first,
    uint32_t now, char *reply, size_t capacity, void *opaque)
{
    TestContext *ctx = opaque;
    ++ctx->calls;
    if (first) { ++ctx->started; }
    if (command->type == PROTOCOL_CMD_TIME_SET && now < 30) {
        return PROTOCOL_WAIT;
    }
    if (command->type == PROTOCOL_CMD_TIME_SET) { ++ctx->time_committed; }
    (void)snprintf(reply, capacity, "OK %u\r\n", (unsigned)command->type);
    return PROTOCOL_DONE;
}

static uint8_t Writer(const char *text, uint16_t length, void *opaque)
{
    TestContext *ctx = opaque;
    size_t used = strlen(ctx->output);
    if (ctx->blocked) { return 0; }
    assert(used + length < sizeof(ctx->output));
    memcpy(ctx->output + used, text, length);
    ctx->output[used + length] = '\0';
    return 1;
}

static void Feed(Protocol *protocol, const char *line)
{
    while (*line) { Protocol_FeedByte(protocol, (uint8_t)*line++); }
}

static void Drain(Protocol *protocol, uint32_t now)
{
    unsigned i;
    for (i = 0; i < 64 && Protocol_IsBusy(protocol); ++i) { Protocol_Process(protocol, now); }
    assert(Protocol_IsIdle(protocol));
}

int main(void)
{
    Protocol protocol;
    ProtocolCommand command;
    TestContext ctx;
    char number[32];
    unsigned i;
    const char *valid[] = {
        "HELP", "help", "TIME(00:00:00)", "TIME(23:59:59)",
        "ALARM1(12:34:56)", "ALARM3(00:00:00)", "ALARM2 OFF", "ALARM3 DEL",
        "ALARM ?", "IMU ?", "SERVO ?", "SERVO SET 0", "SERVO SET 180",
        "SERVO HOLD ON", "SERVO HOLD OFF", "SERVO HOLD ?",
        "SERVO TRACE ON", "SERVO TRACE OFF", "SERVO TRACE ?", "IMU FAULT ?", "SYS RESET ?"
    };
    const char *invalid[] = {
        "", "Help", " HELP", "HELP ", "time(00:00:00)", "TIME(24:00:00)",
        "TIME(00:60:00)", "TIME(00:00:60)", "TIME(1:00:00)", "TIME(01:00:00)X",
        "TIME (01:00:00)", "ALARM0(00:00:00)", "ALARM4 OFF", "ALARM12 OFF",
        "ALARM1 ON", "ALARM1 OFF ", "ALARM1(99:00:00)", "IMU?", "SERVO SET -1",
        "SERVO SET 181", "SERVO SET 12x", "SERVO SET 1.0", "SERVO SET +1",
        "SERVO SET 0000", "SERVO HOLD", "SERVO TRACE ONX", "SERVO SET 90;HELP",
        "IMU FAULT", "IMU FAULT ? ", "imu fault ?", "SYS RESET", "SYS RESET ? ",
        "sys reset ?", "SYS RESET ON"
    };
    for (i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i) { assert(Protocol_Parse(valid[i], &command)); }
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) { assert(!Protocol_Parse(invalid[i], &command)); }
    assert(Protocol_Parse("ALARM3(23:59:58)", &command));
    assert(command.alarm_number == 3 && command.hour == 23 && command.minute == 59 && command.second == 58);
    assert(Protocol_Parse("SERVO SET 180", &command) && command.angle_deg == 180);
    assert(Protocol_Parse("IMU FAULT ?", &command) && command.type == PROTOCOL_CMD_IMU_FAULT_QUERY);
    assert(Protocol_Parse("SYS RESET ?", &command) && command.type == PROTOCOL_CMD_SYS_RESET_QUERY);

    memset(&ctx, 0, sizeof(ctx));
    Protocol_Init(&protocol, Handler, Writer, &ctx);
    Feed(&protocol, "TIME(23:59:55)\r\nSERVO SET 90\n");
    Protocol_Process(&protocol, 0);
    assert(Protocol_IsWaiting(&protocol) && ctx.started == 1 && ctx.time_committed == 0);
    Protocol_Process(&protocol, 10);
    Protocol_Process(&protocol, 20);
    assert(ctx.started == 1 && ctx.output[0] == '\0');
    ctx.blocked = 1;
    Protocol_Process(&protocol, 30);
    assert(ctx.time_committed == 1);
    Protocol_Process(&protocol, 40);
    Protocol_Process(&protocol, 50);
    assert(ctx.time_committed == 1 && ctx.started == 1); /* cached response doesn't reexecute */
    ctx.blocked = 0;
    Drain(&protocol, 60);
    assert(ctx.started == 2 && ctx.time_committed == 1);
    assert(strcmp(ctx.output, "OK 2\r\nOK 9\r\n") == 0);

    memset(&ctx, 0, sizeof(ctx));
    Protocol_Init(&protocol, Handler, Writer, &ctx);
    Feed(&protocol, "\r\n\nHELP\rhelp\n");
    Drain(&protocol, 100);
    assert(ctx.started == 2); /* CRLF is one separator */
    for (i = 0; i < 120; ++i) { Protocol_FeedByte(&protocol, 'X'); }
    Feed(&protocol, "HELP\nHELP\n");
    Drain(&protocol, 100);
    assert(ctx.started == 3 && strstr(ctx.output, "ERR SYNTAX\r\n"));
    Feed(&protocol, "TIME(12:34");
    Protocol_InvalidateInput(&protocol);
    Feed(&protocol, ":56)\n");
    Drain(&protocol, 100);
    assert(ctx.started == 3); /* transport loss can't become a TIME command */

    memset(&ctx, 0, sizeof(ctx));
    Protocol_Init(&protocol, Handler, Writer, &ctx);
    for (i = 0; i < 12; ++i) { Feed(&protocol, "HELP\n"); }
    assert(protocol.dropped_lines == 4);
    Drain(&protocol, 100);
    assert(ctx.started == 8 && strstr(ctx.output, "ERR OVERFLOW 4\r\n"));
    Feed(&protocol, "HELP\n");
    Drain(&protocol, 100);
    assert(ctx.started == 9);

    assert(Protocol_FormatTenths(number, sizeof(number), -1, 1) && strcmp(number, "-0.1") == 0);
    assert(Protocol_FormatTenths(number, sizeof(number), 0, 1) && strcmp(number, "+0.0") == 0);
    assert(Protocol_FormatTenths(number, sizeof(number), 123, 1) && strcmp(number, "+12.3") == 0);
    assert(Protocol_FormatTenths(number, sizeof(number), INT32_MIN, 0) && strcmp(number, "-214748364.8") == 0);
    assert(!Protocol_FormatTenths(number, 2, 123, 1));
    assert(Protocol_FloatToTenths(-0.14f) == -1);
    assert(Protocol_FloatToTenths(0.16f) == 2);
    puts("PASS: strict grammar, time ranges, CRLF, async ordering, TX retry, oversized/lost RX, queue overflow, signed decimals");
    return 0;
}
