#include "basecmd.h"
#include "command.h"
#include "sched.h"
#include "board/misc.h"
#include "load_cell_probe.h"
#include "trsync.h"
#include <stdint.h>
#include <stdlib.h>

enum {
    MDF_REASON_NONE = 0,
    MDF_REASON_DF = 1,
    MDF_REASON_SAFE = 2,
};

struct mdf_state {
    struct timer timer;
    struct load_cell_probe *lce;

    uint32_t sample_ticks;
    int32_t df_threshold;
    int32_t safe_threshold;

    int32_t raw0;
    int32_t prev_raw;
    int32_t last_raw;
    int32_t last_df;
    int32_t last_d0;

    uint8_t active;
    uint8_t triggered;
    uint8_t trigger_reason;

    struct trsync *ts;
    uint8_t use_trsync;
    uint8_t trsync_trigger_reason;
    uint8_t trsync_error_reason;
};

static struct mdf_state mdf;

static void
mdf_do_trigger(uint8_t reason, int32_t raw, int32_t df, int32_t d0)
{
    mdf.triggered = 1;
    mdf.trigger_reason = reason;
    mdf.active = 0;

    mdf.last_raw = raw;
    mdf.last_df = df;
    mdf.last_d0 = d0;

    if (mdf.use_trsync) {
        trsync_do_trigger(mdf.ts, mdf.trsync_trigger_reason);
        return;
    }

    sendf("mdf_trigger reason=%c raw=%i df=%i d0=%i",
          mdf.trigger_reason, raw, df, d0);
}

static uint_fast8_t
mdf_timer_event(struct timer *t)
{
    if (!mdf.active || mdf.triggered)
        return SF_DONE;

    int32_t raw = load_cell_probe_get_last_raw_sample(mdf.lce);
    int32_t df = raw - mdf.prev_raw;
    int32_t d0 = raw - mdf.raw0;

    mdf.last_raw = raw;
    mdf.last_df = df;
    mdf.last_d0 = d0;

    if (abs(df) >= mdf.df_threshold) {
        mdf_do_trigger(MDF_REASON_DF, raw, df, d0);
        return SF_DONE;
    }

    if (abs(d0) >= mdf.safe_threshold) {
        mdf_do_trigger(MDF_REASON_SAFE, raw, df, d0);
        return SF_DONE;
    }

    mdf.prev_raw = raw;
    mdf.timer.waketime += mdf.sample_ticks;
    return SF_RESCHEDULE;
}

void
command_mdf_ping(uint32_t *args)
{
    sendf("mdf_pong value=%u", 117);
}
DECL_COMMAND(command_mdf_ping, "mdf_ping");

void
command_mdf_force_query(uint32_t *args)
{
    uint8_t oid = args[0];
    struct load_cell_probe *lce = load_cell_probe_oid_lookup(oid);
    int32_t raw = load_cell_probe_get_last_raw_sample(lce);

    sendf("mdf_force_state oid=%c raw=%i", oid, raw);
}
DECL_COMMAND(command_mdf_force_query, "mdf_force_query oid=%c");

void
command_mdf_config(uint32_t *args)
{
    uint8_t oid = args[0];

    mdf.lce = load_cell_probe_oid_lookup(oid);
    mdf.sample_ticks = args[1];
    mdf.df_threshold = args[2];
    mdf.safe_threshold = args[3];

    mdf.active = 0;
    mdf.triggered = 0;
    mdf.trigger_reason = MDF_REASON_NONE;
    mdf.use_trsync = 0;
    mdf.ts = NULL;

    mdf.timer.func = mdf_timer_event;
}
DECL_COMMAND(command_mdf_config,
    "mdf_config oid=%c sample_ticks=%u df_threshold=%i safe_threshold=%i");

void
command_mdf_start(uint32_t *args)
{
    if (!mdf.lce)
        shutdown("MDF not configured");

    int32_t raw = load_cell_probe_get_last_raw_sample(mdf.lce);

    sched_del_timer(&mdf.timer);

    mdf.raw0 = raw;
    mdf.prev_raw = raw;
    mdf.last_raw = raw;
    mdf.last_df = 0;
    mdf.last_d0 = 0;
    mdf.triggered = 0;
    mdf.trigger_reason = MDF_REASON_NONE;
    mdf.active = 1;

    // Monitor/debug mode: report mdf_trigger asynchronously, do not stop motion.
    mdf.use_trsync = 0;
    mdf.ts = NULL;

    mdf.timer.waketime = timer_read_time() + mdf.sample_ticks;
    mdf.timer.func = mdf_timer_event;
    sched_add_timer(&mdf.timer);
}
DECL_COMMAND(command_mdf_start, "mdf_start");

void
command_mdf_probe_start(uint32_t *args)
{
    struct load_cell_probe *lce = load_cell_probe_oid_lookup(args[0]);
    int32_t raw = load_cell_probe_get_last_raw_sample(lce);

    sched_del_timer(&mdf.timer);

    mdf.lce = lce;
    mdf.ts = trsync_oid_lookup(args[1]);
    mdf.trsync_trigger_reason = args[2];
    mdf.trsync_error_reason = args[3];

    mdf.sample_ticks = args[4];
    mdf.df_threshold = args[5];
    mdf.safe_threshold = args[6];

    mdf.raw0 = raw;
    mdf.prev_raw = raw;
    mdf.last_raw = raw;
    mdf.last_df = 0;
    mdf.last_d0 = 0;

    mdf.triggered = 0;
    mdf.trigger_reason = MDF_REASON_NONE;
    mdf.active = 1;
    mdf.use_trsync = 1;

    mdf.timer.waketime = timer_read_time() + mdf.sample_ticks;
    mdf.timer.func = mdf_timer_event;
    sched_add_timer(&mdf.timer);
}
DECL_COMMAND(command_mdf_probe_start,
    "mdf_probe_start oid=%c trsync_oid=%c trigger_reason=%c error_reason=%c"
    " sample_ticks=%u df_threshold=%i safe_threshold=%i");

void
command_mdf_stop(uint32_t *args)
{
    sched_del_timer(&mdf.timer);
    mdf.active = 0;
    mdf.use_trsync = 0;
    mdf.ts = NULL;
}
DECL_COMMAND(command_mdf_stop, "mdf_stop");

void
command_mdf_status_query(uint32_t *args)
{
    sendf("mdf_status active=%c triggered=%c reason=%c raw0=%i raw=%i df=%i d0=%i",
          mdf.active, mdf.triggered, mdf.trigger_reason,
          mdf.raw0, mdf.last_raw, mdf.last_df, mdf.last_d0);
}
DECL_COMMAND(command_mdf_status_query, "mdf_status_query");
