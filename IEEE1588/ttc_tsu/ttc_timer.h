#pragma once
#include <stdio.h>
#include "xttcps.h"
#include "xscugic.h"
#include "xemacps.h"


typedef struct {
    XTtcPs *ttcps_tick;
} xttc_handler_args_t;

void register_ttc_handler(void (*cal));
void tick_handler(void *CallBackRef);
int init_ttc_timer();
void ttcps_start();
void ttcps_stop();





