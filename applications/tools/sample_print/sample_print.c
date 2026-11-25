#include "printmng.h"

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile sig_atomic_t interrupted;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    interrupted = 1;
}

static const char *state_name(PRINTMNG_State state)
{
    static const char *names[] = {
        "idle", "queued", "preparing", "prefeed", "homing", "settling",
        "scanning", "feeding", "ejecting", "completed", "cancelled", "failed"
    };
    return state <= PRINTMNG_FAILED ? names[state] : "unknown";
}

static void report(const PRINTMNG_Status *status, void *context)
{
    (void)context;
    printf("job=%" PRIu64 " %s sweep=%u/%u %s columns=%" PRIu64 "/%" PRIu64
           " feed_commanded=%u um home_verified=%u error=%d cleanup=%d\n",
           status->job_id, state_name(status->state), status->sweeps_done, status->plan.sweeps,
           status->sweep.direction == PRINTMNG_LEFT_TO_RIGHT ? "LTR" : "RTL",
           status->total_columns_done, status->plan.total_columns, status->feed_commanded_um,
           status->physical_home_verified, status->last_error, status->cleanup_error);
    fflush(stdout);
}

static int parse_number(const char *text, uint32_t *number)
{
    char *end;
    errno = 0;
    unsigned long parsed = strtoul(text, &end, 10);
    if (errno || !text[0] || text[0] == '-' || *end || parsed > UINT32_MAX)
        return -EINVAL;
    *number = parsed;
    return 0;
}

static void usage(const char *program)
{
    printf("Usage: %s [--passes 8|16] [--image PATH] [--fast]\n"
           "  Default: hardware-free simulated printing (no image decoding or ink).\n"
           "  --feed-channel 12|34 --feed-um-per-rev N: enable calibrated paper motor\n"
           "  --scan-channel 12|34 --scan-um-per-rev N --allow-unhomed-scan:\n"
           "      enable calibrated scan motor; manually place carriage at LEFT first\n"
           "  --feed-reverse / --scan-reverse: reverse the corresponding positive axis\n"
           "  --pps 200..510: motor target speed (default 510)\n"
           "  --current-ma 100|125|150: estimated amplitude, not a measured current limit\n"
           "  --move-timeout-ms N: per-move deadline, default 10000 ms\n"
           "  Ctrl+C requests cancellation and releases motors. Real printhead not connected.\n",
           program);
}

int main(int argc, char **argv)
{
    enum { OPT_FEED_CHANNEL = 256, OPT_FEED_SCALE, OPT_SCAN_CHANNEL, OPT_SCAN_SCALE,
           OPT_UNHOMED, OPT_FEED_REVERSE, OPT_SCAN_REVERSE, OPT_PPS, OPT_CURRENT, OPT_TIMEOUT };
    static const struct option options[] = {
        {"passes", required_argument, NULL, 'p'}, {"image", required_argument, NULL, 'i'},
        {"fast", no_argument, NULL, 'f'}, {"help", no_argument, NULL, 'h'},
        {"feed-channel", required_argument, NULL, OPT_FEED_CHANNEL},
        {"feed-um-per-rev", required_argument, NULL, OPT_FEED_SCALE},
        {"scan-channel", required_argument, NULL, OPT_SCAN_CHANNEL},
        {"scan-um-per-rev", required_argument, NULL, OPT_SCAN_SCALE},
        {"allow-unhomed-scan", no_argument, NULL, OPT_UNHOMED},
        {"feed-reverse", no_argument, NULL, OPT_FEED_REVERSE},
        {"scan-reverse", no_argument, NULL, OPT_SCAN_REVERSE},
        {"pps", required_argument, NULL, OPT_PPS},
        {"current-ma", required_argument, NULL, OPT_CURRENT},
        {"move-timeout-ms", required_argument, NULL, OPT_TIMEOUT}, {NULL, 0, NULL, 0}
    };
    PRINTMNG_Config config;
    PRINTMNG_Job job;
    PRINTMNG_DefaultConfig(&config);
    PRINTMNG_DefaultJob(&job);
    config.on_status = report;
    int option;
    while ((option = getopt_long(argc, argv, "p:i:fh", options, NULL)) != -1) {
        uint32_t number = 0;
        if (option != 'i' && option != 'f' && option != 'h' && option != OPT_UNHOMED &&
            option != OPT_FEED_REVERSE && option != OPT_SCAN_REVERSE && option != '?' &&
            parse_number(optarg, &number) < 0) {
            fprintf(stderr, "Invalid number: %s\n", optarg);
            return 2;
        }
        switch (option) {
        case 'p': job.passes = number; break;
        case 'i':
            if (strlen(optarg) >= sizeof(job.image_path))
                return 2;
            strcpy(job.image_path, optarg);
            break;
        case 'f':
            config.poll_ms = 1;
            config.settle_ms = 0;
            config.simulated_scan_ms = 2;
            config.simulated_feed_ms = 1;
            break;
        case OPT_FEED_CHANNEL: config.feed_axis.channel = (MOTORMNG_Channel)number; break;
        case OPT_SCAN_CHANNEL: config.scan_axis.channel = (MOTORMNG_Channel)number; break;
        case OPT_FEED_SCALE:
            config.feed_axis.travel_um_per_motor_rev = number;
            config.feed_axis.enabled = 1;
            break;
        case OPT_SCAN_SCALE:
            config.scan_axis.travel_um_per_motor_rev = number;
            config.scan_axis.enabled = 1;
            break;
        case OPT_UNHOMED: config.allow_unhomed_scan = 1; break;
        case OPT_FEED_REVERSE: config.feed_axis.forward_direction = MOTORMNG_REVERSE; break;
        case OPT_SCAN_REVERSE: config.scan_axis.forward_direction = MOTORMNG_REVERSE; break;
        case OPT_PPS: config.scan_axis.pps = config.feed_axis.pps = number; break;
        case OPT_CURRENT:
            config.scan_axis.current_estimate_ma = config.feed_axis.current_estimate_ma = number;
            break;
        case OPT_TIMEOUT: config.move_timeout_ms = number; break;
        case 'h': usage(argv[0]); return 0;
        default: usage(argv[0]); return 2;
        }
    }
    if (optind != argc) {
        usage(argv[0]);
        return 2;
    }
    if (config.scan_axis.enabled || config.feed_axis.enabled)
        config.mode = PRINTMNG_MOTOR_SIMULATION;
    printf("mode=%s; geometry is taixin EXAMPLE, not PT100 calibration.\n",
           config.mode == PRINTMNG_DRY_RUN ? "DRY_RUN (no motor access)" : "MOTOR_SIMULATION");
    struct sigaction action = {0};
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) || sigaction(SIGTERM, &action, NULL)) {
        perror("sigaction");
        return 1;
    }
    int result = PRINTMNG_Init(&config);
    if (result < 0) {
        fprintf(stderr, "Init failed: %s (%d)\n", strerror(-result), result);
        return 1;
    }
    uint64_t job_id;
    result = PRINTMNG_Submit(&job, &job_id);
    PRINTMNG_Status status;
    if (!result) {
        for (;;) {
            if (interrupted)
                PRINTMNG_Cancel(job_id);
            result = PRINTMNG_Wait(job_id, 100, &status);
            if (result != -ETIMEDOUT || !status.active)
                break;
        }
    }
    int closed = PRINTMNG_DeInit();
    if (!result)
        result = closed;
    if (result < 0)
        fprintf(stderr, "Print ended: %s (%d)\n", strerror(-result), result);
    return result == -ECANCELED ? 130 : result < 0 ? 1 : 0;
}
