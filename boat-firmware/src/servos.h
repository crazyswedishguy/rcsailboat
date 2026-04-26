#pragma once

void  servos_init();
void  servos_set(int pca_channel, float value);        // value: -1.0..+1.0 (0.0..+1.0 for sail)
float servos_get_commanded(int pca_channel);           // last value sent to this channel
