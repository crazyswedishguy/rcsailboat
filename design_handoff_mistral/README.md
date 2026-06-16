# Handoff: Mistral RC Sailboat Control App

## Overview
Mistral is a mobile control interface for an RC sailboat running on a Raspberry Pi with ELRS (ExpressLRS) radio. It provides real-time telemetry, helm control (sail trim, rudder, motor), a GPS map, device status, and a console log — in both portrait (monitoring) and landscape (two-handed helming) orientations.

## About the Design Files
The files in this bundle are **high-fidelity design references created in HTML + React/JSX** — interactive prototypes showing intended look, layout, and behavior. The task is to **recreate these designs in your target codebase** (React Native, Flutter, SwiftUI, or web) using its established patterns and libraries. Do not ship the HTML directly.

The main entry point is `Mistral App v2.html`. Open it in a browser to interact with the prototype. Source components are in `src/`.

## Fidelity
**High-fidelity.** Exact colors, typography, spacing, and interactions are specified. Recreate pixel-accurately using your codebase's existing libraries.

---

## Design System (`src/ds2.jsx`)

### Color Tokens — Day theme
| Token | Value | Usage |
|---|---|---|
| `bg` | `#eaeef3` | App background |
| `surface` | `#ffffff` | Cards, panels |
| `inset` | `#eef1f5` | Inset backgrounds, tracks |
| `text` | `#0f1722` | Primary text |
| `dim` | `#5a6878` | Secondary labels |
| `faint` | `#97a3b2` | Placeholder, minor labels |
| `border` | `rgba(28,52,86,0.11)` | Hairline separators |
| `borderStrong` | `rgba(28,52,86,0.20)` | Tick marks, horizon lines |
| `accent` | `#1c4ea0` | Navy blue — primary action, tabs, logo |
| `accentSoft` | `rgba(28,78,160,0.09)` | Soft accent fills |
| `safe` | `#1d7a4d` | Linked, bilge dry, OK |
| `safeSoft` | `rgba(29,122,77,0.12)` | Linked badge bg |
| `warn` | `#b5730c` | Motor engaged, trim marker, low LQ |
| `warnSoft` | `rgba(181,115,12,0.12)` | Warn badge bg |
| `danger` | `#c0392b` | STOP button, fault, bilge wet |
| `dangerSoft` | `rgba(192,57,43,0.12)` | Danger badge bg |
| `attSky` | `#cfe0f2` | Attitude horizon sky |
| `attGround` | `#1d2735` | Attitude horizon ground |

### Color Tokens — Dusk (dark) theme
| Token | Value |
|---|---|
| `bg` | `#0a111d` |
| `surface` | `#121d2e` |
| `inset` | `#0f1828` |
| `text` | `#e8eef7` |
| `dim` | `#8b9bb3` |
| `faint` | `#4e5e78` |
| `border` | `rgba(130,160,200,0.14)` |
| `accent` | `#5a8fe6` |
| `safe` | `#3fbf83` |
| `warn` | `#f0b54a` |
| `danger` | `#ff6f5e` |
| `attSky` | `#1c3756` |
| `attGround` | `#0a0f17` |

### Typography
- **Sans:** `-apple-system, BlinkMacSystemFont, "SF Pro Text", "Inter", sans-serif`
- **Mono:** `"JetBrains Mono", "SF Mono", ui-monospace, monospace`
- All numeric readouts use monospace with `font-feature-settings: "tnum", "zero"` (tabular numerals, slashed zero)
- Labels: 8–9px mono, weight 600, `letter-spacing: 0.12em`, ALL CAPS
- Values: 12–26px mono, weight 700
- UI text: 10–14px sans, weight 600–800

### Logo
- SVG seagull glyph in `assets/logo.svg` — a single stroke path
- Render with `stroke={accent}` (navy / light blue in dusk) and `fill="none"`
- Paired with wordmark "Mistral" in 18–19px sans, weight 700, `letter-spacing: -0.01em`, color `accent`
- Followed by "Pi · ELRS" in 9px mono, faint, uppercase

