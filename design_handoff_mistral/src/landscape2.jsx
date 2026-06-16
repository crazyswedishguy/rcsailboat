// ── Landscape helm screen ──
function LandThrottle({ T, motorMode, d }) {
  const engaged = motorMode !== 'off';
  const thrVal = motorMode==='hold' ? d.thr : motorMode==='set' ? d.thrRev : 0;
  const pct = 50 + thrVal/2;
  const fwd = thrVal >= 0;
  const fc = fwd ? T.warn : T.danger;
  const lbl2 = { fontFamily:_MONO, fontSize:7, fontWeight:700, letterSpacing:'0.06em', color:T.faint };
  return (
    <div style={{display:'flex',alignItems:'center',gap:8}}>
      <div style={{position:'relative',flex:1,height:26,background:T.inset,borderRadius:13,border:'1px solid '+T.border,opacity:engaged?1:0.45}}>
        <div style={{position:'absolute',left:'50%',top:'18%',bottom:'18%',width:1.5,background:T.borderStrong,transform:'translateX(-50%)'}}/>
        {engaged && Math.abs(thrVal) > 0 && (
          <div style={{position:'absolute',top:'22%',bottom:'22%',left:fwd?'50%':(pct+'%'),width:(Math.abs(thrVal)/2)+'%',background:fc,opacity:0.2,borderRadius:5}}/>
        )}
        {engaged ? (
          <div style={{position:'absolute',top:'50%',left:(pct+'%'),width:24,height:24,transform:'translate(-50%,-50%)',borderRadius:'50%',background:T.surface,border:'2px solid '+fc,boxShadow:'0 1px 4px rgba(28,52,86,0.16)'}}/>
        ) : (
          <span style={{position:'absolute',top:0,left:0,right:0,bottom:0,display:'flex',alignItems:'center',justifyContent:'center',fontFamily:_MONO,fontSize:8,fontWeight:600,letterSpacing:'0.10em',textTransform:'uppercase',color:T.faint}}>Pick Hold or Set</span>
        )}
        <span style={{position:'absolute',left:6,bottom:2,...lbl2}}>REV</span>
        <span style={{position:'absolute',right:6,bottom:2,...lbl2}}>FWD</span>
      </div>
      <span style={{fontFamily:_MONO,fontWeight:700,fontSize:12,color:engaged?fc:T.faint,fontFeatureSettings:'"tnum","zero"'}}>{thrVal>0?'+':''}{thrVal}%</span>
      {motorMode==='hold' && <span style={{fontFamily:_MONO,fontSize:9,fontWeight:700,color:T.warn,padding:'4px 7px',borderRadius:6,background:T.warnSoft,whiteSpace:'nowrap'}}>Hold</span>}
      {motorMode==='set' && <span style={{fontFamily:_MONO,fontSize:9,fontWeight:700,color:T.danger,padding:'4px 7px',borderRadius:6,background:T.dangerSoft,border:'1px solid '+T.danger,whiteSpace:'nowrap',cursor:'pointer'}}>⊘ Neut</span>}
    </div>
  );
}

