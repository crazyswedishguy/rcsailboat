# SX1262 LoRa bring-up notes (2026-07-24)

First hardware bring-up of the boat ↔ XIAO SX1262 LoRa link, from "radio doesn't
respond at all" to a verified working RC + telemetry round-trip. Kept as a
reference for the pin/timing decisions below and as a record of what NOT to
redo if the radio stack is touched again — several of these bugs look like
plausible things to reintroduce by accident.

This link has since moved from LoRa to FSK modulation (see git history after
this point) for higher throughput at the shorter range this project needs.
These notes describe the LoRa-era implementation and the debugging path that
got it working; most of the underlying pitfalls (SPIClass virtual dispatch,
DIO1 GPIO polling inside RadioLib) apply equally to the FSK implementation.

## Starting point

- `SX1262 begin failed (err -2)` on the boat, every time, regardless of
  wiring changes, resoldered headers, or swapping the physical SX1262 module
  with the XIAO's (which worked fine). That module swap was the key test: it
  proved the fault traveled with the *board*, not the module or the wiring.
- The XIAO's SX1262 crashed on boot with `assert failed: xQueueSemaphoreTake`
  the first time its firmware was touched during this same session.

## Bug 1 — GPIO42 was never actually free

`config.h` originally assigned SX1262 DIO1 to GPIO42 on the boat, following
`docs/pinmap.md`. GPIO42 turned out to be reserved internally on the Waveshare
board as `AMOLED_EN` (display enable) — not broken out to a header at all.
Worse: once that's accounted for, **the boat board has zero free GPIOs**.
GPIO22–25 don't exist on the ESP32-S3 die; GPIO26–37 are consumed by the
internal flash/octal-PSRAM SPI bus; every other pin was already allocated.

Fix: leave DIO1 physically unconnected. RadioLib accepts `RADIOLIB_NC` for
the irq pin. The boat's `elrs_update()` polls the IRQ status register over
SPI (`getIrqFlags()`) once per `loop()` instead of using a hardware
interrupt. The XIAO keeps DIO1 wired (GPIO2/D1) — it has free GPIOs.

## Bug 2 — `SoftSPI : public SPIClass` never actually bit-banged anything

The real root cause of the `err -2` / all-zero SPI readback, reproduced
identically across two physical modules and a header resolder (both of which
looked like wiring fixes but changed nothing, because the fault wasn't in the
wiring).

`SPIClass`'s `begin()/beginTransaction()/transfer()/endTransaction()` are
**not virtual**. RadioLib's `ArduinoHal` stores the SPI object as a plain
`SPIClass*` and calls through that base-class pointer. A class that merely
inherits from `SPIClass` and "overrides" `transfer()` is invisible to
RadioLib at runtime — every call resolves to `SPIClass`'s real hardware
implementation at compile time, regardless of the object's actual runtime
type. Every SPI transaction was silently going to the unconfigured onboard
HSPI peripheral instead of the bit-banged GPIOs. No amount of correct wiring
could have fixed this.

**Fix:** `RadioLibHal` (`Hal.h`) is RadioLib's actual extension point for
non-standard SPI — its `spiBegin/spiBeginTransaction/spiTransfer/
spiEndTransaction/spiEnd` are genuinely virtual. Subclass that instead
(`BitBangHal` in `boat-firmware/src/elrs.cpp`), and construct `Module` via
its `Module(RadioLibHal*, cs, irq, rst, gpio)` constructor.

**Lesson: never subclass `SPIClass` to fake bit-bang SPI on this framework.
Subclass `RadioLibHal`.**

## Bug 3 — same mistake, different shape, on the XIAO

`static SPIClass s_spi(SPI);` in `crsf-bridge/src/main.cpp` was meant to
alias the framework's global `SPI` object. `SPIClass` has no constructor
taking an `SPIClass&`, so this silently invoked the compiler-generated
*copy* constructor instead — copying `SPI`'s internal FreeRTOS mutex handle
at static-init time, before `SPI`'s own constructor (a different translation
unit, unspecified init order across `.cpp` files in C++) was guaranteed to
have run. The copy's mutex handle was null. First `beginTransaction()` call
crashed on `xQueueSemaphoreTake` against it.

**Fix:** use the global `SPI` object directly instead of copy-constructing a
second one.

## Bug 4 — RadioLib's blocking `transmit()` polls DIO1 directly, not over SPI

