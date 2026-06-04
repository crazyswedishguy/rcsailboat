// map.jsx — Map tab (portrait).
// SVG chart showing boat position, home marker, track trail, and range rings.
// Home position is stored in the parent App and updated via setHomePos.

// Props: { T, d, stale, homePos, setHomePos }

const MapTab = ({ T, d, stale, homePos, setHomePos }) => {
  const lbl = (x) => ({ fontFamily:_MONO, fontSize:8.5, fontWeight:600,
    letterSpacing:'0.13em', textTransform:'uppercase', color:T.dim, ...(x||{}) });
  const val = (s,c) => ({ fontFamily:_MONO, fontWeight:700, fontSize:s||14,
    color:c||T.text, fontFeatureSettings:'"tnum","zero"', lineHeight:1 });

  // ── SVG chart ───────────────────────────────────────────────────────────────
  const MapSVG = ({ width, height }) => {
    const W=width, H=height;
    // Fixed positions for boat and home on the SVG canvas.
    // In a real implementation these would be computed from GPS coordinates.
    const bx=W*0.60, by=H*0.55;
    const hx=W*0.38, hy=H*0.42;

    // Stylised land polygon (upper-left blob) — represents shoreline
    const shore = `M0,0 L${W*.52},0 L${W*.55},${H*.08} L${W*.48},${H*.18} `+
      `L${W*.44},${H*.28} L${W*.36},${H*.34} L${W*.28},${H*.38} `+
      `L${W*.18},${H*.36} L${W*.10},${H*.32} L0,${H*.28} Z`;

    // Recent track trail — last few positions ending at boat
    const pts = [
      [bx-W*.18,by+H*.12],[bx-W*.14,by+H*.07],
      [bx-W*.09,by+H*.03],[bx-W*.04,by-H*.01],[bx,by],
    ];
    const track = pts.map(([x,y],i) => `${i===0?'M':'L'}${x},${y}`).join(' ');

    const isDusk    = T.id==='dusk';
    const water     = isDusk ? '#0e1f36' : '#d4e4f0';
    const land      = isDusk ? '#1a2e1a' : '#c8d8b8';
    const shoreStr  = isDusk ? '#2a4a2a' : '#a0b890';
    const ringCol   = isDusk ? 'rgba(90,143,230,0.18)' : 'rgba(28,78,160,0.12)';
    const gridCol   = isDusk ? 'rgba(255,255,255,0.05)' : 'rgba(28,52,86,0.06)';

    return (
      <svg width={W} height={H} viewBox={`0 0 ${W} ${H}`} style={{ display:'block' }}>
        {/* Water background */}
        <rect width={W} height={H} fill={water}/>
        {/* Light grid lines */}
        {[...Array(8)].map((_,i) => (
          <line key={`v${i}`} x1={W/8*i} y1={0} x2={W/8*i} y2={H} stroke={gridCol} strokeWidth="1"/>
        ))}
        {[...Array(6)].map((_,i) => (
          <line key={`h${i}`} x1={0} y1={H/6*i} x2={W} y2={H/6*i} stroke={gridCol} strokeWidth="1"/>
        ))}
        {/* Land */}
        <path d={shore} fill={land} stroke={shoreStr} strokeWidth="1.5"/>
        {/* Range rings from home — spaced at roughly 40/80/130 m */}
        {[40,80,130].map(r => (
          <circle key={r} cx={hx} cy={hy} r={r} fill="none"
            stroke={ringCol} strokeWidth="1" strokeDasharray="4 5"/>
        ))}
        {/* Dashed bearing line boat→home */}
        <line x1={bx} y1={by} x2={hx} y2={hy}
          stroke={T.accent} strokeWidth="1" strokeDasharray="5 4" opacity="0.5"/>
        {/* Track trail */}
        <path d={track} fill="none" stroke={T.accent} strokeWidth="2"
          strokeLinecap="round" strokeLinejoin="round" opacity="0.7"/>
        {/* Home marker */}
        <circle cx={hx} cy={hy} r="6" fill={T.safe} opacity="0.9"/>
        <text x={hx} y={hy+1} textAnchor="middle" dominantBaseline="middle"
          fontSize="7" fontWeight="700" fill="#fff">H</text>
        <rect x={hx+10} y={hy-9} width={42} height={16} rx="4"
          fill={T.surface} opacity="0.9"/>
        <text x={hx+31} y={hy+1} textAnchor="middle" dominantBaseline="middle"
          fontSize="8" fontWeight="700" fontFamily={_MONO} fill={T.text}>HOME</text>
        {/* Boat marker — arrow rotates with heading */}
        <circle cx={bx} cy={by} r="10" fill={T.surface} stroke={T.accent} strokeWidth="2"/>
        <polygon
          points={`${bx},${by-7} ${bx+4.5},${by+4} ${bx},${by+2} ${bx-4.5},${by+4}`}
          fill={T.accent} transform={`rotate(${d.hdg-180},${bx},${by})`}/>
        {/* Distance badge */}
        <rect x={bx+14} y={by-11} width={52} height={20} rx="5" fill={T.accent}/>
        <text x={bx+40} y={by} textAnchor="middle" dominantBaseline="middle"
          fontSize="9" fontWeight="700" fontFamily={_MONO} fill="#fff">
          {d.homeDist}m {String(d.homeBrg).padStart(3,'0')}°
        </text>
        {/* North indicator */}
        <text x={W-14} y={16} textAnchor="middle" fontSize="11" fontWeight="800"
          fontFamily={_MONO} fill={T.text}>N</text>
        <polygon points={`${W-14},22 ${W-17},30 ${W-14},28 ${W-11},30`} fill={T.accent}/>
      </svg>
    );
  };

  return (
    <div style={{ display:'flex', flexDirection:'column' }}>

      {/* ── Nav chips: 6 key position values ─────────────────────────────── */}
      <Card T={T} style={{ padding:'10px 16px' }}>
        <div style={{ display:'grid', gridTemplateColumns:'repeat(3,1fr)', gap:7 }}>
          {[
            ['LAT',   stale.gps,   `${d.lat.toFixed(4)}°`],
            ['LON',   stale.gps,   `${d.lon.toFixed(4)}°`],
            ['SATS',  stale.gps,   `${d.sats}`],
            ['SPEED', stale.gps,   `${d.spd.toFixed(1)} kn`],
            ['HDG',   stale.telem, `${d.hdg}°`],
            ['HOME',  false,        d.homeDist ? `${d.homeDist}m` : '—'],
          ].map(([k, isStale, v]) => (
            <div key={k} style={{ display:'flex', flexDirection:'column', gap:3 }}>
              <span style={lbl()}>{k}</span>
              <span style={val(14, sc(isStale, T.text, T))}>{v}</span>
            </div>
          ))}
        </div>
      </Card>

      {/* ── Chart ────────────────────────────────────────────────────────── */}
      <div style={{ borderBottom:`1px solid ${T.border}` }}>
        <MapSVG width={390} height={340}/>
      </div>

      {/* ── Action buttons ───────────────────────────────────────────────── */}
      <Card T={T} style={{ padding:'11px 16px', display:'flex', gap:9 }}>
        <div onClick={() => setHomePos({ lat:d.lat, lng:d.lon })}
          style={{ flex:1, padding:'11px 8px', borderRadius:8,
            background:T.safe, color:'#fff', textAlign:'center',
            fontFamily:_MONO, fontSize:11, fontWeight:800,
            letterSpacing:'0.06em', cursor:'pointer' }}>
          SET HOME HERE
        </div>
        <div onClick={() => setHomePos(null)}
          style={{ flex:1, padding:'11px 8px', borderRadius:8,
            background:T.inset, border:`1px solid ${T.border}`,
            color:T.text, textAlign:'center',
            fontFamily:_MONO, fontSize:11, fontWeight:700,
            letterSpacing:'0.06em', cursor:'pointer' }}>
          RESET HOME
        </div>
        {/* GPX export — placeholder until track logging is implemented */}
        <div style={{ flex:1, padding:'11px 8px', borderRadius:8,
          background:T.inset, border:`1px solid ${T.border}`,
          color:T.faint, textAlign:'center',
          fontFamily:_MONO, fontSize:11, fontWeight:700,
          letterSpacing:'0.06em' }}>
          EXPORT GPX
        </div>
      </Card>

    </div>
  );
};
