// ── Control screen ──
function Ctrl2({ T, inControl, onTakeControl, onRelease, onStop, motorMode, setMotorMode }) {
  const d = TELE;
  const lbl = (x) => ({ fontFamily:_MONO, fontSize:8.5, fontWeight:600, letterSpacing:'0.13em', textTransform:'uppercase', color:T.dim, ...(x||{}) });
  const val = (s,c) => ({ fontFamily:_MONO, fontWeight:700, fontSize:s||21, color:c||T.text, fontFeatureSettings:'"tnum","zero"', lineHeight:1 });
  const btn = (bg,col,brd) => ({ display:'flex', alignItems:'center', justifyContent:'center', padding:'13px 8px', borderRadius:8, background:bg, color:col, border:brd||'none', fontFamily:_MONO, fontSize:12, fontWeight:800, letterSpacing:'0.06em', cursor:'pointer' });
  const motorOn = motorMode !== 'off';
  const thrVal = motorMode==='hold' ? d.thr : motorMode==='set' ? d.thrRev : 0;
  const thrPct = 50 + thrVal/2;
  const thrFwd = thrVal >= 0;
  const thrFill = thrFwd ? T.warn : T.danger;

  return (
    <div style={{ display:'flex', flexDirection:'column' }}>

      {/* attitude + nav */}
      <HCell T={T} style={{ display:'flex' }}>
        <div style={{ padding:'13px 14px', borderRight:`1px solid ${T.border}`, flexShrink:0 }}>
          <H2Att roll={d.roll} pitch={d.pitch} size={100} T={T}/>
        </div>
        <div style={{ flex:1, display:'grid', gridTemplateRows:'1fr 1fr', gridTemplateColumns:'1fr 1fr' }}>
          {[['HEEL',d.roll.toFixed(1)+'°'],['HDG',d.hdg+'°'],['SPEED',d.spd.toFixed(1)+' kn'],['HOME',d.homeDist+'m']].map(([l,v],i) => (
            <div key={l} style={{ padding:'10px 13px', display:'flex', flexDirection:'column', gap:4, justifyContent:'center', borderRight:i%2===0?`1px solid ${T.border}`:'none', borderTop:i>=2?`1px solid ${T.border}`:'none' }}>
              <div style={lbl()}>{l}</div>
              {l==='HOME'
                ? <div style={{ display:'flex', alignItems:'center', gap:5 }}>
                    <svg width="12" height="12" viewBox="0 0 24 24" fill={T.accent} style={{ transform:`rotate(${d.homeBrg}deg)` }}><path d="M12 2l5 10-5-3-5 3z"/></svg>
                    <span style={val(17)}>{v}</span>
                  </div>
                : <span style={val(17)}>{v}</span>}
            </div>
          ))}
        </div>
      </HCell>

      {/* bilge */}
      <HInset T={T} style={{ display:'flex', alignItems:'center', gap:9, padding:'8px 16px', borderBottom:`1px solid ${T.border}` }}>
        <span style={{ width:7, height:7, borderRadius:99, background:d.bilgeWet?T.danger:T.safe }}/>
        <span style={lbl({ color:T.text })}>{d.bilgeWet?'WATER IN BILGE':'BILGE DRY'}</span>
        <span style={{ flex:1 }}/>
        <span style={{ fontFamily:_MONO, fontSize:10, fontWeight:600, color:T.dim }}>{d.temp}°C</span>
      </HInset>

      {/* SAIL — primary */}
      <HCell T={T} style={{ display:'flex' }}>
        <div style={{ padding:'13px 14px', borderRight:`1px solid ${T.border}`, flexShrink:0 }}>
          <H2Sail value={d.sail/100} size={116} T={T}/>
        </div>
        <div style={{ flex:1, padding:'13px 14px', display:'flex', flexDirection:'column', gap:10, justifyContent:'center' }}>
          <div style={{ display:'flex', alignItems:'baseline', justifyContent:'space-between' }}>
            <span style={lbl()}>SAIL TRIM</span>
            <span style={val(26,T.accent)}>{d.sail}<span style={{ fontSize:13 }}>%</span></span>
          </div>
          <div style={{ display:'grid', gridTemplateColumns:'1fr 1fr', gap:6 }}>
            <div style={btn(T.accentSoft,T.accent,`1.5px solid ${T.accent}`)}>◀ IN</div>
            <div style={btn(T.inset,T.text,`1px solid ${T.border}`)}>OUT ▶</div>
          </div>
        </div>
      </HCell>

      {/* RUDDER */}
      <HCell T={T} style={{ padding:'13px 16px' }}>
        <div style={{ display:'flex', justifyContent:'space-between', alignItems:'baseline', marginBottom:9 }}>
          <span style={lbl()}>RUDDER</span>
          <div style={{ display:'flex', gap:12, alignItems:'baseline' }}>
            <span style={{ fontFamily:_MONO, fontSize:10, fontWeight:700, color:T.warn }}>TRIM {d.trim>=0?'+':''}{Math.round(d.trim*100)}%</span>
            <span style={val(18,T.accent)}>+{d.rud}°</span>
          </div>
        </div>
        <div style={{ position:'relative', height:52, background:T.inset, borderRadius:8, border:`1px solid ${T.border}`, overflow:'hidden' }}>
          {[...Array(9)].map((_,i)=><div key={i} style={{ position:'absolute', top:0, bottom:0, left:`${i/8*100}%`, width:1, background:i===4?T.borderStrong:T.border }}/>)}
          <div style={{ position:'absolute', top:0, bottom:0, left:`${(d.trim*0.42+0.5)*100}%`, width:2.5, transform:'translateX(-50%)', background:T.warn, opacity:0.85 }}/>
          <div style={{ position:'absolute', top:'50%', left:`${(d.rud/35*0.42+0.5)*100}%`, width:40, height:40, transform:'translate(-50%,-50%)', borderRadius:'50%', background:T.surface, border:`2.5px solid ${T.accent}`, boxShadow:'0 2px 6px rgba(28,52,86,0.16)', display:'flex', alignItems:'center', justifyContent:'center' }}>
            <span style={val(11,T.accent)}>+{d.rud}</span>
          </div>
          <span style={{ position:'absolute', left:9, bottom:5, fontFamily:_MONO, fontSize:8.5, fontWeight:700, color:T.faint, letterSpacing:'0.06em' }}>PORT</span>
          <span style={{ position:'absolute', right:9, bottom:5, fontFamily:_MONO, fontSize:8.5, fontWeight:700, color:T.faint, letterSpacing:'0.06em' }}>STBD</span>
        </div>
        <div style={{ display:'grid', gridTemplateColumns:'1fr 1.4fr 1fr', gap:5, marginTop:8 }}>
          {['− TRIM','CENTRE','TRIM +'].map(t=>(
            <div key={t} style={btn(T.inset,T.dim,`1px solid ${T.border}`)}>{t}</div>
          ))}
        </div>
      </HCell>

      {/* MOTOR — auxiliary */}
      <HCell T={T} style={{ padding:'11px 16px' }}>
        <div style={{ display:'flex', alignItems:'center', gap:10, marginBottom: motorOn ? 10 : 0 }}>
          <div style={{ width:30, height:30, borderRadius:7, background:T.inset, display:'flex', alignItems:'center', justifyContent:'center', flexShrink:0 }}>
            <svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke={motorOn?T.warn:T.dim} strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><circle cx="12" cy="12" r="3.4"/><path d="M12 3v3M12 18v3M3 12h3M18 12h3M5.6 5.6l2.1 2.1M16.3 16.3l2.1 2.1M18.4 5.6l-2.1 2.1M7.7 16.3l-2.1 2.1"/></svg>
          </div>
          <div style={{ flex:1 }}>
            <span style={{ fontSize:12.5, fontWeight:700, color:motorOn?T.warn:T.text }}>Motor</span>
            <span style={{ fontFamily:_MONO, fontSize:8, fontWeight:600, letterSpacing:'0.10em', color:T.faint, marginLeft:7 }}>AUX</span>
          </div>
          <div style={{ display:'flex', gap:3, padding:3, borderRadius:9, background:T.inset, border:`1px solid ${T.border}` }}>
            {[['off','Off'],['hold','Hold'],['set','Set']].map(([id,label])=>{
              const on=id===motorMode;
              return <div key={id} onClick={()=>setMotorMode(id)} style={{ padding:'5px 11px', borderRadius:6, background:on?T.surface:'transparent', boxShadow:on?'0 1px 2px rgba(28,52,86,0.12)':'none', fontSize:11, fontWeight:700, color:on?(id==='off'?T.dim:T.warn):T.dim, cursor:'pointer' }}>{label}</div>;
            })}
          </div>
        </div>
        {motorOn && (
          <div style={{ display:'flex', alignItems:'center', gap:10 }}>
            <div style={{ position:'relative', flex:1, height:30, background:T.inset, borderRadius:15, border:`1px solid ${T.border}` }}>
              <div style={{ position:'absolute', left:'50%', top:'18%', bottom:'18%', width:1.5, background:T.borderStrong, transform:'translateX(-50%)' }}/>
              {Math.abs(thrVal)>0 && <div style={{ position:'absolute', top:'22%', bottom:'22%', left:thrFwd?'50%':`${thrPct}%`, width:thrFwd?`${Math.abs(thrVal)/2}%`:`${(50-thrPct)}%`, background:thrFill, opacity:0.20, borderRadius:6 }}/>}
              <div style={{ position:'absolute', top:'50%', left:`${thrPct}%`, width:28, height:28, transform:'translate(-50%,-50%)', borderRadius:'50%', background:T.surface, border:`2.5px solid ${thrFill}`, boxShadow:'0 2px 5px rgba(28,52,86,0.16)' }}/>
              <span style={{ position:'absolute', left:7, bottom:3, fontFamily:_MONO, fontSize:7.5, fontWeight:700, color:T.faint }}>REV</span>
              <span style={{ position:'absolute', right:7, bottom:3, fontFamily:_MONO, fontSize:7.5, fontWeight:700, color:T.faint }}>FWD</span>
            </div>
            <div style={{ minWidth:42, textAlign:'right' }}>
              <div style={val(13, thrFwd?T.warn:T.danger)}>{thrVal>0?'+':''}{thrVal}%</div>
              <div style={{ fontFamily:_MONO, fontSize:7.5, fontWeight:700, color:thrFwd?T.warn:T.danger }}>{thrVal===0?'NEUT':thrFwd?'FWD':'REV'}</div>
            </div>
            {motorMode==='hold' && <span style={{ fontFamily:_MONO, fontSize:9, fontWeight:700, color:T.warn, padding:'5px 8px', borderRadius:7, background:T.warnSoft, whiteSpace:'nowrap' }}>Hold to drive</span>}
            {motorMode==='set' && <span onClick={()=>{}} style={{ fontFamily:_MONO, fontSize:10, fontWeight:700, color:T.danger, padding:'6px 10px', borderRadius:7, background:T.dangerSoft, border:`1px solid ${T.danger}`, whiteSpace:'nowrap', cursor:'pointer' }}>⊘ Neutral</span>}
          </div>
        )}
      </HCell>

      {/* RELEASE + STOP */}
      <div style={{ display:'flex', gap:10, padding:'14px 16px 20px', background:T.bg }}>
        <div onClick={onRelease} style={{ ...btn(T.surface,T.text,`1.5px solid ${T.borderStrong}`), flex:1, borderRadius:10 }}>RELEASE</div>
        <div onClick={onStop} style={{ ...btn(T.danger,'#fff'), flex:1.6, borderRadius:10, boxShadow:'0 3px 14px rgba(192,57,43,0.28)', gap:8 }}>
          <span style={{ width:11, height:11, background:'#fff', borderRadius:2, display:'inline-block' }}/>STOP
        </div>
      </div>
    </div>
  );
}
Object.assign(window, { Ctrl2 });