function LandMap({ T, d }) {
  const W=340, H=140;
  const bx=W*0.60, by=H*0.55, hx=W*0.30, hy=H*0.40;
  const shore = 'M0,0 L'+Math.round(W*0.44)+',0 L'+Math.round(W*0.47)+','+Math.round(H*0.14)+' L'+Math.round(W*0.40)+','+Math.round(H*0.32)+' L'+Math.round(W*0.30)+','+Math.round(H*0.42)+' L'+Math.round(W*0.18)+','+Math.round(H*0.44)+' L'+Math.round(W*0.08)+','+Math.round(H*0.38)+' L0,'+Math.round(H*0.30)+' Z';
  const pts = [[bx-W*0.17,by+H*0.16],[bx-W*0.12,by+H*0.08],[bx-W*0.06,by+H*0.02],[bx,by]];
  const track = pts.map(function(p,i){ return (i===0?'M':'L')+Math.round(p[0])+','+Math.round(p[1]); }).join(' ');
  const isDusk = T.id==='dusk';
  const water = isDusk ? '#0e1f36' : '#d4e4f0';
  const land  = isDusk ? '#1a2e1a' : '#c8d8b8';
  const shoreS= isDusk ? '#2a4a2a' : '#a0b890';
  const grid  = isDusk ? 'rgba(255,255,255,0.04)' : 'rgba(28,52,86,0.05)';
  const ring  = isDusk ? 'rgba(90,143,230,0.15)' : 'rgba(28,78,160,0.10)';
  const gridLines = [];
  for (var gi=0;gi<7;gi++) gridLines.push(gi);
  const hLines = [];
  for (var hi=0;hi<5;hi++) hLines.push(hi);
  return (
    <svg width={W} height={H} viewBox={'0 0 '+W+' '+H} style={{display:'block',width:'100%',height:'100%'}}>
      <rect width={W} height={H} fill={water}/>
      {gridLines.map(function(i){ return <line key={'v'+i} x1={Math.round(W/7*i)} y1={0} x2={Math.round(W/7*i)} y2={H} stroke={grid} strokeWidth="1"/>; })}
      {hLines.map(function(i){ return <line key={'h'+i} x1={0} y1={Math.round(H/5*i)} x2={W} y2={Math.round(H/5*i)} stroke={grid} strokeWidth="1"/>; })}
      <path d={shore} fill={land} stroke={shoreS} strokeWidth="1.5"/>
      {[35,70,115].map(function(r){ return <circle key={r} cx={Math.round(hx)} cy={Math.round(hy)} r={r} fill="none" stroke={ring} strokeWidth="1" strokeDasharray="4 5"/>; })}
      <line x1={Math.round(bx)} y1={Math.round(by)} x2={Math.round(hx)} y2={Math.round(hy)} stroke={T.accent} strokeWidth="1" strokeDasharray="4 4" opacity="0.45"/>
      <path d={track} fill="none" stroke={T.accent} strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" opacity="0.75"/>
      <circle cx={Math.round(hx)} cy={Math.round(hy)} r="6" fill={T.safe}/>
      <text x={Math.round(hx)} y={Math.round(hy)+1} textAnchor="middle" dominantBaseline="middle" fontSize="7" fontWeight="800" fill="#fff">H</text>
      <circle cx={Math.round(bx)} cy={Math.round(by)} r="9" fill={T.surface} stroke={T.accent} strokeWidth="2"/>
      <polygon points={(Math.round(bx))+','+(Math.round(by)-6)+' '+(Math.round(bx)+4)+','+(Math.round(by)+4)+' '+Math.round(bx)+','+(Math.round(by)+2)+' '+(Math.round(bx)-4)+','+(Math.round(by)+4)} fill={T.accent}/>
      <text x={W-12} y={14} textAnchor="middle" fontSize="10" fontWeight="800" fontFamily={_MONO} fill={T.text}>N</text>
      <polygon points={(W-12)+',18 '+(W-15)+',26 '+(W-12)+',24 '+(W-9)+',26'} fill={T.accent}/>
    </svg>
  );
}

