#include "ttc_timer.h"
#include "xttcps.h"

#include "../../fml_interrupt.h"

/************************** Constant Definitions *****************************/

/*
 * The following constants map to the XPAR parameters created in the
 * xparameters.h file. They are only defined here such that a user can easily
 * change all the needed parameters in one place.
 */
#define TTC_TICK_DEVICE_ID	XPAR_XTTCPS_1_DEVICE_ID
#define TTC_TICK_INTR_ID	XPAR_XTTCPS_1_INTR

/*
 * Constants to set the basic operating parameters.
 * PWM_DELTA_DUTY is critical to the running time of the test. Smaller values
 * make the test run longer.
 */
#define	TICK_TIMER_FREQ_HZ	1000// 100000000 // 1000  /* Tick timer counter's output frequency  */

#define TICKS_PER_CHANGE_PERIOD	 1000 // 1000000000/* Tick signals per update */


/**************************** Type Definitions *******************************/
typedef struct {
	u32 OutputHz;	/* Output frequency */
	XInterval Interval;	/* Interval value */
	u8 Prescaler;	/* Prescaler value */
	u16 Options;	/* Option settings */
} TmrCntrSetup;


/************************** Variable Definitions *****************************/

static XTtcPs TtcPsInst;  /* Timer counter instance */
static xttc_handler_args_t sXttc_handler;

static TmrCntrSetup SettingsTable=
	{TICK_TIMER_FREQ_HZ, 0, 0, 0};	/* Ticker timer counter initial setup,
						only output freq */
static u32 TickCount = 0;		/* Ticker interrupts between seconds change */
static u8 ErrorCount;		/* Errors seen at interrupt time */
static void (*TickCallback)() = NULL;

static u16 Interval;
static u8 Prescaler;


static int setup_timer(int DeviceID);


int setup_timer(int DeviceID)
{
	int Status;
	XTtcPs_Config *Config;
	XTtcPs *Timer;
	TmrCntrSetup *TimerSetup;

	TimerSetup = &SettingsTable;

	Timer = &TtcPsInst;

	/*
	 * Look up the configuration based on the device identifier
	 */
	Config = XTtcPs_LookupConfig(DeviceID);
	if (NULL == Config) {
		return XST_FAILURE;
	}

	/*
	 * Initialize the device
	 */
	Status = XTtcPs_CfgInitialize(Timer, Config, Config->BaseAddress);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/*
	 * Set the options
	 */
	XTtcPs_SetOptions(Timer, TimerSetup->Options);

	/*
	 * Timer frequency is preset in the TimerSetup structure,
	 * however, the value is not reflected in its other fields, such as
	 * IntervalValue and PrescalerValue. The following call will map the
	 * frequency to the interval and prescaler values.
	 */
	XTtcPs_CalcIntervalFromFreq(Timer, TimerSetup->OutputHz,
		&(TimerSetup->Interval), &(TimerSetup->Prescaler));

	/*
	 * Set the interval and prescale
	 */
	XTtcPs_SetInterval(Timer, TimerSetup->Interval);
	XTtcPs_SetPrescaler(Timer, TimerSetup->Prescaler);

	return XST_SUCCESS;
}

int setup_ticker()
{
	int Status;
	TmrCntrSetup *TimerSetup;
	XTtcPs *TtcPsTick = sXttc_handler.ttcps_tick;
	TimerSetup = &SettingsTable;

	/*
	 * Set up appropriate options for Ticker: interval mode without
	 * waveform output.
	 */
	TimerSetup->Options |= (XTTCPS_OPTION_INTERVAL_MODE |
					      XTTCPS_OPTION_WAVE_DISABLE);
	Status = setup_timer(TTC_TICK_DEVICE_ID);
	if(Status != XST_SUCCESS) {
		return Status;
	}
	Status = InterruptConnect(TTC_TICK_INTR_ID,
		(Xil_InterruptHandler)tick_handler, (void *)TtcPsTick);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}
	/*
	 * Enable the interrupts for the tick timer/counter
	 * We only care about the interval timeout.
	 */
	XTtcPs_EnableInterrupts(TtcPsTick, XTTCPS_IXR_INTERVAL_MASK);

	return Status;
}


/***************************************************************************/
/**
*
* This function is the handler which handles the periodic tick interrupt.
* It updates its count, and set a flag to signal PWM timer counter to
* update its duty cycle.
*
* This handler provides an example of how to handle data for the TTC and
* is application specific.
*
* @param	CallBackRef contains a callback reference from the driver, in
*		this case it is the instance pointer for the TTC driver.
*
* @return	None.
*
*****************************************************************************/
// #define TSU_TIMER_SEC_GEM0 0xff0b01d0
// #define TSU_TIMER_NS_GEM0 0xff0b01d4
// static uint32_t tsu_sec[10000];
// static uint32_t tsu_ns[10000];
// static uint32_t ns_diff[10000];

void tick_handler(void *CallBackRef)
{
	#if 0
	tsu_sec[TickCount] = Xil_In32(TSU_TIMER_SEC_GEM0);
	tsu_ns[TickCount] = Xil_In32(TSU_TIMER_NS_GEM0);
	if (TickCount > 1)
	{
		ns_diff[TickCount] = tsu_ns[TickCount] - tsu_ns[TickCount-1];
	}
	#endif
	
	u32 StatusEvent;
	XTtcPs *TtcPsTick = (XTtcPs *)CallBackRef;
	/*
	 * Read the interrupt status, then write it back to clear the interrupt.
	 */
	StatusEvent = XTtcPs_GetInterruptStatus(TtcPsTick);
	XTtcPs_ClearInterruptStatus(TtcPsTick, StatusEvent);
	// TickCount++;
	// if (TickCount >= 50)
	// {
	// 	TickCount %= 50;
	// }
	TickCallback();
	// if (0 != (XTTCPS_IXR_INTERVAL_MASK & StatusEvent)) {
	// 	TickCount++;
	// 	if (TICKS_PER_CHANGE_PERIOD == TickCount) {
	// 		TickCount = 0;
	// 		TickCallback();
	// 	}

	// }
	// else {
	// 	/*
	// 	 * The Interval event should be the only one enabled. If it is
	// 	 * not it is an error
	// 	 */
	// 	ErrorCount++;
	// }
	// #endif
}

int init_ttc_timer()
{
	int Status;
	/*
	 * Connect the Intc to the interrupt subsystem such that interrupts can
	 * occur.  This function is application specific.
	 */
	sXttc_handler.ttcps_tick = &TtcPsInst;

	Status = setup_ticker();
	if (Status != XST_SUCCESS) {
		return Status;
	}
	return Status;
}

void ttcps_start()
{
	/*
	 * Start the tick timer/counter
	 */
	XTtcPs_Start(&TtcPsInst);
	xil_printf("======== ttc start \r\n");
}

void ttcps_stop()
{
	XTtcPs_Stop(&TtcPsInst);	
}

void register_ttc_handler(void (*cal))
{
	TickCallback = cal;
}