---

## Shared Telemetry Data Shape
```js
{
  volts: 11.8,      // battery voltage
  chg: 68,          // battery charge %
  amps: 2.4,        // current draw A
  mah: 340,         // mAh consumed
  sats: 9,          // GPS satellites
  spd: 3.2,         // speed kn
  lq: 94,           // link quality %
  rssi: -72,        // RSSI dBm
  roll: -4.1,       // heel degrees (negative = port heel)
  pitch: 1.8,       // pitch degrees
  hdg: 247,         // heading degrees
  temp: 42,         // MCU temp °C
  bilgeWet: false,  // bilge sensor
  lat: 50.8234,     // GPS latitude
  lon: -1.4123,     // GPS longitude
  alt: 0,           // altitude m
  sail: 59,         // sail trim 0–100%
  rud: 6,           // rudder position degrees (signed)
  thr: 42,          // motor throttle % (signed: +fwd, -rev)
  thrRev: -28,      // demo reverse value
  trim: 0.06,       // rudder trim offset (signed, normalised)
  homeDist: 128,    // distance to home m
  homeBrg: 64,      // bearing to home degrees
}
```

---

## App Shell

### Portrait layout (390×844px)
```
┌─────────────────────────┐
│ StatusBar (30px)        │  time left · theme toggle · orient toggle · signal
│ Masthead                │  logo + wordmark · Pi·ELRS · LINKED badge
│ TabBar                  │  Control / Instr / Map / Devices / Console
│ HelmBanner / TeleBar    │  "TAP TO TAKE HELM" or "HELM ACTIVE" (Control)
│                         │  Telemetry strip (other tabs)
│ Screen content          │
│ (scrollable)            │
└─────────────────────────┘
```

### Landscape layout (844×390px)
```
┌────────────────────────────────────────────────────────────────┐
│ TopBar: logo+wordmark · tab rail · vitals · theme · ⇅ · status│
│ "TAP TO TAKE HELM" banner (if not in control, Control tab)     │
├──────────┬──────────────────────────────────────┬─────────────┤
│ SAIL     │ MINI-MAP (hero)                      │ ATTITUDE    │
│ (left    │ land/water · home · track · distance │ (top)       │
│ thumb)   │                                      ├─────────────┤
│          ├──────────────────────────────────────┤ RUDDER      │
│          │ MOTOR card · RELEASE · STOP          │ (bottom)    │
└──────────┴──────────────────────────────────────┴─────────────┘
```

---

## Screens

### 1. Control (Portrait)
Primary helming screen. See `src/ctrl2.jsx`.

**Attitude + Nav card** (full width, hairline bottom border)
- Left: 100×100px attitude horizon SVG (see below)
- Right: 2×2 grid — HEEL / HDG / SPEED / HOME with bearing arrow
- Each cell: 8.5px label + 17px value

**Bilge row** (inset background, 8px padding)
- Dot (7px, safe/danger) · "BILGE DRY" or "WATER IN BILGE" · temp readout right

**Sail trim card** (full width)
- Left: 116px sail arc SVG (quarter-circle, curved boom line, thumb on arc end)
- Right: "SAIL TRIM" label + 26px value + two buttons (◀ IN / OUT ▶)
- Buttons: 13px tall, full width each half, rounded 8px

**Rudder card** (full width, 13px padding)
- Header: "RUDDER" label + amber trim offset + 18px signed value
- Track: 52px tall, inset bg, 9 tick marks, amber trim marker, 40px circular thumb with value
- PORT / STBD labels at ends
- Trim stepper below: 3 buttons (− TRIM / CENTRE / TRIM +), 1:1.4:1 grid

**Motor card** (full width, 11px padding)
- Aux motor icon (sunburst, 15px) + "Motor" label + "AUX" tag
- Off / Hold / Set segmented selector (right-aligned)
  - Off = dim, Hold = warn amber, Set = warn amber
  - Selected has white bg + subtle shadow
