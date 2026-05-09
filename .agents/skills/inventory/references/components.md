# Inventory Skill — Component Reference

Rules keyed by what is present in the project BOM. When a trigger condition is met,
add the implied component to the checklist if it is not already present.
Apply **all** matching rules — do not stop at the first match.

## Rules

| Trigger: if present in BOM | Implied component | Qty | Specification | Purpose | Search hint |
|---|---|---|---|---|---|
| I²C bus (any I²C device) | 4.7kΩ pull-up resistors | 2 | 1/4W through-hole | I²C bus pull-ups — one pair per bus, not per device | search Mouser: "4.7k ohm resistor through-hole" |
| Motor or ESC | Ferrite clamp cores | 4 | Snap-fit, Fair-Rite 31 material | Motor and ESC lead EMI suppression | search Mouser: "ferrite snap clamp 31 material" |
| Motor or ESC | Ceramic capacitors 100nF | 3 | 50V rated, ceramic | Motor terminal noise suppression (one across terminals, one each terminal to case) | search Mouser: "100nF 50V ceramic capacitor" |
| Battery main lead | Inline fuse holder + fuses | 1 holder + 2 fuses | ATC/ATO blade type, rated to motor stall current | Battery positive lead short-circuit protection | search Mouser: "ATC blade fuse holder inline" |
| Battery main lead | Main power connector pair | 1 pair | XT60 or EC5, rated for peak motor current | Battery connection; polarised to prevent reverse connection | search Amazon: "XT60 connector pair" |
| LiPo battery | LiPo-safe charging bag | 1 | Sized for the battery pack | Safe charging and storage of LiPo cells | search Amazon: "LiPo safe bag" |
| LiPo battery | Battery cell voltage alarm | 1 | Plugs into balance lead, piezo buzzer | Low-voltage warning during use | search Amazon: "LiPo voltage alarm balance lead" |
| 3.3V and 5V logic devices on shared signal lines | Logic level shifter module | 1 | Bidirectional, I²C compatible, 4-channel minimum | Level translation for 3.3V↔5V signal crossings | search Amazon: "bidirectional logic level shifter 4 channel" |
| N-MOSFET switch circuit | Flyback diode | 1 per inductive load | 1N4007 or Schottky; voltage rating above supply rail | Protects MOSFET from inductive kickback when switching off | search Mouser: "1N4007 diode" |
| Any logic IC with a VCC pin (I²C, SPI, UART) | Bypass capacitors 100nF | 1 per IC VCC pin | 50V ceramic, placed close to VCC pin | Decouple logic supply against switching transients | search Mouser: "100nF 50V ceramic capacitor" |
| High-current supply wiring longer than 30cm | Bulk electrolytic capacitor | 1 | 100–470µF, voltage rating above rail voltage | Stabilise rail against current-draw transients | search Mouser: "470uF electrolytic capacitor" |
| Any servo | Servo extension leads | 1 per servo (optional) | Standard 3-pin servo lead, length to suit routing | Extend servo connector from PCA9685 to servo mounting point | search Amazon: "servo extension lead" |
