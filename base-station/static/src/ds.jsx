// ds.jsx — Design system: color tokens, typography, shared atoms and SVG widgets.
// All other component files depend on the constants and components defined here.
// Loaded first by index.html so subsequent scripts can reference them freely.

// ── Font stacks ───────────────────────────────────────────────────────────────
// _SANS: system UI fonts — no download needed, looks native on every platform.
// _MONO: JetBrains Mono for all numeric readouts; tabular numerals keep digits
//        aligned so values don't jump as they update.
const _SANS = '-apple-system,BlinkMacSystemFont,"SF Pro Text","Inter",sans-serif';
const _MONO = '"JetBrains Mono","SF Mono",ui-monospace,monospace';

// ── Color themes ──────────────────────────────────────────────────────────────
// Two themes: Day (light, for outdoor use in bright sun) and Dusk (dark, for
// low-light / night use).  All components receive `T` (the active theme object)
// as a prop and read colors from it — never hardcode hex values in components.
const THEMES = {
  day: {
    id:'day', sans:_SANS, mono:_MONO,
    bg:'#eaeef3',        // page background
    surface:'#ffffff',   // cards and panels
    inset:'#eef1f5',     // inset fields, tracks, filter bars
    text:'#0f1722',      // primary text
    dim:'#5a6878',       // secondary labels
    faint:'#97a3b2',     // placeholder / stale-data colour
    border:'rgba(28,52,86,0.11)',
    borderStrong:'rgba(28,52,86,0.20)',
    accent:'#1c4ea0',                        // navy — primary actions, tabs
    accentSoft:'rgba(28,78,160,0.09)',
    accentLine:'rgba(28,78,160,0.24)',
    safe:'#1d7a4d',    safeSoft:'rgba(29,122,77,0.12)',   // linked, bilge dry
    warn:'#b5730c',    warnSoft:'rgba(181,115,12,0.12)',   // motor, trim
    danger:'#c0392b',  dangerSoft:'rgba(192,57,43,0.12)', // STOP, fault
    attSky:'#cfe0f2',    // attitude horizon sky colour
    attGround:'#1d2735', // attitude horizon ground colour
  },
  dusk: {
    id:'dusk', sans:_SANS, mono:_MONO,
    bg:'#0a111d',
    surface:'#121d2e',
    inset:'#0f1828',
    text:'#e8eef7',
    dim:'#8b9bb3',
    faint:'#4e5e78',
    border:'rgba(130,160,200,0.14)',
    borderStrong:'rgba(130,160,200,0.24)',
    accent:'#5a8fe6',
    accentSoft:'rgba(90,143,230,0.14)',
    accentLine:'rgba(90,143,230,0.30)',
    safe:'#3fbf83',    safeSoft:'rgba(63,191,131,0.14)',
    warn:'#f0b54a',    warnSoft:'rgba(240,181,74,0.12)',
    danger:'#ff6f5e',  dangerSoft:'rgba(255,111,94,0.12)',
    attSky:'#1c3756',
    attGround:'#0a0f17',
  },
};

// ── Staleness helpers ─────────────────────────────────────────────────────────
// When live data hasn't been received recently, render the value in T.faint so
// the operator knows the reading is potentially out-of-date rather than current.

// sc: "stale colour" — returns T.faint when stale, normalColor otherwise.
const sc = (stale, normalColor, T) => stale ? T.faint : normalColor;

// sv: "stale value" — returns '—' when data was never received (e.g. no GPS fix),
// otherwise formats the value with the provided string.
const sv = (neverReceived, formatted) => neverReceived ? '—' : formatted;

// ── Design-system atoms ───────────────────────────────────────────────────────
// Small presentational components used consistently across every screen.

// Lbl: uppercase mono label, 8.5 px — used for field names like "HEEL", "LQ".
const Lbl = ({ children, T, style }) => (
  <div style={{ fontFamily:_MONO, fontSize:8.5, fontWeight:600,
    letterSpacing:'0.13em', textTransform:'uppercase', color:T.dim, ...style }}>
    {children}
  </div>
);

// Val: large tabular monospace value — used for all numeric readouts.
// fontFeatureSettings enables tabular numerals ("tnum") and slashed zero ("zero")
// so digits are equal-width and 0 is distinguishable from O.
const Val = ({ children, T, size=21, color }) => (
  <span style={{ fontFamily:_MONO, fontWeight:700, fontSize:size,
    color:color||T.text, fontFeatureSettings:'"tnum","zero"', lineHeight:1 }}>
    {children}
  </span>
);

// Card: full-width card container with hairline bottom border.
// The primary layout unit — all major sections are wrapped in a Card.
const Card = ({ children, T, style }) => (
  <div style={{ background:T.surface, borderBottom:`1px solid ${T.border}`, ...style }}>
    {children}
  </div>
);

// Inset: lighter background div for secondary rows (bilge bar, filter bars, etc.)
const Inset = ({ children, T, style }) => (
  <div style={{ background:T.inset, ...style }}>{children}</div>
);

