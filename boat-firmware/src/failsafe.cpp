// failsafe.cpp — link-loss detection and safe-state flag.
//
// Polls elrs_link_ok() each loop iteration. When the link goes away,
// sets s_active = true and logs the event. When it returns, clears the
// flag. main.cpp checks failsafe_active() and commands the safe servo
// positions (defined in config.h failsafe_pos namespace).
//
// elrs_link_ok() returns false in the Phase 3 stub (no CRSF hardware yet),
// so failsafe will always be active in ELRS mode until Phase 3 is done.
// That is intentional — the boat cannot be controlled without a working
// ELRS receive path.

#include "failsafe.h"
#include "elrs.h"
#include <Arduino.h>

static bool s_active = false;

void failsafe_init()
{
    s_active = false;
}

void failsafe_update()
{
    bool link = elrs_link_ok();

    if (!link && !s_active) {
        s_active = true;
        Serial.println("failsafe: LINK LOST — motor off, sail eased, rudder hard over");
    } else if (link && s_active) {
        s_active = false;
        Serial.println("failsafe: link restored");
    }
}

bool failsafe_active() { return s_active; }
