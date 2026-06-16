// map.jsx — Map tab (portrait).
// Leaflet map with offline OSM tiles served by the Pi at /tiles/{z}/{x}/{y}.png.
// Falls back to a "tiles not ready" message if Leaflet hasn't been loaded.
// Run `scripts/download_tiles.py --fetch-leaflet` once to pre-fetch tiles + Leaflet.

// Props: { T, d, stale, homePos, setHomePos }

const MapTab = ({ T, d, stale, homePos, setHomePos }) => {
  const { useRef, useEffect, useState } = React;
  const containerRef = useRef(null);
  const lfRef       = useRef(null);   // { map, boat, track, home, boatIcon, homeIcon }
  const trackPtsRef = useRef([]);
  const [flash, setFlash] = useState('');

  function flashMsg(m) { setFlash(m); setTimeout(() => setFlash(''), 1500); }

  function makeBoatIcon(L, hdg, isStale) {
    const color = isStale ? T.faint : T.accent;
    return L.divIcon({
      html: `<svg width="22" height="22" viewBox="0 0 22 22"
        style="overflow:visible;opacity:${isStale ? 0.55 : 1}">
        <circle cx="11" cy="11" r="8" fill="${T.surface}" stroke="${color}" stroke-width="2"/>
        <polygon points="11,3 14.5,15 11,13 7.5,15" fill="${color}"
          transform="rotate(${hdg || 0},11,11)"/>
      </svg>`,
      iconSize: [22, 22], iconAnchor: [11, 11], className: '',
    });
  }

  function makeHomeIcon(L) {
    return L.divIcon({
      html: `<svg width="20" height="20" viewBox="0 0 20 20">
        <circle cx="10" cy="10" r="9" fill="${T.safe}" opacity="0.9"
          stroke="#fff" stroke-width="1.5"/>
        <text x="10" y="14" text-anchor="middle" font-size="10" font-weight="700"
          fill="#fff" font-family="monospace">H</text>
      </svg>`,
      iconSize: [20, 20], iconAnchor: [10, 10], className: '',
    });
  }

  // ── Initialise Leaflet map once on mount ──────────────────────────────────
  useEffect(() => {
    const L = window.L;
    if (!L || lfRef.current) return;

    const defaultCenter = [40.934, -73.071]; // Port Jefferson Harbor
    const initCenter = homePos ? [homePos.lat, homePos.lng]
                     : (d.lat  ? [d.lat, d.lon] : defaultCenter);

    const map = L.map(containerRef.current, {
      center: initCenter,
      zoom: 14,
      zoomControl: true,
    });

    L.tileLayer('/tiles/{z}/{x}/{y}.png', {
      maxZoom: 17,
      minZoom: 8,
      attribution: '© <a href="https://openstreetmap.org">OpenStreetMap</a> contributors',
      // Transparent 1×1 PNG for tiles that haven't been downloaded yet
      errorTileUrl: 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNkYAAAAAYAAjCB0C8AAAAASUVORK5CYII=',
    }).addTo(map);

    L.control.scale({ maxWidth: 100, imperial: true }).addTo(map);

    const boatIcon = makeBoatIcon(L, d.hdg, false);
    const homeIcon = makeHomeIcon(L);

    const initPos = (d.lat && d.lon) ? [d.lat, d.lon] : initCenter;
    const boat  = L.marker(initPos, { icon: boatIcon, zIndexOffset: 100 }).addTo(map);
    const track = L.polyline([], { color: T.accent, weight: 2, opacity: 0.75 }).addTo(map);
    const home  = homePos
      ? L.marker([homePos.lat, homePos.lng], { icon: homeIcon }).addTo(map)
      : null;

    lfRef.current = { map, boat, track, home, homeIcon };

    return () => { map.remove(); lfRef.current = null; };
  }, []);

  // ── Update boat position + heading on each telemetry tick ─────────────────
  useEffect(() => {
    const L = window.L;
    const lf = lfRef.current;
    if (!L || !lf) return;

    // Always update icon color to reflect current GPS staleness
    lf.boat.setIcon(makeBoatIcon(L, d.hdg, stale.gps));

    // Only update position and track when GPS is fresh
    if (!d.lat || !d.lon || stale.gps) return;

    const pos = [d.lat, d.lon];
    lf.boat.setLatLng(pos);

    trackPtsRef.current.push(pos);
    if (trackPtsRef.current.length > 500) trackPtsRef.current.shift();
    lf.track.setLatLngs(trackPtsRef.current);
  }, [d.lat, d.lon, d.hdg, stale.gps]);

  // ── Update home marker ────────────────────────────────────────────────────
  useEffect(() => {
    const L = window.L;
    const lf = lfRef.current;
    if (!L || !lf) return;

    if (!homePos) {
      if (lf.home) { lf.home.remove(); lf.home = null; }
      return;
    }
    const p = [homePos.lat, homePos.lng];
    if (lf.home) {
      lf.home.setLatLng(p);
    } else {
      lf.home = L.marker(p, { icon: lf.homeIcon }).addTo(lf.map);
    }
  }, [homePos]);

  const lbl = (x) => ({ fontFamily:_MONO, fontSize:8.5, fontWeight:600,
    letterSpacing:'0.13em', textTransform:'uppercase', color:T.dim, ...(x||{}) });
  const val = (s,c) => ({ fontFamily:_MONO, fontWeight:700, fontSize:s||14,
    color:c||T.text, fontFeatureSettings:'"tnum","zero"', lineHeight:1 });

  return (
    <div style={{ display:'flex', flexDirection:'column' }}>

      {/* ── Position readouts ─────────────────────────────────────────────── */}
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

      {/* ── Leaflet map (or not-ready message) ───────────────────────────── */}
      {!window.L ? (
        <div style={{ height:340, display:'flex', flexDirection:'column',
          alignItems:'center', justifyContent:'center', gap:10,
          background:T.inset, borderTop:`1px solid ${T.border}`,
          borderBottom:`1px solid ${T.border}` }}>
          <span style={{ fontFamily:_MONO, fontSize:11, fontWeight:700, color:T.warn }}>
            Map not ready
          </span>
          <span style={{ fontFamily:_MONO, fontSize:9, color:T.faint, textAlign:'center',
            maxWidth:260, lineHeight:1.6 }}>
            Run <span style={{ color:T.accent }}>scripts/download_tiles.py --fetch-leaflet</span> on
            the Pi, then restart the server.
          </span>
        </div>
      ) : (
        <div style={{ position:'relative', height:340,
          borderTop:`1px solid ${T.border}`,
          borderBottom:`1px solid ${T.border}` }}>
          <div ref={containerRef}
            style={{ height:'100%',
              background: T.id==='dusk' ? '#0e1f36' : '#d4e4f0' }}/>
          {stale.gps && (
            <div style={{ position:'absolute', top:8, left:0, right:0, zIndex:1000,
              display:'flex', justifyContent:'center', pointerEvents:'none' }}>
              <div style={{ padding:'4px 10px', borderRadius:6,
                background: trackPtsRef.current.length > 0
                  ? 'rgba(243,156,18,0.90)' : 'rgba(192,57,43,0.90)',
                color:'#fff', fontFamily:_MONO, fontSize:9, fontWeight:700,
                letterSpacing:'0.06em' }}>
                {trackPtsRef.current.length > 0 ? 'GPS STALE · LAST KNOWN POSITION' : 'NO GPS FIX'}
              </div>
            </div>
          )}
        </div>
      )}

      {/* ── Actions ──────────────────────────────────────────────────────── */}
      <Card T={T} style={{ padding:'11px 16px', display:'flex', flexDirection:'column', gap:8 }}>
        {flash && (
          <div style={{ textAlign:'center', fontFamily:_MONO, fontSize:10,
            fontWeight:700, color:T.safe, letterSpacing:'0.08em' }}>
            ✓ {flash}
          </div>
        )}
        <div style={{ display:'flex', gap:9 }}>
          <div onClick={() => {
              setHomePos({ lat:d.lat, lng:d.lon });
              flashMsg(d.lat ? 'Home set' : 'Home set (no GPS fix yet)');
            }}
            style={{ flex:1, padding:'11px 8px', borderRadius:8,
              background:T.safe, color:'#fff', textAlign:'center',
              fontFamily:_MONO, fontSize:11, fontWeight:800,
              letterSpacing:'0.06em', cursor:'pointer' }}>
            SET HOME HERE
          </div>
          <div onClick={() => { setHomePos(null); flashMsg('Home cleared'); }}
            style={{ flex:1, padding:'11px 8px', borderRadius:8,
              background:T.inset, border:`1px solid ${T.border}`,
              color:T.text, textAlign:'center',
              fontFamily:_MONO, fontSize:11, fontWeight:700,
              letterSpacing:'0.06em', cursor:'pointer' }}>
            RESET HOME
          </div>
          <div onClick={() => {
              const lf = lfRef.current;
              if (!lf) return;
              const c = (d.lat && d.lon) ? [d.lat, d.lon] : [40.934, -73.071];
              lf.map.setView(c, 15);
            }}
            style={{ flex:1, padding:'11px 8px', borderRadius:8,
              background:T.inset, border:`1px solid ${T.border}`,
              color:T.text, textAlign:'center',
              fontFamily:_MONO, fontSize:11, fontWeight:700,
              letterSpacing:'0.06em', cursor:'pointer' }}>
            CENTRE MAP
          </div>
        </div>
      </Card>

    </div>
  );
};