Once RX started working, every boat telemetry reply logged
`TX error -5` (`RADIOLIB_ERR_TX_TIMEOUT`) — even with the antenna properly
seated. `SX126x::transmit()`'s wait loop checks
`hal->digitalRead(mod->getIrq())` directly — i.e. it polls the DIO1 GPIO
pin itself, not the SPI-based IRQ status register. DIO1 is intentionally
`RADIOLIB_NC` on the boat (see Bug 1), so that read always returns 0 and the
loop spins to timeout on every call, regardless of whether the transmission
actually completed in hardware.

**Fix:** don't call `transmit()`. Call `startTransmit()`, then poll
`getIrqFlags()` for `RADIOLIB_SX126X_IRQ_TX_DONE` over SPI ourselves (same
pattern already used for RX), then `finishTransmit()`.

**Lesson: removing DIO1 breaks more than RX. Check every RadioLib blocking
call — not just the one you're actively using — for hidden DIO1 polling
before assuming SPI-based `getIrqFlags()` polling alone covers it.**

## Timing: TX completing didn't mean the XIAO ever received it

With bugs 1–4 fixed, the boat's telemetry replies transmitted successfully
(`finishTransmit()` returning `RADIOLIB_ERR_NONE` every time) but the XIAO
never showed a receipt. Its `TELEM_WAIT_MS` listen window (8 ms) was shorter
than the boat's actual reply time (~18 ms, measured directly via a timing
diagnostic — not assumed).

That 18 ms broke down as ~10.3 ms genuine LoRa time-on-air (per RadioLib's
own `getTimeOnAir()`) plus ~7.7 ms of **software** overhead on top — the
boat's bit-banged SPI running at a self-imposed ~500 kHz (two
`delayMicroseconds(1)` calls per bit) against a chip rated for 16 MHz.

Fixed in two steps, each verified against hundreds of clean transmissions
(zero CRC failures) on real hardware before trusting it:

1. Removed the explicit per-bit delays. 18 ms → 14 ms.
2. Replaced `digitalWrite()`/`digitalRead()` in the bit-bang loop with direct
   `GPIO.out_w1ts/out_w1tc/in` register access — Arduino's GPIO wrapper call
   overhead (pin validation, mode dispatch) turned out to be the larger
   remaining cost. 14 ms → 12 ms.

**Lesson: on ESP32 Arduino, `digitalWrite`/`digitalRead` call overhead can
dominate over intentional delays in a tight bit-bang loop. Profile before
assuming the explicit delay is the bottleneck.**

The remaining gap was closed by shrinking the highest-rate telemetry frame
(ATTITUDE, 24 Hz) from the CRSF-standard 6-byte encoding to a custom 3-byte
one (`PROTOCOL_VERSION` 4 — see `docs/protocol.md`), and widening
`TELEM_WAIT_MS` to 18 ms — sized to cover the measured worst case across
*all* frame types (GPS at ~15 ms), not just the now-smaller ATTITUDE, since
SAILBOAT carries safety-relevant status bits (capsized/armed/failsafe) that
shouldn't be starved in favor of the highest-frequency-but-lower-stakes
frame.

## The real achieved RC rate was never 50 Hz

Measured directly (not estimated) on the boat's receive side: **~20 Hz**,
with inter-packet gaps ranging ~35–80 ms. `TX_PERIOD_MS = 20` on the XIAO is
a minimum-gap *gate*, not an enforced cycle time — `ap_radio_tick()`'s own
real duration (XIAO's own RC frame time-on-air alone is ~15 ms at these LoRa
settings, plus the telemetry listen window, plus apparent WiFi/web-server
overhead sharing the same `loop()`) already exceeded the nominal 20 ms
before any of this bring-up's changes.

This also explains the `LQ=40%`/`LQ=52%` readings seen during bring-up — that
calculation assumes a 50 Hz baseline (`count × 2`), so it reads ~40–50% at a
genuine ~20 Hz rate even with perfect reception. The link was never actually
dropping that many packets; the percentage was measuring against the wrong
baseline.

This is what motivated the move from LoRa to FSK modulation: LoRa's chirp
spread-spectrum was already at its fastest practical settings (SF7, BW500,
CR4/5) for this link, and further software optimization had diminishing
returns. Since this project's actual range requirement (~500 m) is well
within FSK's reach, switching modulation was the remaining lever with real
headroom.
