#include "timesync.h"
#include "timesync_private.h"
#include "ptp.h"
#include "gptp.h"

#include "ttc_tsu/tsu_time.h"
#include "../fml_udp_log.h"

#include <assert.h>

// Set coarse time in every Nth cycle if no fine time service available
#define RESYNC_COARSE_TIME 60

// Timeout of the services in seconds
#define COARSE_TIMEOUT_SEC 300
#define FINE_TIMEOUT_SEC 20

// PPS period boundaries
#define PPS_PERIOD_HIGH 1050
#define PPS_PERIOD_LOW 950

// The time connection between PPS rise and the processing. Todo: calibrate it!
#define PPS_LATENCY_CORRECTION_NS 1200

// The age in milliseconds of the coarse time value extracted from GPRMC message
#define COARSE_TIME_AGE_MAX 1000

// The border in nanoseconds using hard set or drift algo in PPS interrupt handler
#define SYNC_DIFF_BOUNDARY_NS 10000

// The boundary of fine correction in nanoseconds
#define PPS_CORRECTION_BOUNDARY 50

static bool isInitialized = false;
static timesync_state_e status = TS_NEVER;
static bool coarse_service_ok = false;
static time_t last_coarse_time_value;
static uint32_t last_coarse_time_stamp;
static uint32_t last_pps_time_stamp;

#ifdef PTP_TEST
static bool sMonitor = false;
#else
static bool sMonitor = false;
#endif

static int sync_diff_ns;
static int sync_diff_integrated_ns = 0;
static clock_type clock_source = CLOCK_TYPE_NONE;

static void coarse_time_received(const time_t timestamp, const uint32_t nanoseconds)
{
    static int receive_counter = 0;

    coarse_service_ok = true;
    last_coarse_time_value = timestamp;
    last_coarse_time_stamp = tsu_get_uptime_ms();
    if(status == TS_NEVER || status == TS_LOST ||
            (status == TS_COARSE && ++receive_counter > RESYNC_COARSE_TIME))
    {
        if(timestamp > 0)
        {
            tsu_timestamp_t tsu_timestamp;
            if(nanoseconds > 0)
                tsu_timestamp.nanosec = nanoseconds;
            else
                tsu_get_time(&tsu_timestamp);    //keep the msec
            tsu_timestamp.seconds = timestamp;
            tsu_set_time(&tsu_timestamp);
        }
        receive_counter = 0;
        if(status != TS_FINE)
            status = TS_COARSE;
    }
}

static void fine_time_received(const time_t timestamp, const uint32_t nanoseconds)
{
    if(timestamp > 0)
    {
        tsu_timestamp_t tsu_timestamp;
        tsu_timestamp.seconds = timestamp;
        tsu_timestamp.nanosec = nanoseconds;
        tsu_set_time(&tsu_timestamp);
    }
    status = TS_FINE;
}

static void pps_received()
{
    const uint32_t current_stamp = tsu_get_uptime_ms();
    const int prev_pps_age = (int)(current_stamp - last_pps_time_stamp);

    last_pps_time_stamp = current_stamp;
    if(prev_pps_age < PPS_PERIOD_LOW || prev_pps_age > PPS_PERIOD_HIGH)
        return;

    if(status == TS_FINE)
    {
        tsu_timestamp_t t;
        tsu_get_time(&t);
        sync_diff_ns = t.nanosec - PPS_LATENCY_CORRECTION_NS;

        if(sync_diff_ns > (int)(500 * TSU_NS_IN_MS))
            sync_diff_ns -= (int)(1000 * TSU_NS_IN_MS);

        if(sync_diff_ns < -SYNC_DIFF_BOUNDARY_NS || sync_diff_ns > SYNC_DIFF_BOUNDARY_NS)  // 超出边界 硬校准
        {
            // out of boundaries, use hard time set
            const int coarse_stamp_age = (int)(current_stamp - last_coarse_time_stamp);
            if(coarse_stamp_age >= COARSE_TIME_AGE_MAX)
                return;
            tsu_timestamp_t tsu_timestamp =
            {
                .seconds = (uint32_t)(last_coarse_time_value + 1),     // last message contains the previous second
                .nanosec = PPS_LATENCY_CORRECTION_NS
            };
            tsu_set_time(&tsu_timestamp);
            sync_diff_integrated_ns = 0;
        }
        else     // we are within the boundaries, fine tune the clock
        {
            const int corr = (sync_diff_integrated_ns - sync_diff_ns) / 10;
            sync_diff_integrated_ns -= corr;
            tsu_clock_fine_tune(sync_diff_integrated_ns, 0, PPS_CORRECTION_BOUNDARY);
        }
    }

    if(status == TS_COARSE)
    {
        status = TS_FINE;
    }
}

static void coarse_timeout()
{
    if(status == TS_COARSE)
        status = TS_LOST;
    coarse_service_ok = false;
}

static void fine_timeout()
{
    if(status == TS_FINE)
    {
        if(coarse_service_ok)
            status = TS_COARSE;
        else
            status = TS_LOST;
    }
}

static void log_state()
{
    static const char* state_txt[] =
    {
        [TS_NEVER] = "never synced",
        [TS_COARSE] = "coarse sync",
        [TS_FINE] = "fine sync",
        [TS_LOST] = "sync lost"
    };
    xil_printf("Timesync state: %s\r\n", state_txt[status]);
}


static void housekeeping()
{
    static timesync_state_e status_prev = TS_NEVER;

    if(status != status_prev)
    {
        status_prev = status;
        log_state();
    }

    if(sMonitor && sync_diff_ns)
    {
        xil_printf("Diff:%d, avg:%d\r\n", sync_diff_ns, sync_diff_integrated_ns);
        sync_diff_ns = 0;
    }
}

static void update_error(const int error_ns, const int avg_error_ns)
{
    sync_diff_ns = error_ns;
    sync_diff_integrated_ns = avg_error_ns;
}


static struct timesync_init_param init_param =
{
    // .timesource_name = NO_TIME_SERVICE,
    .coarse_timeout_sec = COARSE_TIMEOUT_SEC,
    .fine_timeout_sec = FINE_TIMEOUT_SEC,
    .coarse_time_received = coarse_time_received,
    .fine_time_received = fine_time_received,
    .pps_received = pps_received,
    .coarse_timeout = coarse_timeout,
    .fine_timeout = fine_timeout,
    .housekeeping = housekeeping,
    .cleanup_function = NULL,
    .error_report = update_error
};


static void start_timesync()
{
  switch (clock_source)
  {
    case CLOCK_TYPE_PTP:
    ptp_init(&init_param);
    break;
    case CLOCK_TYPE_GPTP:  
    gptp_init(&init_param);
    break;
    case CLOCK_TYPE_NONE:
    break;
    
    default:
    break;
  }
  init_param.cleanup_function = ptp_cleanup;
}

void timesync_init(clock_type type)
{
  assert(!isInitialized);
  clock_source = type;
  start_timesync();
  isInitialized = true;
}