#pragma once

void  elrs_init();
void  elrs_update();
float elrs_get_channel(int ch);  // returns normalized value for channel ch (0-based)
bool  elrs_link_ok();