- When engaged: bidirectional throttle track (26px tall, rounded)
  - Center tick = neutral, amber fill right for FWD, red fill left for REV
  - 24px circular thumb, signed % label, FWD/REV end labels (7px)
  - "Hold to drive" chip (Hold mode) or "⊘ Neutral" button (Set mode)

**RELEASE + STOP row** (14px padding, bottom)
- RELEASE: flex 1, surface bg, strong border, MONO 13px
- STOP: flex 1.6, danger red bg, 10px border radius, white square icon + "STOP" MONO 15px weight 800, shadow

### 2. Instruments (Portrait)
Full telemetry breakdown. See `src/instr2.jsx`.

Sections with accent-colored section headers in inset rows:
- **ATTITUDE** — horizon SVG (112px) + 2×2 grid (HEEL/PITCH/HDG/SPD)
- **POWER** — 2-col grid: VOLTAGE / CURRENT / CONSUMED / CHARGE (fault colouring: voltage < 11V = warn, < 10.5V = danger; charge < 30% = warn, < 20% = danger)
- **LINK** — LQ / RSSI / PROTOCOL / RATE (LQ < 70 = warn; RSSI < -80 = warn, < -90 = danger)
- **NAVIGATION** — LAT / LON / SPEED / HEADING / SATS / HOME
- **CONTROLS** — HEEL / PITCH / SAIL / RUDDER
- **ENVIRONMENT** — bilge dot + status + MCU temp

### 3. Map (Portrait)
See `src/map2.jsx`.

**Nav chips** (6-up grid): LAT / LON / SATS / SPEED / HDG / HOME

**SVG Map** (402×360px)
- Water: `#d4e4f0` (day) / `#0e1f36` (dusk)
- Land polygon: `#c8d8b8` / `#1a2e1a`, shoreline stroke `#a0b890` / `#2a4a2a`
- Grid lines: very faint
- Range rings from home: 40/80/130px radius, dashed accent
- Dashed bearing line boat→home
- Track trail: 5 points, 2px accent stroke, 70% opacity
- Home marker: 6px safe green circle + "H" white label
- Boat: 10px circle surface/accent border + heading arrow (rotates with hdg)
- Distance badge: accent bg, white text "128m 064°"
- North indicator: top-right corner

**Actions row**: SET HOME HERE (safe green) / RESET HOME / EXPORT GPX

### 4. Devices (Portrait)
See `src/dev2.jsx`.

Summary strip: dot + "ALL SYSTEMS NOMINAL" / fault message + "9/9 online"

Per device card (9 devices):
- Status dot (safe/warn/danger) + device name (13px 700) + subtitle (9px mono faint)
- 4-col stats grid (inset bg, hairline borders): each cell has 7.5px label + 12px value

Devices: Raspberry Pi, ELRS Receiver, GPS Module, IMU/Compass, Rudder Servo, Sail Winch, Motor ESC, Bilge Sensor, Battery Monitor

Firmware section at bottom: 4 rows of name + version

### 5. Console (Portrait)
See `src/con2.jsx`.

Filter bar (scrollable horizontal): ALL / OK / INFO / DATA / WARN / ERR chips + CLEAR
- Active chip: white bg, shadow, colored label
- Inactive: transparent

Log entries (scrollable):
- Timestamp (9.5px mono faint) · Level (9px mono 800, colored) · Message (11px mono)
- Hairline bottom border per row

Live indicator footer: green dot + "LIVE · N entries"

### 6. Landscape Control (Helm)
See `src/landscape2.jsx`.

**Left column (136px)** — Sail trim slider
- Vertical pill (54px wide), inset bg, "In" top / "Out" bottom
- Filled track from top: height = `(100 - sail%) × 74%`
- 42px circular thumb with value
- "% trimmed" label below

