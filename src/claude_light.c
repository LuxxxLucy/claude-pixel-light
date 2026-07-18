// claude-light — map Claude Code hook events to pixel-lightsd commands.
// usage: claude-light hook <Event>   (hook JSON arrives on stdin)
// claude-light <line...>      (forward a raw daemon line)
// One-shot: writes lines to the daemon's fifo and exits. If no daemon is
// listening the lines are dropped silently.
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define FIFO "/run/pixel-lights.fifo"
#define MAX_LINE 256
#define MAX_JSON 16384

// The palette. Colourblind-safe: states ride the blue<->amber axis plus
// brightness plus motion; no state is told apart by red-vs-green alone.
// Bare effect names use the daemon's defaults.
#define FX_REST "breathe"
#define FX_READY "breathe c=006e82 period=6 lo=0.55 hi=0.9"
#define FX_BOOT "play scan for 0.6 then " FX_READY
#define FX_WORK "play scan"
#define FX_DONE "play sparkle for 1 then " FX_READY
#define FX_DONE_FANCY "play rainbow for 1 then " FX_READY
#define FX_COMPACT "play ripple for 1.5 then " FX_READY
#define FX_WAIT "play knock"
#define FX_PERM "play blink"
#define FX_PAUSE "play heartbeat"
#define FX_ERROR "play alert for 2 then " FX_READY

#define IDLE_ON "idle 600 " FX_REST  // settle to rest after 10 min quiet
#define IDLE_OFF "idle off"  // attention states must not fade out
#define KBD_WORK "kbd ramp 20 60 90"
#define KBD_RESTORE "kbd restore"
#define FANCY_ODDS 500  // 1-in-N rainbow finish

static const char *fifo_path(void)
{
    const char *p = getenv("PIXEL_LIGHTS_FIFO");
    return p ? p : FIFO;
}

// one full line per write: atomic under PIPE_BUF, so writers never interleave
static void send(const char *msg)
{
    char buf[MAX_LINE];
    int len = snprintf(buf, sizeof buf, "%s\n", msg);
    if (len >= (int)sizeof buf) {
        return;
    }
    int fd = open(fifo_path(), O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        return;
    }
    if (write(fd, buf, len) < 0) {
        // no listener: drop
    }
    close(fd);
}

// Best-effort read of a flat JSON string field: "key": "value".
static int json_field(const char *json, const char *key, char *out, size_t n)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) {
        return -1;
    }
    p = strchr(p + strlen(pat), ':');
    if (!p) {
        return -1;
    }
    for (p++; *p == ' ' || *p == '\t'; p++) {
    }
    if (*p != '"') {
        return -1;
    }
    size_t i = 0;
    for (p++; *p && *p != '"' && i < n - 1; p++) {
        out[i++] = *p;
    }
    out[i] = 0;
    return 0;
}

// read all of stdin (hook JSON), looping over short reads
static void read_stdin(char *buf, size_t n)
{
    size_t got = 0;
    ssize_t r;
    while (got < n - 1 && (r = read(0, buf + got, n - 1 - got)) > 0) {
        got += r;
    }
    buf[got] = 0;
}

static int fancy_roll(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    // mix sec/nsec/pid: stays 1-in-N even on a coarse clock
    unsigned long mix = (unsigned long)ts.tv_nsec ^
                        ((unsigned long)ts.tv_sec << 10) ^
                        (unsigned long)getpid();
    return mix % FANCY_ODDS == 0;
}

static void hook(const char *ev)
{
    if (!strcmp(ev, "SessionStart")) {
        send("clear");  // session boundary: stale overlays die here
        send(IDLE_ON);
        send(FX_BOOT);
        send(KBD_RESTORE);
    } else if (!strcmp(ev, "UserPromptSubmit") || !strcmp(ev, "PostToolUse")) {
        // PostToolUse re-asserts working after a granted permission or a
        // compact; the daemon dedupes, so nothing restarts
        send(IDLE_OFF);  // working must never fade out mid-turn
        send(FX_WORK);
        send(KBD_WORK);
    } else if (!strcmp(ev, "Stop")) {
        send(IDLE_ON);
        send(fancy_roll() ? FX_DONE_FANCY : FX_DONE);
        send(KBD_RESTORE);
    } else if (!strcmp(ev, "SessionEnd")) {
        send("clear");
        send(IDLE_ON);
        send("play " FX_REST);
        send(KBD_RESTORE);
    } else if (!strcmp(ev, "PreCompact")) {
        send(IDLE_ON);
        send(FX_COMPACT);
        send(KBD_RESTORE);
    } else if (!strcmp(ev, "SubagentStart")) {
        send("push rainbow");
    } else if (!strcmp(ev, "SubagentStop")) {
        send("pop");
    } else {
        static char in[MAX_JSON];
        char f[64];
        read_stdin(in, sizeof in);
        if (!strcmp(ev, "Notification")) {
            int perm = json_field(in, "notification_type", f, sizeof f) == 0 &&
                       strstr(f, "permission");
            send(IDLE_OFF);
            send(perm ? FX_PERM : FX_WAIT);
            send(KBD_RESTORE);
        } else if (!strcmp(ev, "StopFailure")) {
            int paused = json_field(in, "error_type", f, sizeof f) == 0 &&
                         (strstr(f, "rate") || strstr(f, "overload") ||
                          strstr(f, "billing"));
            send(paused ? IDLE_OFF : IDLE_ON);
            send(paused ? FX_PAUSE : FX_ERROR);
            send(KBD_RESTORE);
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: claude-light <hook EVENT | line...>\n");
        return 2;
    }
    if (!strcmp(argv[1], "hook") && argc >= 3) {
        hook(argv[2]);
        return 0;
    }
    char msg[MAX_LINE] = "";
    for (int i = 1; i < argc; i++) {
        strncat(msg, argv[i], sizeof msg - strlen(msg) - 2);
        if (i + 1 < argc) {
            strncat(msg, " ", sizeof msg - strlen(msg) - 2);
        }
    }
    send(msg);
    return 0;
}
