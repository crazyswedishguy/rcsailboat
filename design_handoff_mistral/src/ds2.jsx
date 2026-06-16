// ── Hybrid design system: B-layout · A-palette · Day + Dusk themes ──
const { useState, useRef, useEffect, useCallback } = React;

const _SANS = '-apple-system,BlinkMacSystemFont,"SF Pro Text","Inter",sans-serif';
const _MONO = '"JetBrains Mono","SF Mono",ui-monospace,monospace';

const THEMES2 = {
  day: {
    id:'day', sans:_SANS, mono:_MONO,
    bg:'#eaeef3', surface:'#ffffff', inset:'#eef1f5',
    text:'#0f1722', dim:'#5a6878', faint:'#97a3b2',
    border:'rgba(28,52,86,0.11)', borderStrong:'rgba(28,52,86,0.20)',
    accent:'#1c4ea0', accentSoft:'rgba(28,78,160,0.09)', accentLine:'rgba(28,78,160,0.24)',
    safe:'#1d7a4d', safeSoft:'rgba(29,122,77,0.12)',
    warn:'#b5730c', warnSoft:'rgba(181,115,12,0.12)',
    danger:'#c0392b', dangerSoft:'rgba(192,57,43,0.12)',
    attSky:'#cfe0f2', attGround:'#1d2735',
  },
  dusk: {
    id:'dusk', sans:_SANS, mono:_MONO,
    bg:'#0a111d', surface:'#121d2e', inset:'#0f1828',
    text:'#e8eef7', dim:'#8b9bb3', faint:'#4e5e78',
    border:'rgba(130,160,200,0.14)', borderStrong:'rgba(130,160,200,0.24)',
    accent:'#5a8fe6', accentSoft:'rgba(90,143,230,0.14)', accentLine:'rgba(90,143,230,0.30)',
    safe:'#3fbf83', safeSoft:'rgba(63,191,131,0.14)',
    warn:'#f0b54a', warnSoft:'rgba(240,181,74,0.12)',
    danger:'#ff6f5e', dangerSoft:'rgba(255,111,94,0.12)',
    attSky:'#1c3756', attGround:'#0a0f17',
  },
};

// ── atoms ──
const H2Lbl = ({ children, T, style }) => (
  <div style={{ fontFamily:_MONO, fontSize:8.5, fontWeight:600, letterSpacing:'0.13em', textTransform:'uppercase', color:T.dim, ...style }}>{children}</div>
);
const H2Val = ({ children, T, size=21, color }) => (
  <span style={{ fontFamily:_MONO, fontWeight:700, fontSize:size, color:color||T.text, fontFeatureSettings:'"tnum","zero"', lineHeight:1 }}>{children}</span>
);
const H2Unit = ({ children, T }) => (
  <span style={{ fontFamily:_MONO, fontSize:10, fontWeight:600, color:T.dim }}>{children}</span>
);

// Hairline cell — the B-layout atom
const HCell = ({ children, T, style }) => (
  <div style={{ background:T.surface, borderBottom:`1px solid ${T.border}`, ...style }}>{children}</div>
);

// Inset chip e.g. for bilge/temp row
const HInset = ({ children, T, style }) => (
  <div style={{ background:T.inset, ...style }}>{children}</div>
);

// Attitude horizon
function H2Att({ roll, pitch, size=100, T }) {
  const r = size/2, po = pitch*(r/22), clip=`h2att-${T.id}-${size}`;
  return (
    <svg viewBox={`${-r} ${-r} ${size} ${size}`} width={size} height={size} style={{display:'block'}}>
      <defs><clipPath id={clip}><circle r={r*0.92}/></clipPath></defs>
      <circle r={r*0.92} fill={T.attGround}/>
      <g clipPath={`url(#${clip})`} transform={`rotate(${roll})`}>
        <rect x={-r} y={-r*2+po} width={r*2} height={r*2} fill={T.attSky}/>
        <rect x={-r} y={po} width={r*2} height={r*2} fill={T.attGround}/>
        <line x1={-r} y1={po} x2={r} y2={po} stroke={T.text} strokeWidth="1.1" opacity="0.55"/>
        {[-20,-10,10,20].map(d=>{const y=po-d*(r/22),w=d%20===0?r*.40:r*.24;return <line key={d} x1={-w/2} y1={y} x2={w/2} y2={y} stroke={T.text} strokeWidth=".8" opacity=".28"/>})}
      </g>
      <circle r={r*0.92} fill="none" stroke={T.borderStrong} strokeWidth="1.2"/>
      <line x1={-r*.30} y1="0" x2={-r*.09} y2="0" stroke={T.accent} strokeWidth="2.2" strokeLinecap="round"/>
      <line x1={r*.09} y1="0" x2={r*.30} y2="0" stroke={T.accent} strokeWidth="2.2" strokeLinecap="round"/>
      <circle r="2" fill={T.accent}/>
      <polygon points={`0,${-r*.88} ${-r*.05},${-r*.77} ${r*.05},${-r*.77}`} fill={T.accent} transform={`rotate(${roll})`}/>
    </svg>
  );
}

// Sail quarter-arc
function H2Sail({ value, size=120, T }) {
  const CX=20,CY=20,R=size-52;
  const a=(1-value)*Math.PI/2;
  const pt={x:CX+R*Math.cos(a),y:CY+R*Math.sin(a)};
  const mid={x:(CX+pt.x)/2,y:(CY+pt.y)/2};
  const bow=12;
  const ctrl={x:mid.x+bow*Math.sin(a),y:mid.y-bow*Math.cos(a)};
  return (
    <svg viewBox={`0 0 ${size} ${size}`} width={size} height={size} style={{display:'block'}}>
      <path d={`M ${CX},${CY} L ${CX+R},${CY} A ${R},${R} 0 0,1 ${CX},${CY+R} Z`} fill={T.accentSoft}/>
      <path d={`M ${CX+R},${CY} A ${R},${R} 0 0,1 ${CX},${CY+R}`} fill="none" stroke={T.border} strokeWidth="1.2" strokeDasharray="3 4"/>
      <path d={`M ${CX},${CY} Q ${ctrl.x.toFixed(1)},${ctrl.y.toFixed(1)} ${pt.x.toFixed(1)},${pt.y.toFixed(1)} Z`} fill={T.accentSoft} stroke={T.accent} strokeWidth="1.6"/>
      <line x1={CX} y1={CY} x2={pt.x.toFixed(1)} y2={pt.y.toFixed(1)} stroke={T.dim} strokeWidth="1.4" strokeLinecap="round"/>
      <circle cx={CX} cy={CY} r="3" fill={T.dim}/>
      <circle cx={pt.x.toFixed(1)} cy={pt.y.toFixed(1)} r="9" fill={T.surface} stroke={T.accent} strokeWidth="2.2"/>
    </svg>
  );
}

// Shared data
const TELE = {
  volts:11.8, chg:68, amps:2.4, mah:340,
  sats:9, spd:3.2, lq:94, rssi:-72,
  roll:-4.1, pitch:1.8, hdg:247,
  temp:42, bilgeWet:false,
  lat:50.8234, lon:-1.4123, alt:0,
  sail:59, rud:6, thr:42, thrRev:-28, trim:0.06,
  homeDist:128, homeBrg:64,
};

Object.assign(window, { THEMES2, H2Lbl, H2Val, H2Unit, HCell, HInset, H2Att, H2Sail, TELE });