**Center column (flex 1)** — Mini-map + Motor/Stop
- Mini-map (340×140px): same as Map screen but compact
- Distance badge (absolute top-right): home icon + "128 m 064°"
- Bottom overlay: Hdg / Heel / Pitch vitals over gradient
- Motor card: same 3-mode selector + bidirectional throttle track
- RELEASE button (90px, surface bg) — only visible when in control
- STOP button (130px, danger red)

**Right column (210px)** — Attitude + Rudder
- Attitude card (top): 68px horizon + HEEL / PITCH 2-col grid
- Rudder card (flex 1, bottom): header + large drag pad + trim stepper
  - 50px circular thumb with value
  - Amber trim marker line
  - PORT / STBD / "drag · self-centres" labels
  - − / Centre / + trim stepper (1:1.3:1 grid)

---

## Interactions & Behavior

### Helm handoff
- **Portrait**: Tapping "TAP TO TAKE HELM" banner activates helm (banner becomes navy "HELM ACTIVE" strip)
- **Landscape**: Same banner below top bar; "Linked" pill in top bar becomes "Helm active" (accent bg) when active
- RELEASE button / clicking pill again releases helm
- STOP cuts power and centres rudder (sets motor to neutral, rud = 0) — always available

### Motor modes
- **Off**: throttle track disabled, dimmed
- **Hold (momentary)**: throttle applies while dragging; releases to neutral on lift
- **Set (latching)**: throttle stays at position; "⊘ Neutral" button cuts to zero

### Theme toggle
Day ↔ Dusk via "☀ DAY / ☾ DUSK" button in status bar (portrait) or top bar (landscape)

### Orientation
"⇄ LANDSCAPE" in portrait status bar → landscape; "⇅ PORTRAIT" in landscape top bar → portrait. In production, respond to device orientation events.

### Home position
Set automatically on first GPS fix; "SET HOME HERE" button resets it. Distance and bearing computed continuously from GPS.

---

## Attitude Horizon SVG
Rendered as SVG, not canvas. Key elements:
1. Ground rect (full circle clip) = `attGround` color
2. Sky rect (upper half) = `attSky` color, rotated by heel angle
3. Horizon line at pitch offset
4. Pitch ladder lines (±10°, ±20°)
5. Fixed aircraft reference marks (left + right bars, center dot)
6. Triangle heading indicator at top of circle (rotates with roll)
7. Clip path = circle at 92% of radius

Pitch offset = `pitch × (radius / 22)` px

---

## Assets
- `assets/logo.svg` — seagull glyph, single stroke path. Render with `stroke={accent}` color, `fill="none"`, `strokeWidth="27.5"` (scaled to viewBox 1446×332). Typical rendered height: 22–26px.

---

## Files
| File | Description |
|---|---|
| `Mistral App v2.html` | Main interactive prototype — open in browser |
| `src/ds2.jsx` | Design system: color tokens, shared atoms (H2Att, H2Sail, HCell, HInset) |
| `src/ctrl2.jsx` | Control screen component |
| `src/instr2.jsx` | Instruments screen component |
| `src/map2.jsx` | Map screen component |
| `src/dev2.jsx` | Devices screen component |
| `src/con2.jsx` | Console/log screen component |
| `src/landscape2.jsx` | Landscape helm layout (LandMap, LandThrottle, Landscape2) |
| `assets/logo.svg` | Mistral seagull logo mark |

---

## Notes for Implementation

1. **WebSocket telemetry**: Replace the static `TELE` object in `ds2.jsx` with a WebSocket connection to the Pi. All components receive `T` (theme) and `d` (telemetry data) as props — straightforward to swap.

2. **Controls**: The current prototype shows static control positions. In production, sail/rudder/throttle controls need drag gesture handlers that send commands to the Pi and update local state optimistically.

3. **Orientation detection**: Use `window.matchMedia('(orientation: landscape)')` or device orientation API; mirror the state toggle logic already in the app.

4. **STOP safety**: STOP must be a direct hardware command, not routed through any debounce or animation. It should be the highest-priority action in the UI.

5. **Bilge alert**: If `bilgeWet === true`, consider a persistent modal overlay above all screens, not just the subtle row indicator.
