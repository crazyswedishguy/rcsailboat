// dev.jsx — Devices tab (portrait).
// Per-device status cards with 4-column stat grids.  Live telemetry data is
// overlaid on the static device metadata wherever we have a live reading.

// Props: { T, d, stale, deviceStatus }
//   deviceStatus — map from device id → level ('ok'|'warn'|'error'|'absent')
//                  from the CRSF 0x81 device-status frame

const Dev = ({ T, d, stale, deviceStatus }) => {
  const ds = deviceStatus || {};

  const lbl = (x) => ({ fontFamily:_MONO, fontSize:8.5, fontWeight:600,
    letterSpacing:'0.13em', textTransform:'uppercase', color:T.dim, ...(x||{}) });
  const val = (s,c) => ({ fontFamily:_MONO, fontWeight:700, fontSize:s||13,
    color:c||T.text, fontFeatureSettings:'"tnum","zero"', lineHeight:1 });

  // Map a level string to a theme colour
  function levelColor(level) {
    if (level==='ok')    return T.safe;
    if (level==='warn')  return T.warn;
    if (level==='error') return T.danger;
    return T.faint; // absent / unknown
  }

  // Status dot component
  const Dot = ({ level='absent' }) => (
    <span style={{ width:8, height:8, borderRadius:99, flexShrink:0,
      background:levelColor(level),
      boxShadow:level==='ok'?`0 0 0 3px ${T.safeSoft}`:'none' }}/>
  );

  // Overall health: any error → danger, any warn → warn, else ok
  const levels = Object.values(ds);
  const summaryLevel = levels.includes('error') ? 'error'
    : levels.includes('warn') ? 'warn' : 'ok';
  const summaryText = summaryLevel==='error' ? 'FAULT DETECTED'
    : summaryLevel==='warn' ? 'WARNING' : 'ALL SYSTEMS NOMINAL';
  const onlineCount = levels.filter(l => l==='ok'||l==='warn').length;

  // ── Device definitions ─────────────────────────────────────────────────────
  // Each entry has a fixed id, name, subtitle, and default stats.
  // Live readings from `d` and `ds` overlay the defaults where available.
  const devices = [
    {
      id:'rpi', name:'Raspberry Pi', sub:'Base station · Python 3.11',
      level:'ok',
      stats:[['CPU','—'],['RAM','—'],['TEMP',`${d.temp}°C`],['UPTIME','—']],
    },
    {
      id:'elrs', name:'ELRS Receiver', sub:'ExpressLRS · UART1',
      level: ds.ft || 'absent',
      stats:[
        ['LQ',   stale.link ? '—' : `${d.lq}%`],
        ['RSSI', stale.link ? '—' : `${d.rssi} dBm`],
        ['SNR',  '—'],
        ['RATE', '50 Hz'],
      ],
    },
    {
      id:'gps', name:'GPS Module', sub:'BN-880 · UART2',
      level: d.sats>=3 ? 'ok' : d.sats>0 ? 'warn' : 'absent',
      stats:[
        ['SATS', stale.gps ? '—' : `${d.sats}`],
        ['FIX',  stale.gps ? '—' : d.sats>=3 ? '3D' : 'No fix'],
        ['SPD',  stale.gps ? '—' : `${d.spd.toFixed(1)} kn`],
        ['ALT',  stale.gps ? '—' : `${d.alt}m`],
      ],
    },
    {
      id:'imu', name:'IMU / Compass', sub:'QMI8658 · I²C 0x6B',
      level: ds.qmi || 'absent',
      stats:[
        ['HEEL',  stale.telem ? '—' : `${d.roll.toFixed(1)}°`],
        ['PITCH', stale.telem ? '—' : `${d.pitch.toFixed(1)}°`],
        ['HDG',   stale.telem ? '—' : `${d.hdg}°`],
        ['STATUS', ds.qmi || '—'],
      ],
    },
    {
      id:'pca', name:'Servo Driver', sub:'PCA9685 · I²C 0x40',
      level: ds.pca || 'absent',
      stats:[
        ['RUDDER', `${d.rud>=0?'+':''}${d.rud}°`],
        ['SAIL',   `${d.sail}%`],
        ['MOTOR',  `${d.thr}%`],
        ['FREQ',   '50 Hz'],
      ],
    },
    {
      id:'ina', name:'Battery Monitor', sub:'INA228 · I²C 0x41',
      level: ds.ina || 'absent',
      stats:[
        ['V',   stale.telem ? '—' : `${d.volts.toFixed(2)} V`],
        ['A',   stale.telem ? '—' : `${d.amps.toFixed(2)} A`],
        ['mAh', stale.telem ? '—' : `${d.mah}`],
        ['CHG', stale.telem ? '—' : `${d.chg}%`],
      ],
    },
    {
      id:'bilge', name:'Bilge Sensor', sub:'Float switch · GPIO2',
      level: ds.bilge==='warn' ? 'warn' : ds.bilge==='ok' ? 'ok' : 'absent',
      stats:[
        ['STATE', d.bilgeWet ? 'WET' : 'DRY'],
        ['PUMP',  ds.pump || '—'],
        ['GPIO',  '2'],
        ['TYPE',  'Analog'],
      ],
    },
    {
      id:'rudder', name:'Rudder Servo', sub:'PCA9685 Ch 0',
      level: ds.rudder || 'absent',
      stats:[['POS',`${d.rud>=0?'+':''}${d.rud}°`],['RANGE','±35°'],['FREQ','50 Hz'],['TRIM',`${Math.round(d.trim*100)}%`]],
    },
    {
      id:'winch', name:'Sail Winch', sub:'PCA9685 Ch 1',
      level: ds.winch || 'absent',
      stats:[['POS',`${d.sail}%`],['RANGE','0–100%'],['FREQ','50 Hz'],['STATUS',ds.winch||'—']],
    },
  ];

  return (
    <div style={{ display:'flex', flexDirection:'column' }}>

      {/* ── Summary strip ────────────────────────────────────────────────── */}
      <Inset T={T} style={{ display:'flex', alignItems:'center', gap:10,
        padding:'9px 16px', borderBottom:`1px solid ${T.border}` }}>
        <Dot level={summaryLevel}/>
        <span style={lbl({ color:levelColor(summaryLevel) })}>{summaryText}</span>
        <span style={{ flex:1 }}/>
        <span style={{ fontFamily:_MONO, fontSize:10, color:T.dim }}>
          {onlineCount}/{devices.length} online
        </span>
      </Inset>

      {/* ── Device cards ─────────────────────────────────────────────────── */}
      {devices.map(dev => (
        <Card key={dev.id} T={T} style={{ padding:'12px 16px' }}>
          <div style={{ display:'flex', alignItems:'flex-start', gap:10, marginBottom:9 }}>
            <Dot level={dev.level}/>
            <div style={{ flex:1 }}>
              <div style={{ fontSize:13, fontWeight:700, color:T.text, lineHeight:1.2 }}>
                {dev.name}
              </div>
              <div style={{ fontFamily:_MONO, fontSize:9, color:T.faint,
                marginTop:2, letterSpacing:'0.04em' }}>{dev.sub}</div>
            </div>
          </div>
          {/* 4-column stat grid in an inset container */}
          <div style={{ display:'grid', gridTemplateColumns:'repeat(4,1fr)',
            background:T.inset, borderRadius:7,
            border:`1px solid ${T.border}`, overflow:'hidden' }}>
            {dev.stats.map(([k,v], i) => (
              <div key={k} style={{ padding:'8px 10px',
                borderRight:i<3?`1px solid ${T.border}`:'none',
                display:'flex', flexDirection:'column', gap:3 }}>
                <span style={lbl({ fontSize:7.5 })}>{k}</span>
                <span style={val(12)}>{v}</span>
              </div>
            ))}
          </div>
        </Card>
      ))}

      {/* ── Firmware section ─────────────────────────────────────────────── */}
      <Card T={T} style={{ padding:'12px 16px' }}>
        <div style={lbl({ color:T.accent, marginBottom:9 })}>FIRMWARE</div>
        <div style={{ display:'flex', flexDirection:'column', gap:6 }}>
          {[
            ['Mistral Pi App', 'dev'],
            ['ELRS',           '3.x'],
            ['Bootloader',     '—'],
            ['OS',             'RPi OS 12'],
          ].map(([k,v]) => (
            <div key={k} style={{ display:'flex',
              justifyContent:'space-between', alignItems:'baseline' }}>
              <span style={{ fontSize:12, color:T.text }}>{k}</span>
              <span style={val(11,T.dim)}>{v}</span>
            </div>
          ))}
        </div>
      </Card>

    </div>
  );
};
