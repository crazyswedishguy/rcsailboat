// Transparent USB-CDC <-> UART1 byte pump.
//
// Serial  = native USB-CDC to the Pi (enumerates as /dev/ttyACM0).
// Serial1 = hardware UART to the Ranger Micro CRSF pin, fixed at the rate
//           the handset actually runs (420000), independent of whatever
//           baud the Pi-side pyserial happens to open the CDC port at.
//
// CRSF is half-duplex single-wire: Serial1 RX will also see our own TX
// echoed back over the shared line. That's expected — the Pi-side parser
// already ignores frame type 0x16 (RC_CHANNELS_PACKED) on receive.

#include <Arduino.h>

namespace pins {
constexpr int CRSF_TX = 43;  // D6 -> series resistor -> Ranger Micro CRSF pin
constexpr int CRSF_RX = 44;  // D7 -> Ranger Micro CRSF pin (direct)
}  // namespace pins

constexpr uint32_t CRSF_BAUD = 400000;

void setup() {
  Serial.begin(115200);
  Serial1.begin(CRSF_BAUD, SERIAL_8N1, pins::CRSF_RX, pins::CRSF_TX);
}

void loop() {
  while (Serial.available()) {
    Serial1.write(Serial.read());
  }
  while (Serial1.available()) {
    Serial.write(Serial1.read());
  }
}
