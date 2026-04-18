#include <Arduino.h>

#include "elrs.h"
#include "failsafe.h"
#include "servos.h"
#include "telemetry.h"

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("rcsailboat firmware v0 — Phase 0 scaffold");

    elrs_init();
    servos_init();
    telemetry_init();
    failsafe_init();
}

void loop() {
    elrs_update();
    failsafe_update();
    telemetry_update();
}