function Landscape2({ T, tab, setTab, motorMode, setMotorMode, inControl, setInControl, setOrient, theme, setTheme }) {
  const d = TELE;
  const engaged = motorMode !== 'off';
  const lbl = function(x) { return Object.assign({ fontFamily:_MONO, fontSize:8.5, fontWeight:600, letterSpacing:'0.12em', textTransform:'uppercase', color:T.dim }, x||{}); };
  const val = function(s,c) { return { fontFamily:_MONO, fontWeight:700, fontSize:s||14, color:c||T.text, fontFeatureSettings:'"tnum","zero"', lineHeight:1 }; };
  const card = { background:T.surface, border:'1px solid '+T.border, borderRadius:14, boxShadow:'0 1px 3px rgba(28,52,86,0.05)' };
  const LTABS = [['control','Control'],['instr','Instr'],['map','Map'],['devices','Devices'],['console','Console']];
  const isDusk = T.id === 'dusk';
  const badgeBg = isDusk ? 'rgba(18,29,46,0.88)' : 'rgba(255,255,255,0.88)';
  const gradBg = isDusk ? 'linear-gradient(transparent,rgba(18,29,46,0.90) 50%)' : 'linear-gradient(transparent,rgba(255,255,255,0.90) 50%)';

  return (
    <div style={{width:'100%',height:'100%',background:T.bg,display:'flex',flexDirection:'column',overflow:'hidden'}}>
      {/* top bar */}
      <div style={{display:'flex',alignItems:'center',gap:10,padding:'6px 14px',background:T.surface,borderBottom:'2px solid '+T.accent,flexShrink:0}}>
        <div style={{display:'flex',alignItems:'center',gap:8,flexShrink:0}}>
          <svg viewBox="0 0 1446 332" height="22" style={{display:'block'}} fill="none">
            <g transform="translate(-1127 -1319)">
              <path d="M2092.02 1471.14C2091.93 1475.36 2211.1 1511.7 2208.14 1529.07 2205.18 1546.43 2131.11 1561.05 2074.25 1575.36 2017.39 1589.67 1931.56 1606.24 1866.96 1614.93 1802.36 1623.61 1742.65 1651.47 1686.66 1627.47 1891.87 1484.34 1704.44 1481.14 1535.61 1443.47 1366.78 1405.81 1146.87 1447.39 1141.1 1428.98 1135.34 1410.56 1369.82 1333.4 1501.03 1333 1632.24 1332.6 1805.65 1419.92 1928.36 1426.56 2051.07 1433.2 2132.43 1381.09 2237.3 1372.82 2342.17 1364.56 2579.57 1407.98 2557.57 1376.99"
                stroke={T.accent} strokeWidth="28" strokeMiterlimit="8" strokeLinecap="round"/>
            </g>
          </svg>
          <span style={{fontSize:15,fontWeight:700,letterSpacing:'-0.01em',color:T.accent}}>Mistral</span>
        </div>
        <div style={{display:'flex',gap:1,padding:3,borderRadius:10,background:T.inset,border:'1px solid '+T.border,flexShrink:0}}>
          {LTABS.map(function(item){
            var id=item[0], label=item[1], on=id===tab;
            return <div key={id} onClick={function(){ setTab(id); }} style={{padding:'5px 11px',borderRadius:7,background:on?T.surface:'transparent',boxShadow:on?'0 1px 2px rgba(28,52,86,0.10)':'none',fontFamily:_MONO,fontSize:10,fontWeight:700,color:on?T.accent:T.faint,cursor:'pointer',whiteSpace:'nowrap'}}>{label}</div>;
          })}
        </div>
        <span style={{flex:1}}/>
        {[['Batt',d.volts.toFixed(1)+'V'],['LQ',d.lq+'%'],['Sats',''+d.sats],['Spd',d.spd.toFixed(1)+' kn']].map(function(kv){
          return <div key={kv[0]} style={{display:'flex',alignItems:'baseline',gap:5}}><span style={lbl({fontSize:8})}>{kv[0]}</span><span style={val(11.5)}>{kv[1]}</span></div>;
        })}
        <div onClick={function(){ setTheme(function(t){ return t==='day'?'dusk':'day'; }); }} style={{fontFamily:_MONO,fontSize:9,fontWeight:700,color:T.faint,padding:'4px 9px',borderRadius:6,background:T.inset,border:'1px solid '+T.border,cursor:'pointer',flexShrink:0}}>
          {theme==='day'?'☀ DAY':'☾ DUSK'}
        </div>
        <div onClick={function(){ setOrient('portrait'); }} style={{fontFamily:_MONO,fontSize:9,fontWeight:700,color:T.faint,padding:'4px 9px',borderRadius:6,background:T.inset,border:'1px solid '+T.border,cursor:'pointer',flexShrink:0}}>
          ⇅ PORTRAIT
        </div>
        <div onClick={function(){ if(tab==='control') setInControl(function(v){ return !v; }); }} style={{display:'inline-flex',alignItems:'center',gap:6,padding:'5px 11px',borderRadius:99,background:inControl?T.accent:T.safeSoft,color:inControl?'#fff':T.safe,fontSize:10,fontWeight:700,letterSpacing:'0.04em',textTransform:'uppercase',flexShrink:0,cursor:tab==='control'?'pointer':'default'}}>
          <span style={{width:6,height:6,borderRadius:99,background:inControl?'#fff':T.safe}}/>
          {inControl?'Helm active':'Linked'}
        </div>
      </div>

      {/* helm handoff banner — only on Control tab, only when not in control */}
      {tab === 'control' && !inControl && (
        <div onClick={function(){ setInControl(true); }} style={{display:'flex',alignItems:'center',gap:10,padding:'8px 16px',background:T.inset,borderBottom:'1px solid '+T.border,cursor:'pointer',flexShrink:0}}>
          <svg width="13" height="13" viewBox="0 0 24 24" fill={T.dim}><path d="M12 2a3 3 0 0 1 3 3v1.6a6 6 0 0 1 3 5.2V18l2 2v1H4v-1l2-2v-5.2a6 6 0 0 1 3-5.2V5a3 3 0 0 1 3-3z"/></svg>
          <span style={{fontFamily:_MONO,fontSize:11,fontWeight:700,letterSpacing:'0.09em',color:T.dim}}>TAP TO TAKE HELM</span>
          <span style={{flex:1}}/>
          <span style={{fontFamily:_MONO,fontSize:9,color:T.faint,letterSpacing:'0.05em'}}>STOP CUTS POWER · CENTRES RUDDER</span>
        </div>
      )}

      {/* body */}
      {tab === 'control' ? (
      <div style={{flex:1,display:'flex',gap:10,padding:11,minHeight:0}}>

        {/* LEFT — SAIL */}
        <div style={Object.assign({},card,{width:136,padding:12,display:'flex',flexDirection:'column',alignItems:'center',gap:8})}>
          <div style={{display:'flex',alignItems:'baseline',justifyContent:'space-between',width:'100%'}}>
            <span style={lbl()}>Sail</span>
            <span style={val(14,T.accent)}>{d.sail}%</span>
          </div>
          <div style={{position:'relative',flex:1,width:54,minHeight:0,background:T.inset,borderRadius:27,border:'1px solid '+T.border}}>
            <span style={{position:'absolute',top:8,left:0,right:0,textAlign:'center',fontFamily:_MONO,fontSize:8,fontWeight:600,letterSpacing:'0.10em',textTransform:'uppercase',color:T.faint}}>In</span>
            <span style={{position:'absolute',bottom:8,left:0,right:0,textAlign:'center',fontFamily:_MONO,fontSize:8,fontWeight:600,letterSpacing:'0.10em',textTransform:'uppercase',color:T.faint}}>Out</span>
            <div style={{position:'absolute',left:'50%',top:'12%',width:5,transform:'translateX(-50%)',borderRadius:3,height:((100-d.sail)*0.74)+'%',background:T.accent}}/>
            <div style={{position:'absolute',left:'50%',top:((100-d.sail)*0.74+12)+'%',width:42,height:42,transform:'translate(-50%,-50%)',borderRadius:'50%',background:T.surface,border:'2.5px solid '+T.accent,boxShadow:'0 2px 6px rgba(28,52,86,0.18)',display:'flex',alignItems:'center',justifyContent:'center'}}>
              <span style={val(11,T.accent)}>{d.sail}</span>
            </div>
          </div>
          <span style={lbl({fontSize:8.5,color:T.dim})}>trimmed</span>
        </div>

        {/* CENTER — map + motor + stop */}
        <div style={{flex:1,display:'flex',flexDirection:'column',gap:10,minWidth:0}}>
          <div style={Object.assign({},card,{flex:1,overflow:'hidden',position:'relative',minHeight:0})}>
            <LandMap T={T} d={d}/>
            <div style={{position:'absolute',top:8,right:10,display:'flex',alignItems:'center',gap:6,padding:'4px 9px',borderRadius:8,background:badgeBg,border:'1px solid '+T.border,whiteSpace:'nowrap'}}>
              <svg width="11" height="11" viewBox="0 0 24 24" fill="none" stroke={T.safe} strokeWidth="2.2" strokeLinecap="round"><path d="M3 11l9-7 9 7"/><path d="M5 10v9h14v-9"/></svg>
              <span style={val(12)}>{d.homeDist} m</span>
              <span style={val(10,T.dim)}>{(''+d.homeBrg).padStart(3,'0')}°</span>
            </div>
            <div style={{position:'absolute',left:0,right:0,bottom:0,display:'flex',gap:14,padding:'6px 12px',background:gradBg}}>
              {[['Hdg',d.hdg+'°'],['Heel',d.roll.toFixed(1)+'°'],['Pitch',d.pitch.toFixed(1)+'°']].map(function(kv){
                return <div key={kv[0]} style={{display:'flex',alignItems:'baseline',gap:5}}><span style={lbl({fontSize:8})}>{kv[0]}</span><span style={val(11.5)}>{kv[1]}</span></div>;
              })}
            </div>
          </div>

          <div style={{display:'flex',gap:10,flexShrink:0}}>
            <div style={Object.assign({},card,{flex:1,padding:'8px 12px',display:'flex',flexDirection:'column',gap:7,border:engaged?'1.5px solid '+T.warn:'1px solid '+T.border})}>
              <div style={{display:'flex',alignItems:'center',gap:8}}>
                <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke={engaged?T.warn:T.dim} strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><circle cx="12" cy="12" r="3.4"/><path d="M12 3v3M12 18v3M3 12h3M18 12h3M5.6 5.6l2.1 2.1M16.3 16.3l2.1 2.1M18.4 5.6l-2.1 2.1M7.7 16.3l-2.1 2.1"/></svg>
                <span style={{fontSize:11.5,fontWeight:700,color:engaged?T.warn:T.text}}>Motor</span>
                <span style={lbl({fontSize:7.5,color:T.faint})}>· aux</span>
                <span style={{flex:1}}/>
                <div style={{display:'flex',gap:2,padding:2,borderRadius:8,background:T.inset,border:'1px solid '+T.border}}>
                  {[['off','Off'],['hold','Hold'],['set','Set']].map(function(item){
                    var id=item[0], label=item[1], on=id===motorMode;
                    return <div key={id} onClick={function(){ setMotorMode(id); }} style={{padding:'4px 9px',borderRadius:6,background:on?T.surface:'transparent',boxShadow:on?'0 1px 2px rgba(28,52,86,0.10)':'none',fontSize:10,fontWeight:700,color:on?(id==='off'?T.dim:T.warn):T.dim,cursor:'pointer'}}>{label}</div>;
                  })}
                </div>
              </div>
              <LandThrottle T={T} motorMode={motorMode} d={d}/>
            </div>
            {inControl && (
              <div onClick={function(){ setInControl(false); }} style={{width:90,borderRadius:12,background:T.surface,border:'1.5px solid '+T.borderStrong,color:T.text,fontSize:12,fontWeight:700,fontFamily:_MONO,letterSpacing:'0.06em',display:'flex',alignItems:'center',justifyContent:'center',cursor:'pointer'}}>RELEASE</div>
            )}
            <div style={{width:130,borderRadius:12,background:T.danger,color:'#fff',fontSize:15,fontWeight:800,letterSpacing:'0.08em',display:'flex',alignItems:'center',justifyContent:'center',gap:8,cursor:'pointer'}}>
              <span style={{width:11,height:11,background:'#fff',borderRadius:2}}/>STOP
            </div>
          </div>
        </div>

        {/* RIGHT — attitude + rudder */}
        <div style={{width:210,display:'flex',flexDirection:'column',gap:10,minHeight:0}}>
          <div style={Object.assign({},card,{padding:'10px 12px',display:'flex',alignItems:'center',gap:12,flexShrink:0})}>
            <H2Att roll={d.roll} pitch={d.pitch} size={68} T={T}/>
            <div style={{display:'grid',gridTemplateColumns:'1fr 1fr',gap:'6px 10px'}}>
              {[['Heel',d.roll.toFixed(1)+'°'],['Pitch',d.pitch.toFixed(1)+'°']].map(function(kv){
                return <div key={kv[0]} style={{display:'flex',flexDirection:'column',gap:2}}><span style={lbl({fontSize:8})}>{kv[0]}</span><span style={val(14)}>{kv[1]}</span></div>;
              })}
            </div>
          </div>
          <div style={Object.assign({},card,{flex:1,padding:'11px 12px',display:'flex',flexDirection:'column',gap:8,minHeight:0})}>
            <div style={{display:'flex',alignItems:'baseline',justifyContent:'space-between'}}>
              <span style={lbl()}>Rudder</span>
              <div style={{display:'flex',gap:9,alignItems:'baseline'}}>
                <span style={{fontFamily:_MONO,fontSize:9,fontWeight:700,color:T.warn}}>Trim +{Math.round(d.trim*100)}%</span>
                <span style={val(14,T.accent)}>+{d.rud}°</span>
              </div>
            </div>
            <div style={{position:'relative',flex:1,minHeight:0,background:T.inset,borderRadius:12,border:'1px solid '+T.border,overflow:'hidden'}}>
              <div style={{position:'absolute',left:'50%',top:'12%',bottom:'12%',width:1.5,background:T.borderStrong,transform:'translateX(-50%)'}}/>
              <div style={{position:'absolute',top:0,bottom:0,left:((d.trim*0.42+0.5)*100)+'%',width:2,background:T.warn,opacity:0.85,transform:'translateX(-50%)'}}/>
              <div style={{position:'absolute',top:'50%',left:((d.rud/35*0.42+0.5)*100)+'%',width:50,height:50,transform:'translate(-50%,-50%)',borderRadius:'50%',background:T.surface,border:'2.5px solid '+T.accent,boxShadow:'0 2px 6px rgba(28,52,86,0.18)',display:'flex',alignItems:'center',justifyContent:'center'}}>
                <span style={val(12,T.accent)}>+{d.rud}</span>
              </div>
              <span style={{position:'absolute',left:9,bottom:6,fontFamily:_MONO,fontSize:8.5,fontWeight:700,letterSpacing:'0.06em',textTransform:'uppercase',color:T.faint}}>Port</span>
              <span style={{position:'absolute',right:9,bottom:6,fontFamily:_MONO,fontSize:8.5,fontWeight:700,letterSpacing:'0.06em',textTransform:'uppercase',color:T.faint}}>Stbd</span>
              <span style={{position:'absolute',left:0,right:0,top:7,textAlign:'center',fontFamily:_MONO,fontSize:7.5,fontWeight:600,color:T.faint}}>drag · self-centres</span>
            </div>
            <div style={{display:'grid',gridTemplateColumns:'1fr 1.3fr 1fr',gap:5}}>
              {['−','Centre','+'].map(function(t){
                return <div key={t} style={{textAlign:'center',padding:'8px 4px',borderRadius:7,background:T.inset,border:'1px solid '+T.border,fontFamily:_MONO,fontSize:11,fontWeight:700,color:T.dim,cursor:'pointer'}}>{t}</div>;
              })}
            </div>
          </div>
        </div>
      </div>
      ) : (
        <div style={{flex:1,overflowY:'auto',background:T.bg}}>
          <TelemetryBar T={T}/>
          {tab==='instr'   && <Instr2 T={T}/>}
          {tab==='map'     && <Map2 T={T}/>}
          {tab==='devices' && <Dev2 T={T}/>}
          {tab==='console' && <Con2 T={T}/>}
        </div>
      )}
    </div>
  );
}
Object.assign(window, { Landscape2, LandThrottle, LandMap });