// ── Attitude horizon (SVG) ────────────────────────────────────────────────────
// Synthetic flight-instrument horizon showing heel and pitch.
// - Roll angle rotates the sky/ground rectangle (positive = starboard heel).
// - Pitch offset moves the horizon line up/down (positive = bow up).
// - A fixed aircraft reference (wings + dot) always points straight ahead.
const AttHorizon = ({ roll=0, pitch=0, size=100, T }) => {
  const r = size / 2;
  const po = pitch * (r / 22);           // pixels of vertical offset per degree
  const cid = `att-${T.id}-${size}`;    // unique clip-path id per size+theme
  return (
    <svg viewBox={`${-r} ${-r} ${size} ${size}`} width={size} height={size}
      style={{ display:'block', flexShrink:0 }}>
      <defs><clipPath id={cid}><circle r={r*0.92}/></clipPath></defs>
      {/* Ground fills first, then sky overlaps the upper half */}
      <circle r={r*0.92} fill={T.attGround}/>
      <g clipPath={`url(#${cid})`} transform={`rotate(${roll})`}>
        <rect x={-r} y={-r*2+po} width={r*2} height={r*2} fill={T.attSky}/>
        <rect x={-r} y={po}      width={r*2} height={r*2} fill={T.attGround}/>
        <line x1={-r} y1={po} x2={r} y2={po}
          stroke={T.text} strokeWidth="1.1" opacity="0.55"/>
        {/* Pitch ladder lines at ±10° and ±20° */}
        {[-20,-10,10,20].map(deg => {
          const y = po - deg*(r/22);
          const w = Math.abs(deg)===20 ? r*.40 : r*.24;
          return <line key={deg} x1={-w/2} y1={y} x2={w/2} y2={y}
            stroke={T.text} strokeWidth=".8" opacity=".28"/>;
        })}
      </g>
      <circle r={r*0.92} fill="none" stroke={T.borderStrong} strokeWidth="1.2"/>
      {/* Fixed reference — wings and centre dot, always horizontal */}
      <line x1={-r*.30} y1="0" x2={-r*.09} y2="0"
        stroke={T.accent} strokeWidth="2.2" strokeLinecap="round"/>
      <line x1={r*.09}  y1="0" x2={r*.30}  y2="0"
        stroke={T.accent} strokeWidth="2.2" strokeLinecap="round"/>
      <circle r="2" fill={T.accent}/>
      {/* Roll indicator triangle — rotates with heel angle */}
      <polygon points={`0,${-r*.88} ${-r*.05},${-r*.77} ${r*.05},${-r*.77}`}
        fill={T.accent} transform={`rotate(${roll})`}/>
    </svg>
  );
};

// ── Sail arc (SVG) ────────────────────────────────────────────────────────────
// Quarter-circle arc showing sail trim angle.
// value: 0.0 = fully eased (sheet out), 1.0 = fully trimmed in.
// A curved "boom" line runs from the mast (corner) to the thumb on the arc.
const SailArc = ({ value=0.5, size=116, T }) => {
  const CX=20, CY=20, R=size-52;
  const a = (1-value) * Math.PI/2;   // angle measured from horizontal
  const pt = { x:CX+R*Math.cos(a), y:CY+R*Math.sin(a) };
  const mid = { x:(CX+pt.x)/2, y:(CY+pt.y)/2 };
  // Quadratic bezier control point — offsets the boom line to look curved
  const ctrl = { x:mid.x+12*Math.sin(a), y:mid.y-12*Math.cos(a) };
  return (
    <svg viewBox={`0 0 ${size} ${size}`} width={size} height={size}
      style={{ display:'block', flexShrink:0 }}>
      {/* Filled sector background */}
      <path d={`M ${CX},${CY} L ${CX+R},${CY} A ${R},${R} 0 0,1 ${CX},${CY+R} Z`}
        fill={T.accentSoft}/>
      {/* Dashed arc track */}
      <path d={`M ${CX+R},${CY} A ${R},${R} 0 0,1 ${CX},${CY+R}`}
        fill="none" stroke={T.border} strokeWidth="1.2" strokeDasharray="3 4"/>
      {/* Boom — filled region + stroke */}
      <path d={`M ${CX},${CY} Q ${ctrl.x.toFixed(1)},${ctrl.y.toFixed(1)} ${pt.x.toFixed(1)},${pt.y.toFixed(1)} Z`}
        fill={T.accentSoft} stroke={T.accent} strokeWidth="1.6"/>
      {/* Mast (boom root line) */}
      <line x1={CX} y1={CY} x2={pt.x.toFixed(1)} y2={pt.y.toFixed(1)}
        stroke={T.dim} strokeWidth="1.4" strokeLinecap="round"/>
      <circle cx={CX} cy={CY} r="3" fill={T.dim}/>
      {/* Draggable thumb */}
      <circle cx={pt.x.toFixed(1)} cy={pt.y.toFixed(1)} r="9"
        fill={T.surface} stroke={T.accent} strokeWidth="2.2"/>
    </svg>
  );
};

// ── Mistral seagull logo (SVG) ────────────────────────────────────────────────
// Single-stroke seagull glyph from the Mistral brand.  Always fill="none";
// colour comes from T.accent via the stroke prop.
const Logo = ({ T, height=26 }) => (
  <svg viewBox="0 0 1446 332" height={height} style={{ display:'block', flexShrink:0 }} fill="none">
    <g transform="translate(-1127 -1319)">
      <path d="M2092.02 1471.14C2091.93 1475.36 2211.1 1511.7 2208.14 1529.07 2205.18 1546.43 2131.11 1561.05 2074.25 1575.36 2017.39 1589.67 1931.56 1606.24 1866.96 1614.93 1802.36 1623.61 1742.65 1651.47 1686.66 1627.47 1891.87 1484.34 1704.44 1481.14 1535.61 1443.47 1366.78 1405.81 1146.87 1447.39 1141.1 1428.98 1135.34 1410.56 1369.82 1333.4 1501.03 1333 1632.24 1332.6 1805.65 1419.92 1928.36 1426.56 2051.07 1433.2 2132.43 1381.09 2237.3 1372.82 2342.17 1364.56 2579.57 1407.98 2557.57 1376.99"
        stroke={T.accent} strokeWidth="27.5" strokeMiterlimit="8" strokeLinecap="round"/>
    </g>
  </svg>
);
