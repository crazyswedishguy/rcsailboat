// diag.cpp — I²C peripheral diagnostics.

#include "diag.h"
#include "config.h"
#include <Arduino.h>
#include <Wire.h>

static DeviceInfo s_dev[DEV_COUNT] = {
    {"Touch Screen",    "Display input",      i2c_addr::FT3168,  false, false},
    {"IMU",             "Roll & pitch",       i2c_addr::QMI8658, false, false},
    {"Servo Driver",    "Servos & ESC",       i2c_addr::PCA9685, false, false},
    {"Current Sensor",  "Battery monitor",    i2c_addr::INA228,  false, false},
};

static diag_reinit_fn   s_reinit[DEV_COUNT]  = {};
static volatile uint8_t s_reprobe_req        = DEV_COUNT;  // DEV_COUNT = none

// Recover a physically-stuck I²C bus.
// Wire.end()+Wire.begin() alone does not help when a device is holding SDA low
// (mid-transaction): the i2c-ng driver immediately returns INVALID_STATE on the
// first call because it sees the bus as busy.  The fix is to bit-bang up to 9
// SCL pulses — enough to clock any stuck device through its current byte and
// release SDA — then issue a STOP before reinitialising Wire.
void i2c_recover_bus()
{
    Wire.end();

    // Release SDA (input + pull-up) and take SCL as output.
    pinMode(pins::I2C_SDA, INPUT_PULLUP);
    pinMode(pins::I2C_SCL, OUTPUT);
    digitalWrite(pins::I2C_SCL, HIGH);
    delayMicroseconds(10);

    // Clock up to 9 times while SDA is still held low by a stuck device.
    for (int i = 0; i < 9; i++) {
        if (digitalRead(pins::I2C_SDA) != LOW) break;
        digitalWrite(pins::I2C_SCL, LOW);
        delayMicroseconds(5);
        digitalWrite(pins::I2C_SCL, HIGH);
        delayMicroseconds(5);
    }

    // Issue a STOP condition (SDA LOW→HIGH while SCL is HIGH).
    pinMode(pins::I2C_SDA, OUTPUT);
    digitalWrite(pins::I2C_SDA, LOW);
    delayMicroseconds(5);
    digitalWrite(pins::I2C_SCL, HIGH);
    delayMicroseconds(5);
    digitalWrite(pins::I2C_SDA, HIGH);
    delayMicroseconds(5);

    // Release both pins before handing them back to the I2C peripheral.
    pinMode(pins::I2C_SDA, INPUT);
    pinMode(pins::I2C_SCL, INPUT);
    delayMicroseconds(50);

    Wire.begin(pins::I2C_SDA, pins::I2C_SCL);
    Wire.setClock(I2C_FREQ_HZ);
}

// Send address byte and check for ACK.  Recovers the bus on NACK so the next
// probe starts with a clean i2c-ng driver state.
static bool probe_one(uint8_t addr)
{
    // Write 1 byte (reg 0x00) so Wire calls i2c_master_transmit() rather than
    // i2c_master_probe().  Probe-only sends only the address phase; some chips
    // (including FT3168 on this board) appear not to ACK during a bare address
    // probe but do respond correctly to an actual write transaction.
    Wire.beginTransmission(addr);
    Wire.write((uint8_t)0x00);
    bool ok = (Wire.endTransmission(true) == 0);
    if (!ok) i2c_recover_bus();
    return ok;
}

void diag_register_reinit(DevId id, diag_reinit_fn fn) { s_reinit[id] = fn; }

void diag_init()
{
    // FT3168 needs up to 300 ms to start responding after power-on.
    // Default Wire timeout is 50 ms (too short); use 500 ms here.
    Wire.setTimeOut(500);
    Serial.println("diag: I\xC2\xB2""C scan");
    for (uint8_t i = 0; i < DEV_COUNT; i++) {
        bool found = probe_one(s_dev[i].addr);
        s_dev[i].present = found;
        s_dev[i].enabled = found;
        Serial.printf("  %-8s 0x%02X  %s\n",
                      s_dev[i].name, s_dev[i].addr, found ? "OK" : "absent");
    }
    Wire.setTimeOut(50);  // restore default for normal operation
}

void diag_reprobe(DevId id)
{
    bool found = probe_one(s_dev[id].addr);
    s_dev[id].present = found;
    if (found) {
        s_dev[id].enabled = true;
        if (s_reinit[id]) s_reinit[id]();
        Serial.printf("diag: reprobe %-8s  OK — reinit done\n", s_dev[id].name);
    } else {
        s_dev[id].enabled = false;
        Serial.printf("diag: reprobe %-8s  still absent\n", s_dev[id].name);
    }
}

void  diag_request_reprobe(DevId id) { s_reprobe_req = (uint8_t)id; }
DevId diag_pending_reprobe()         { return (DevId)s_reprobe_req; }
void  diag_clear_reprobe()           { s_reprobe_req = DEV_COUNT; }

bool              diag_ok(DevId id)   { return s_dev[id].present && s_dev[id].enabled; }
const DeviceInfo *diag_info(DevId id) { return &s_dev[id]; }
