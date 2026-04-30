#pragma once

void  imu_init();
void  imu_update();        // call every loop iteration
float imu_roll_deg();      // heel angle — positive = starboard heel
float imu_pitch_deg();     // pitch angle — positive = bow up
bool  imu_is_capsized();   // true if roll > 110° sustained > 2 s
