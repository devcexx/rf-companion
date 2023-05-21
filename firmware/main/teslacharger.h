#ifndef TESLACHARGER_H
#define TESLACHARGER_H

#include "driver/gpio.h"

void tesla_charger_open_door_sync(gpio_num_t gpio);

#endif
