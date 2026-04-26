#pragma once

void  power_init();
void  power_update();      // call every loop iteration
float power_voltage_v();
float power_current_a();
float power_mah_used();    // accumulated since boot
