#include "servos.h"

static float s_commanded[16] = {};  // one slot per PCA9685 channel

void servos_init() {}

void servos_set(int pca_channel, float value) {
    (void)pca_channel;
    (void)value;
    if (pca_channel >= 0 && pca_channel < 16)
        s_commanded[pca_channel] = value;
}

float servos_get_commanded(int pca_channel) {
    if (pca_channel < 0 || pca_channel >= 16) return 0.0f;
    return s_commanded[pca_channel];
}
