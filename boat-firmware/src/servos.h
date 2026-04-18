#pragma once

void servos_init();
// value: normalized -1.0 .. +1.0 (or 0.0 .. +1.0 for sail)
void servos_set(int pca_channel, float value);
