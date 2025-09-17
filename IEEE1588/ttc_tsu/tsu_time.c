#include "tsu_time.h"
#include "ttc_timer.h"

#include "xstatus.h"
#include "xparameters.h"
#include "xparameters_ps.h"
#include "xil_io.h"



#define TSU_CLK_FREQ 	XPAR_PSU_ETHERNET_0_ENET_TSU_CLK_FREQ_HZ

#define GEM_TSU_INC_CTRL 0xff0b01c4
#define GEM_CLK_CTRL 0x00FF180308

#define TSU_TIMER_INCR_GEM0 0xff0b01dc
#define TSU_TIMER_INCR_SUB_NS_GEM0 0xff0b01bc
#define TSU_TIMER_SEC_GEM0 0xff0b01d0
#define TSU_TIMER_NS_GEM0 0xff0b01d4
#define TSU_TIMER_ADJUST 0xff0b01d8
#define GEM_TSU_REF_CTRL 0xff5e0100


static tsu_adjustment fine_tune_register = { .word = 0 };
static volatile uint32_t sUptime_mS = 0;

#define KP 0.0005 // 0.0005    // 比例系数，建议初值 0.01~0.2
#define KI 0.001   // 积分系数，建议初值 0.001~0.02
#define ACCUM_MAX 200000
#define LIMIT_ADUUST_STEP 100000


#define TTC_TEST

#ifdef TTC_TEST
static u32 tsu_sec;
static u32 tsu_ns;
static uint8_t is_read = 0;
#endif


FASTCODE void tsu_clock_fine_tune(const int error_ns, int32_t sync_diff_integrated_ns, const int boundary)
{
  #if 1
    int32_t freq_adj = error_ns + sync_diff_integrated_ns / 20;
    // int32_t freq_adj = error_ns + sync_diff_integrated_ns;
    const int error_ns_abs = abs(freq_adj);
    if(error_ns_abs < boundary && error_ns > -boundary)
        return;

    /* adjust the RTC in every msec, so the drift value is the half of the error/1000 */
    fine_tune_register.direction = freq_adj > 0 ? ADJUST_ADD : ADJUST_SUBTRACT;

    fine_tune_register.nanosec = error_ns_abs; 
    // xil_printf("ns abs :: %d\r\n", error_ns_abs);
  #else
    const int error_ns_abs = abs(error_ns);
    if(error_ns_abs < boundary )
        return;

    uint32_t freq_adj = abs(error_ns + sync_diff_integrated_ns / 20);
    /* adjust the RTC in every msec, so the drift value is the half of the error/1000 */
    fine_tune_register.direction = error_ns > 0 ? ADJUST_ADD : ADJUST_SUBTRACT;

    fine_tune_register.nanosec = freq_adj; 
  #endif


}

FASTCODE u32 tsu_get_uptime_ms(void)
{
  return sUptime_mS;
}

FASTCODE uint64_t tsu_get_tick_cnt(void)
{
  return sUptime_mS;
}

FASTCODE void tsu_driver_adjust_time(tsu_adjustment adjust)
{
    Xil_Out32(TSU_TIMER_ADJUST, adjust.word);
    // printf(" write :: %d read ::  %d\r\n", adjust.word, Xil_In32(TSU_TIMER_ADJUST));
}

//TODO: Register this rtc_systick_cb to RTOS systick hook!
FASTCODE void tsu_systick_cb(void)
{
    sUptime_mS++;
    tsu_driver_adjust_time(fine_tune_register);
}


void print_tsu_local_time()
{
  // xil_printf("print_tsu_local_time :: %u.%u\r\n", Xil_In32(TSU_TIMER_SEC_GEM0), Xil_In32(TSU_TIMER_NS_GEM0));
  tsu_sec = Xil_In32(TSU_TIMER_SEC_GEM0);
  tsu_ns  = Xil_In32(TSU_TIMER_NS_GEM0);
#ifdef TTC_TEST
  is_read = 1;
#endif
}

void read_tsu_local_time()
{
  xil_printf("print_tsu_local_time :: %us  %uns \r\n", tsu_sec, tsu_ns);
  is_read = 0;
}

int8_t is_readable()
{
  return is_read;
}

void init_tsu(void (*ttc_handle))
{
	int Status = XST_SUCCESS;

  #if 0  // remove ttc 20250912 @hjk
  register_ttc_handler(ttc_handle);
	if (init_ttc_timer()  != XST_SUCCESS) while(1);
	#endif

	u32 value = 0x00010601;
	Xil_Out32(GEM_TSU_REF_CTRL, value);
	value = 0x00010601 | 0x01000000;
	Xil_Out32(GEM_TSU_REF_CTRL, value);

	/* Nanosecond and sub-nanosecond increments per clock cycle */
	double ns_per_cycle = 1e9 / TSU_CLK_FREQ;
	u32 ns_int = (u32)ns_per_cycle;
	u32 sub_ns = (u32)((ns_per_cycle - ns_int) * (1 << 24)); // 24位小数精度
	xil_printf("write TSU_TIMER_INCR_GEM0 :: 0x%2.2x\r\nTSU_TIMER_INCR_SUB_NS_GEM0 :: 0x%2.2x\r\n", ns_int, sub_ns);
	Xil_Out32(TSU_TIMER_INCR_GEM0, ns_int);   // 0x04
	Xil_Out32(TSU_TIMER_INCR_SUB_NS_GEM0, sub_ns); // 0x00
	
	/*	clear tsu counter	*/
	Xil_Out32(TSU_TIMER_NS_GEM0, 0);
	Xil_Out32(TSU_TIMER_SEC_GEM0, 0);

  xil_printf("clear tsu time :: %u.%u\r\n", Xil_In32(TSU_TIMER_NS_GEM0), Xil_In32(TSU_TIMER_SEC_GEM0));

}

#if 0  // remove 20250912 @hjk
void tsu_start()
{
  ttcps_start();
}

void tsu_stop()
{
  ttcps_stop();
}
#endif

void tsu_get_time(tsu_timestamp_t *out)
{ 
  u64 first_sec = Xil_In32(TSU_TIMER_SEC_GEM0);
  u32 nsec = Xil_In32(TSU_TIMER_NS_GEM0);
  u64 second_sec = Xil_In32(TSU_TIMER_SEC_GEM0);
  
  if (first_sec != second_sec)
  {
    nsec = 0;
  }
  
  out->seconds = second_sec;
  out->nanosec = nsec;
}

void tsu_set_time(tsu_timestamp_t *ts)
{
  Xil_Out32(TSU_TIMER_NS_GEM0, ts->nanosec);
	Xil_Out32(TSU_TIMER_SEC_GEM0, ts->seconds);
}
