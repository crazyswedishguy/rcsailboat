// ── Map screen ──
function Map2({ T }) {
  const d = TELE;
  const lbl = (x) => ({ fontFamily:_MONO, fontSize:8.5, fontWeight:600, letterSpacing:'0.13em', textTransform:'uppercase', color:T.dim, ...(x||{}) });
  const val = (s,c) => ({ fontFamily:_MONO, fontWeight:700, fontSize:s||14, color:c||T.text, fontFeatureSettings:'"tnum","zero"', lineHeight:1 });

  // SVG map: simple land/water chart with home + boat
  const MapSVG = ({ width, height }) => {
    const W=width, H=height;
    // boat at roughly 60% across, 55% down
    const bx=W*0.60, by=H*0.55;
    // home at 38% across, 42% down
    const hx=W*0.38, hy=H*0.42;
    // shoreline polygon (land = upper-left blob)
    const shore = `M0,0 L${W*0.52},0 L${W*0.55},${H*0.08} L${W*0.48},${H*0.18} L${W*0.44},${H*0.28} L${W*0.36},${H*0.34} L${W*0.28},${H*0.38} L${W*0.18},${H*0.36} L${W*0.10},${H*0.32} L0,${H*0.28} Z`;
    // track: 5 recent points ending at boat
    const track = [
      [bx-W*0.18, by+H*0.12],[bx-W*0.14, by+H*0.07],[bx-W*0.09, by+H*0.03],[bx-W*0.04, by-H*0.01],[bx,by]
    ].map(([x,y],i)=>i===0?`M${x},${y}`:`L${x},${y}`).join(' ');
    // range rings from home
    const rings = [40,80,130].map(r=>({r, x:hx, y:hy}));
    // bearing line home
    const bearRad = (d.homeBrg-90)*Math.PI/180;
    const lineLen = Math.hypot(bx-hx, by-hy);
    const water = T.id==='dusk' ? '#0e1f36' : '#d4e4f0';
    const land  = T.id==='dusk' ? '#1a2e1a' : '#c8d8b8';
    const shore_stroke = T.id==='dusk' ? '#2a4a2a' : '#a0b890';
    const ring_col = T.id==='dusk' ? 'rgba(90,143,230,0.18)' : 'rgba(28,78,160,0.12)';
    const track_col = T.accent;
    const grid_col = T.id==='dusk' ? 'rgba(255,255,255,0.05)' : 'rgba(28,52,86,0.06)';
    return (
      <svg width={W} height={H} viewBox={`0 0 ${W} ${H}`} style={{ display:'block' }}>
        {/* water */}
        <rect width={W} height={H} fill={water}/>
        {/* grid */}
        {[...Array(8)].map((_,i)=><line key={'v'+i} x1={W/8*i} y1={0} x2={W/8*i} y2={H} stroke={grid_col} strokeWidth="1"/>)}
        {[...Array(6)].map((_,i)=><line key={'h'+i} x1={0} y1={H/6*i} x2={W} y2={H/6*i} stroke={grid_col} strokeWidth="1"/>)}
        {/* land */}
        <path d={shore} fill={land} stroke={shore_stroke} strokeWidth="1.5"/>
        {/* range rings */}
        {rings.map(({r,x,y})=><circle key={r} cx={x} cy={y} r={r} fill="none" stroke={ring_col} strokeWidth="1" strokeDasharray="4 5"/>)}
        {/* bearing line boat→home */}
        <line x1={bx} y1={by} x2={hx} y2={hy} stroke={T.accent} strokeWidth="1" strokeDasharray="5 4" opacity="0.5"/>
        {/* track */}
        <path d={track} fill="none" stroke={track_col} strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" opacity="0.7"/>
        {/* home marker */}
        <circle cx={hx} cy={hy} r="6" fill={T.safe} opacity="0.9"/>
        <text x={hx} y={hy+1} textAnchor="middle" dominantBaseline="middle" fontSize="7" fontWeight="700" fill="#fff">H</text>
        {/* home label */}
        <rect x={hx+10} y={hy-9} width={42} height={16} rx="4" fill={T.surface} opacity="0.9"/>
        <text x={hx+31} y={hy+1} textAnchor="middle" dominantBaseline="middle" fontSize="8" fontWeight="700" fontFamily={_MONO} fill={T.text}>HOME</text>
        {/* boat marker */}
        <circle cx={bx} cy={by} r="10" fill={T.surface} stroke={T.accent} strokeWidth="2"/>
        <polygon points={`${bx},${by-7} ${bx+4.5},${by+4} ${bx},${by+2} ${bx-4.5},${by+4}`} fill={T.accent} transform={`rotate(${d.hdg-180},${bx},${by})`}/>
        {/* distance badge */}
        <rect x={bx+14} y={by-11} width={52} height={20} rx="5" fill={T.accent}/>
        <text x={bx+40} y={by} textAnchor="middle" dominantBaseline="middle" fontSize="9" fontWeight="700" fontFamily={_MONO} fill="#fff">{d.homeDist}m {d.homeBrg}°</text>
        {/* north */}
        <text x={W-14} y={16} textAnchor="middle" fontSize="11" fontWeight="800" fontFamily={_MONO} fill={T.text}>N</text>
        <polygon points={`${W-14},22 ${W-17},30 ${W-14},28 ${W-11},30`} fill={T.accent}/>
      </svg>
    );
  };

  return (
    <div style={{ display:'flex', flexDirection:'column' }}>
      {/* nav chips */}
      <HCell T={T} style={{ padding:'10px 16px' }}>
        <div style={{ display:'grid', gridTemplateColumns:'repeat(3,1fr)', gap:7 }}>
          {[['LAT', d.lat.toFixed(4)+'°'], ['LON', d.lon.toFixed(4)+'°'], ['SATS', d.sats+''], ['SPEED', d.spd.toFixed(1)+' kn'], ['HDG', d.hdg+'°'], ['HOME', d.homeDist+'m']].map(([k,v]) => (
            <div key={k} style={{ display:'flex', flexDirection:'column', gap:3 }}>
              <span style={lbl()}>{k}</span>
              <span style={val(14)}>{v}</span>
            </div>
          ))}
        </div>
      </HCell>

      {/* MAP — fills the available space */}
      <div style={{ flex:1, position:'relative', minHeight:340, borderBottom:`1px solid ${T.border}` }}>
        <MapSVG width={402} height={360}/>
      </div>

      {/* actions */}
      <HCell T={T} style={{ padding:'11px 16px', display:'flex', gap:9 }}>
        <div style={{ flex:1, padding:'11px 8px', borderRadius:8, background:T.safe, color:'#fff', textAlign:'center', fontFamily:_MONO, fontSize:11, fontWeight:800, letterSpacing:'0.06em' }}>SET HOME HERE</div>
        <div style={{ flex:1, padding:'11px 8px', borderRadius:8, background:T.inset, border:`1px solid ${T.border}`, color:T.text, textAlign:'center', fontFamily:_MONO, fontSize:11, fontWeight:700, letterSpacing:'0.06em' }}>RESET HOME</div>
        <div style={{ flex:1, padding:'11px 8px', borderRadius:8, background:T.inset, border:`1px solid ${T.border}`, color:T.text, textAlign:'center', fontFamily:_MONO, fontSize:11, fontWeight:700, letterSpacing:'0.06em' }}>EXPORT GPX</div>
      </HCell>
    </div>
  );
}
Object.assign(window, { Map2 });
