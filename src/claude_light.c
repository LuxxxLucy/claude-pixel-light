#define _GNU_SOURCE
#include "pixel_lights.h"

#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define FIFO "/run/pixel-lights.fifo"
#define NLED 4
#define FRAME_MS 50          /* ~20 fps, matching the EC's native step */
#define IDLE_MS (10 * 60000) /* settle to REST after 10 min quiet */
#define PI 3.14159265358979323846
#define FLOOR 0.06 /* never fully dark: a live bar, not a crashed one */
#define GAMMA 2.2  /* perceptual gamma for the LED PWM */

/* Perceptual gamma: cheap LEDs are ~linear in PWM but the eye is not, so a
 * linear fade looks like a snap. Map desired level -> PWM through a table. */
static unsigned char gamma_lut[256];
static void build_gamma(void)
{
    for (int i = 0; i < 256; i++) {
        gamma_lut[i] = (unsigned char)(pow(i / 255.0, GAMMA) * 255.0 + 0.5);
    }
}

static double clampd(double x, double lo, double hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

static struct pl_rgb dim(struct pl_rgb c, double k)
{
    k = clampd(k, 0, 1);
    return (struct pl_rgb){ c.r * k, c.g * k, c.b * k };
}

/* h,s,v in 0..1 -> rgb 0..255. Used only for the subagent rainbow. */
static struct pl_rgb hsv(double h, double s, double v)
{
    double i = floor(h * 6), f = h * 6 - i;
    double p = v * (1 - s), q = v * (1 - f * s), u = v * (1 - (1 - f) * s);
    double r, g, b;
    switch (((int)i) % 6) {
        case 0:
            r = v, g = u, b = p;
            break;
        case 1:
            r = q, g = v, b = p;
            break;
        case 2:
            r = p, g = v, b = u;
            break;
        case 3:
            r = p, g = q, b = v;
            break;
        case 4:
            r = u, g = p, b = v;
            break;
        default:
            r = v, g = p, b = q;
            break;
    }
    return (struct pl_rgb){ r * 255, g * 255, b * 255 };
}

/* Sub-pixel brightness of LED i for a light "head" at fractional position p:
 * a triangular blob, so 4 LEDs read as a smooth ~40-step scanner. */
static double blob(int i, double p)
{
    double d = fabs(i - p);
    return d < 1.0 ? 1.0 - d : 0.0;
}

/* Deterministic hash in 0..1, so the sparkle needs no RNG state. */
static double hash1(double x)
{
    double s = sin(x * 12.9898) * 43758.5453;
    return s - floor(s);
}

/* The palette. Colourblind-safe: states ride the blue<->amber axis plus
 * brightness plus MOTION; no state is told apart by red-vs-green alone. */
enum st {
    REST,
    READY,
    BOOT,
    WORKING,
    WAITING,
    PERMISSION,
    DONE,
    PAUSED,
    ERROR,
    COMPACT
};
static const struct pl_rgb C_REST = { 0, 50, 170 }, C_READY = { 0, 110, 130 },
                           C_WAIT = { 255, 150, 0 }, C_PERM = { 255, 110, 0 },
                           C_DONE = { 255, 255, 255 },
                           C_PAUSE = { 150, 0, 255 }, C_ERR = { 255, 60, 0 };

/* Colour of every LED at time t (seconds in this state); `phase` is the
 * accumulated animation angle for tempo-varying motion; `sub` is the live
 * subagent count. Writes NLED entries into out. */
static void render(enum st s, double t, double phase, int sub,
                   struct pl_rgb out[NLED])
{
    /* Subagents override any base: a fast 4-colour rainbow, one hue per LED. */
    if (sub > 0) {
        for (int i = 0; i < NLED; i++) {
            out[i] = hsv(fmod(t * 2.0 + i * 0.25, 1.0), 1.0, 1.0);
        }
        return;
    }
    switch (s) {
        case REST: {
            /* the daemon's home: a calm blue that breathes in brightness.
             * ticks at the full frame rate so the fade reads as smooth. */
            double k = 0.75 - 0.25 * cos(2 * PI * t / 8.0); /* 0.5 .. 1.0 */
            for (int i = 0; i < NLED; i++) {
                out[i] = dim(C_REST, k);
            }
            break;
        }
        case READY: {
            double k = 0.55 + 0.35 * (0.5 - 0.5 * cos(2 * PI * t / 6.0));
            for (int i = 0; i < NLED; i++) {
                out[i] = dim(C_READY, k);
            }
            break;
        }
        case BOOT:
        case WORKING: {
            double p = 1.5 + 1.5 * sin(phase); /* head bounces across the bar */
            double hot =
                0.55 + 0.45 * tanh(t / 90.0); /* crest brightens over time */
            double h =
                fmod(0.6 + t * 0.05, 1.0); /* start blue, drift the wheel */
            for (int i = 0; i < NLED; i++) {
                double v = FLOOR + (hot - FLOOR) * blob(i, p);
                out[i] = hsv(fmod(h + i * 0.04, 1.0), 0.85, v);
            }
            break;
        }
        case WAITING: {
            double u = fmod(t, 1.4) / 1.4; /* knock: two taps then wait */
            double on = (u < 0.12 || (u > 0.22 && u < 0.34)) ? 1.0 : FLOOR;
            for (int i = 0; i < NLED; i++) {
                out[i] = dim(C_WAIT, on);
            }
            break;
        }
        case PERMISSION: {
            double on =
                fmod(t, 0.34) < 0.17 ? 1.0 : FLOOR; /* ~3 Hz hard blink */
            for (int i = 0; i < NLED; i++) {
                out[i] = dim(C_PERM, on);
            }
            break;
        }
        case DONE: {
            double env = clampd(1.0 - t / 0.9, 0, 1); /* whole burst decays */
            int bucket = (int)(t / 0.06);
            for (int i = 0; i < NLED; i++) {
                double lit = hash1(bucket * 4 + i) > 0.55 ? 1.0 : 0.0;
                double b = lit * (1.0 - fmod(t, 0.06) / 0.06) * env;
                out[i] = dim(C_DONE, b);
            }
            break;
        }
        case PAUSED: {
            double u = fmod(t, 3.0);
            double b1 = exp(-pow((u - 0.0) / 0.09, 2));        /* lub */
            double b2 = 0.6 * exp(-pow((u - 0.28) / 0.09, 2)); /* dub */
            double k = 0.12 + 0.5 * fmin(1.0, b1 + b2);
            for (int i = 0; i < NLED; i++) {
                out[i] = dim(C_PAUSE, k);
            }
            break;
        }
        case ERROR: {
            double u = fmod(t, 0.2); /* 3 fast flashes, then a dim hold */
            double on = t < 0.9 ? (u < 0.1 ? 1.0 : 0.0) : 0.25;
            struct pl_rgb c = (t < 0.9 && u < 0.05) ? C_DONE : C_ERR;
            for (int i = 0; i < NLED; i++) {
                out[i] = dim(c, on);
            }
            break;
        }
        case COMPACT: {
            double p = 1.5 + 1.5 * sin(phase); /* slow inward ripple */
            for (int i = 0; i < NLED; i++) {
                out[i] = dim(C_READY, FLOOR + (1 - FLOOR) * blob(i, p));
            }
            break;
        }
    }
}

/* --- token protocol: one word per line, state name or a modifier --- */

static const struct {
    const char *name;
    enum st st;
} TOKENS[] = {
    { "boot", BOOT },       { "rest", REST },
    { "idle", REST },       { "ready", READY },
    { "working", WORKING }, { "waiting", WAITING },
    { "input", WAITING },   { "permission", PERMISSION },
    { "done", DONE },       { "paused", PAUSED },
    { "error", ERROR },     { "compact", COMPACT },
};
#define NTOKENS ((int)(sizeof TOKENS / sizeof TOKENS[0]))

static int token_state(const char *name)
{
    for (int i = 0; i < NTOKENS; i++) {
        if (!strcmp(name, TOKENS[i].name)) {
            return TOKENS[i].st;
        }
    }
    return -1;
}

static const char *fifo_path(void)
{
    const char *p = getenv("CLAUDE_LIGHT_FIFO");
    return p ? p : FIFO;
}

/* Tell a running daemon a token; do nothing if none listens. */
static void send(const char *msg)
{
    int fd = open(fifo_path(), O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        return;
    }
    dprintf(fd, "%s\n", msg);
    close(fd);
}

/* Best-effort read of a flat JSON string field: "key": "value". */
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

/* Turn a Claude Code hook event into a token. Events that need a payload
 * field read the hook JSON from stdin; the rest map to a fixed token. */
static void hook(const char *ev)
{
    if (!strcmp(ev, "SessionStart")) {
        send("boot");
    } else if (!strcmp(ev, "UserPromptSubmit")) {
        send("working");
    } else if (!strcmp(ev, "Stop")) {
        send("done");
    } else if (!strcmp(ev, "SessionEnd")) {
        send("rest");
    } else if (!strcmp(ev, "PreCompact")) {
        send("compact");
    } else if (!strcmp(ev, "SubagentStart")) {
        send("subagent +");
    } else if (!strcmp(ev, "SubagentStop")) {
        send("subagent -");
    } else {
        char in[4096], f[64];
        ssize_t n = read(0, in, sizeof in - 1);
        in[n > 0 ? n : 0] = 0;
        if (!strcmp(ev, "Notification")) {
            send(json_field(in, "notification_type", f, sizeof f) == 0 &&
                         strstr(f, "permission")
                     ? "permission"
                     : "waiting");
        } else if (!strcmp(ev, "StopFailure")) {
            int paused = json_field(in, "error_type", f, sizeof f) == 0 &&
                         (strstr(f, "rate") || strstr(f, "overload") ||
                          strstr(f, "billing"));
            send(paused ? "paused" : "error");
        }
    }
}

static volatile sig_atomic_t stop;
static void quit(int sig)
{
    (void)sig;
    stop = 1;
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* Apply one token to the daemon's state. Returns 1 if the base state changed
 * (so the caller can restart the clock), 0 otherwise. */
static int apply(char *line, enum st *s, int *sub, int *fancy)
{
    char *arg = strchr(line, ' ');
    if (arg) {
        *arg++ = 0;
    }
    if (!strcmp(line, "subagent")) {
        *sub += (arg && arg[0] == '-') ? -1 : 1;
        if (*sub < 0) {
            *sub = 0;
        }
        return 0;
    }
    int next = token_state(line);
    if (next < 0) {
        return 0;
    }
    *fancy =
        (next == DONE && hash1(now_ms()) * 500 < 1); /* rare rainbow done */
    *s = next;
    return 1;
}

static int run(void)
{
    signal(SIGINT, quit);
    signal(SIGTERM, quit);
    signal(SIGHUP, quit);

    build_gamma();
    const char *fifo = fifo_path();
    unlink(fifo);
    if (mkfifo(fifo, 0666) < 0) {
        perror("mkfifo");
        return 1;
    }
    chmod(fifo, 0666); /* defeat umask: unprivileged hooks write here */
    int fd = open(fifo, O_RDWR | O_NONBLOCK);
    if (fd < 0 || pl_lb_claim() < 0) {
        perror("pixel-lights (run as root?)");
        unlink(fifo);
        return 1;
    }

    pl_lb_brightness(255); /* full range; our gamma does the dimming */

    int kbd0 = pl_kbd_get(); /* restore the user's keyboard light on exit */
    int kbd_last = kbd0;
    enum st s = REST; /* idle home: show a calm blue the moment we start */
    int sub = 0, fancy = 0, primed = 0;
    struct pl_rgb prev[NLED];
    double t0 = now_ms(), last = t0, active = t0, phase = 0;

    while (!stop) {
        double now = now_ms();
        double dt = (now - last) / 1000.0;
        double t = (now - t0) / 1000.0;
        last = now;

        double omega = 0; /* advance the animation angle for moving states */
        if (s == WORKING || s == BOOT) {
            omega = 2 * PI * (0.6 + 0.6 * tanh(t / 60.0));
        } else if (s == COMPACT) {
            omega = 2 * PI * 0.5;
        }
        phase += omega * dt;

        struct pl_rgb out[NLED];
        render(s, t, phase, fancy ? 1 : sub,
               out); /* fancy done: force rainbow */
        for (int i = 0; i < NLED; i++) {
            out[i] = (struct pl_rgb){ gamma_lut[out[i].r], gamma_lut[out[i].g],
                                      gamma_lut[out[i].b] };
        }
        if (!primed || memcmp(out, prev, sizeof out) != 0) {
            pl_lb_leds(out, NLED); /* skip the EC write when nothing changed */
            memcpy(prev, out, sizeof out);
            primed = 1;
        }

        if (kbd0 >= 0) { /* keyboard = a slow "still cooking" thermometer */
            int kt = (s == WORKING) ? (int)(20 + 40 * tanh(t / 90.0)) : kbd0;
            if (abs(kt - kbd_last) >= 3) {
                pl_kbd_set(kt);
                kbd_last = kt;
            }
        }

        struct pollfd p = { fd, POLLIN, 0 };
        poll(&p, 1, FRAME_MS);
        if (p.revents & POLLIN) {
            char buf[256], *tok, *nl;
            ssize_t n = read(fd, buf, sizeof buf - 1);
            if (n > 0) {
                buf[n] = 0;
                for (tok = buf; (nl = strchr(tok, '\n')); tok = nl + 1) {
                    *nl = 0;
                    if (apply(tok, &s, &sub, &fancy)) {
                        t0 = active = now_ms();
                        phase = 0;
                    } else {
                        active = now_ms();
                    }
                }
            }
        }

        /* Transients hand off; settled states decay toward REST. Recompute
         * elapsed: a token applied above may have just reset t0. */
        double te = (now_ms() - t0) / 1000.0;
        if ((s == BOOT && te > 0.6) || (s == DONE && te > 1.0) ||
            (s == COMPACT && te > 1.5) || (s == ERROR && te > 2.0)) {
            s = READY;
            t0 = now_ms();
            phase = 0;
            fancy = 0;
        } else if (s == READY && now_ms() - active >= IDLE_MS) {
            s = REST;
            t0 = now_ms();
        }
    }

    if (kbd0 >= 0) {
        pl_kbd_set(kbd0);
    }
    pl_lb_release();
    unlink(fifo);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: claude-light <daemon|hook EVENT|state>\n");
        return 2;
    }
    if (!strcmp(argv[1], "daemon")) {
        return run();
    }
    if (!strcmp(argv[1], "hook") && argc >= 3) {
        hook(argv[2]);
        return 0;
    }
    char msg[64] = "";
    for (int i = 1; i < argc; i++) {
        strncat(msg, argv[i], sizeof msg - strlen(msg) - 2);
        if (i + 1 < argc) {
            strncat(msg, " ", sizeof msg - strlen(msg) - 2);
        }
    }
    send(msg);
    return 0;
}
