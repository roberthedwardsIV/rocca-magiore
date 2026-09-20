import React, { useEffect, useState, useMemo, useCallback } from 'react';
import DeckGL from '@deck.gl/react';
import { _GlobeView as GlobeView, LightingEffect, AmbientLight, DirectionalLight, FlyToInterpolator } from '@deck.gl/core';
import { GeoJsonLayer, ArcLayer, ColumnLayer, ScatterplotLayer } from '@deck.gl/layers';

// Lighting
const ambientLight = new AmbientLight({ color: [255, 255, 255], intensity: 1.0 });
const dirLight = new DirectionalLight({
  color: [255, 255, 255],
  intensity: 2.0,
  direction: [-3, -9, -1]
});
const lightingEffect = new LightingEffect({ ambientLight, dirLight });

const INITIAL_VIEW_STATE = {
  longitude: -40.0,
  latitude: 30.0,
  zoom: 1.5,
  maxZoom: 14,
  pitch: 45,
};

const REGIONS = ['ALL', 'AMERICAS', 'EMEA', 'APAC'];

export default function PanopticonMap({ assets, hubs, chokepoints, links, onFetchRequest, onSelect, selectedId }) {  
  const [planes, setPlanes] = useState({});
  const [ships, setShips] = useState({});
  const [events, setEvents] = useState([]);
  const [worldBorders, setWorldBorders] = useState(null);
  const [timeParams, setTimeParams] = useState(0);
  const [showArcs, setShowArcs] = useState(true);
  
  // Camera & region filter
  const [viewState, setViewState] = useState(INITIAL_VIEW_STATE);
  const [regionIdx, setRegionIdx] = useState(0);
  const activeRegion = REGIONS[regionIdx];

  const [minDependency, setMinDependency] = useState(0.0);
  const [minConfidence, setMinConfidence] = useState(0.0);
  const [maxLag, setMaxLag] = useState(60);

  // --- ENGINE CLOCK ---
  useEffect(() => {
    let animationFrame;
    const animate = () => {
      setTimeParams(t => (t + 1) % 1000);
      animationFrame = requestAnimationFrame(animate);
    };
    animate();
    return () => cancelAnimationFrame(animationFrame);
  }, []);

  // --- BASEMAP TOPOLOGY FETCH ---
  useEffect(() => {
    fetch('https://raw.githubusercontent.com/johan/world.geo.json/master/countries.geo.json')
      .then(res => res.json())
      .then(data => setWorldBorders(data));
  }, []);

  // --- WEBSOCKET EVENT HOOKS ---
  useEffect(() => {
    const handleStream = (e) => {
      const { channel, payload } = e.detail;
      if (channel === 'global_sky') setPlanes(prev => ({ ...prev, [payload.icao]: payload })); 
      if (channel === 'maritime_ais') setShips(prev => ({ ...prev, [payload.mmsi]: payload }));
      if (channel === 'raw_signals') {
        if (payload.entity_type === 'earthquake' || payload.entity_type === 'wildfire') {
          setEvents(prev => {
            if (prev.find(ev => ev.entity_id === payload.entity_id)) return prev;
            return [...prev, payload].slice(-20);
          });
        }
      }
    };
    window.addEventListener('stream-event', handleStream);
    return () => window.removeEventListener('stream-event', handleStream);
  }, []);

  // Reset camera when HUD selection is cleared
  useEffect(() => {
    if (!selectedId) {
      setViewState(prev => ({
        ...prev,
        longitude: -40.0,
        latitude: 30.0,
        zoom: 1.5,
        pitch: 45,
        transitionDuration: 1500,
        transitionInterpolator: new FlyToInterpolator()
      }));
    }
  }, [selectedId]);

  // Fly-to on pick
  const handleMapClick = (info) => {
    if (!info.object) return;
    const obj = info.object;
    
    let targetLon, targetLat;

    // Check if it's an Arc (calculate midpoint)
    if (obj.origin_lon !== undefined && obj.target_lon !== undefined) {
       targetLon = (obj.origin_lon + obj.target_lon) / 2;
       targetLat = (obj.origin_lat + obj.target_lat) / 2;
    } 
    // Otherwise it's an Asset
    else if (obj.lon !== undefined && obj.lat !== undefined) {
       targetLon = obj.lon;
       targetLat = obj.lat;
    } else {
       return;
    }

    // Execute the camera sweep
    setViewState(prev => ({
      ...prev,
      longitude: targetLon,
      latitude: targetLat,
      zoom: 4.5,
      pitch: 55,
      transitionDuration: 1500,
      transitionInterpolator: new FlyToInterpolator()
    }));

    // Tell App.js to open the HUD
    if (onSelect) onSelect(obj);
  };

  // Trade-arc filters
  const filteredLinks = useMemo(() => {
    if (!links || !showArcs) return [];
    
    return links.filter(l => {
      const dep = l.dependency_weight || 0;
      const conf = l.confidence_score || 0;
      const lag = l.transport_lag_days || 0;
      
      return dep >= minDependency && conf >= minConfidence && lag <= maxLag;
    });
  }, [links, showArcs, minDependency, minConfidence, maxLag]);

  // Regional asset filter
  const filteredAssets = useMemo(() => {
    const all = [...(assets || []), ...(hubs || []), ...(chokepoints || [])];
    if (activeRegion === 'ALL') return all;
    
    return all.filter(a => {
      const lon = a.lon || 0;
      if (activeRegion === 'AMERICAS') return lon < -30;
      if (activeRegion === 'EMEA') return lon >= -30 && lon < 50;
      if (activeRegion === 'APAC') return lon >= 50;
      return true;
    });
  }, [assets, hubs, chokepoints, activeRegion]);


  // --- LAYER GENERATION PIPELINE ---

  // 1. Globe Skin (Deep Radar Ocean + Subtle Cyan Wireframe)
  const earthLayer = new GeoJsonLayer({
    id: 'earth-sphere',
    data: { type: 'Feature', geometry: { type: 'Polygon', coordinates: [[[-180, 90], [180, 90], [180, -90], [-180, -90], [-180, 90]]] } },
    filled: true,
    getFillColor: [5, 10, 18, 255], // Deep ocean blue-black
    stroked: false
  });

  const basemapLayer = new GeoJsonLayer({
    id: 'base-wireframe',
    data: worldBorders || { type: 'FeatureCollection', features: [] }, 
    stroked: true,
    filled: true,
    lineWidthMinPixels: 1,
    getLineColor: [30, 70, 90, 200], // Visible cyan-grey borders
    getFillColor: [10, 15, 22, 255], // Dark landmasses
  });

  // Asset columns (height from sensitivity score)
  const columnsLayer = new ColumnLayer({
    id: 'asset-pillars',
    data: filteredAssets,
    diskResolution: 6,
    radius: 15000,
    extruded: true,
    pickable: true,
    elevationScale: 100,
    getPosition: d => [d.lon || 0, d.lat || 0],
    getFillColor: d => d.id == selectedId ? [255, 191, 0, 255] : [0, 242, 234, 200],
    
    // Elevation scales with total_sensitivity
    getElevation: d => d.total_sensitivity > 0 ? (d.total_sensitivity * 1000000) : 10000, 
    
    onClick: handleMapClick, 
  });

  // 3. Synaptic Network Arcs (Unfiltered)
  const arcsLayer = new ArcLayer({
    id: 'synaptic-routes',
    data: filteredLinks, 
    pickable: true,
    getWidth: 2,
    getSourcePosition: d => [d.origin_lon, d.origin_lat],
    getTargetPosition: d => [d.target_lon, d.target_lat],
    getSourceColor: [168, 85, 247, 200], 
    getTargetColor: [255, 0, 128, 200],  
    getTilt: () => 15,
    onClick: handleMapClick,
  });

  // 4. Dynamic Telemetry (Planes & Ships)
  const dynamicLayer = new ScatterplotLayer({
    id: 'dynamic-telemetry',
    data: [...Object.values(planes).slice(0, 400), ...Object.values(ships).slice(0, 400)],
    pickable: false,
    opacity: 0.8,
    stroked: false,
    filled: true,
    radiusScale: 1000,
    radiusMinPixels: 2,
    radiusMaxPixels: 10,
    getPosition: d => [d.lon || 0, d.lat || 0],
    getFillColor: [255, 191, 0, 200],
  });

  // Shock events (wildfire / earthquake)
  const shockLayer = new ScatterplotLayer({
    id: 'kinetic-shocks',
    data: events,
    pickable: false,
    opacity: 0.5,
    stroked: true,
    filled: true,
    lineWidthMinPixels: 2,
    radiusScale: 1000,
    getPosition: d => [d.data?.lon || 0, d.data?.lat || 0],
    getRadius: d => {
      const base = d.entity_type === 'earthquake' ? (d.data?.mag || 1) * 20 : (d.data?.frp || 1) * 2;
      return base + (Math.sin(timeParams / 10) * 15);
    },
    // Wildfires = Red/Crimson, Earthquakes = Seismic Purple/Magenta
    getFillColor: d => d.entity_type === 'wildfire' ? [255, 30, 0, 100] : [168, 85, 247, 100],
    getLineColor: d => d.entity_type === 'wildfire' ? [255, 30, 0, 255] : [168, 85, 247, 255],
  });

  // --- EVENT HANDLERS ---
  const handleViewStateChange = useCallback(({ viewState }) => {
    setViewState(viewState); // Allow user panning/zooming
    
    const fakeBounds = { getWest: () => -180, getSouth: () => -90, getEast: () => 180, getNorth: () => 90 };
    onFetchRequest(fakeBounds, viewState.zoom);
  }, [onFetchRequest]);

  return (
    <div className="relative w-full h-full bg-term_black">
      <div className="absolute inset-0 scanlines-overlay z-50 pointer-events-none"></div>
      
      <DeckGL
        views={new GlobeView({ resolution: 2 })}
        viewState={viewState} // Now fully controlled
        onViewStateChange={handleViewStateChange}
        controller={true}
        effects={[lightingEffect]}
        layers={[earthLayer, basemapLayer, arcsLayer, columnsLayer, dynamicLayer, shockLayer]} 
        getCursor={() => 'crosshair'}
      />

      {/* --- DIAGNOSTIC OVERLAY (TOP LEFT) --- */}
      <div className="absolute top-4 left-4 z-40 text-[10px] text-term_cyan pointer-events-auto holo-text bg-[#000000]/80 p-2 border border-[#333333]">
        <div>[ORBITAL SYNAPSE ENGINE]</div>
        <div>RENDER_MODE : GLOBE_GL</div>
        <div>V-SYNC_HZ   : {(60 + Math.sin(timeParams/20) * 2).toFixed(1)}</div>
        <div>MEM_ALLOC   : {filteredAssets.length * 64} BYTES</div>
        
        {/* Toggle Arcs */}
        <div 
          className="cursor-pointer hover:text-white mt-1 border-t border-[#333333] pt-1"
          onClick={() => setShowArcs(!showArcs)}
        >
          &gt; TRADE_ARCS : [{showArcs ? 'ON' : 'OFF'}]
        </div>
        
        {/* Cycle Regions */}
        <div 
          className="cursor-pointer hover:text-white mt-1"
          onClick={() => setRegionIdx((i) => (i + 1) % REGIONS.length)}
        >
          &gt; RGN_FILTER : [{activeRegion}]
        </div>

        {/* Arc filter sliders */}
        {showArcs && (
          <div className="mt-3 border-t border-[#333333] pt-2 space-y-2 pointer-events-auto">
            {/* Dependency Slider */}
            <div className="flex flex-col">
              <label className="flex justify-between text-[#ff0080] mb-1">
                <span>MIN_DEPENDENCY:</span>
                <span>{(minDependency * 100).toFixed(0)}%</span>
              </label>
              <input type="range" min="0" max="1" step="0.05" value={minDependency} 
                     onChange={(e) => setMinDependency(parseFloat(e.target.value))} 
                     className="w-full h-1 bg-[#333] rounded-lg appearance-none cursor-pointer accent-[#ff0080]" />
            </div>
            
            {/* Confidence Slider */}
            <div className="flex flex-col">
              <label className="flex justify-between text-[#a855f7] mb-1">
                <span>MIN_CONFIDENCE:</span>
                <span>{(minConfidence * 100).toFixed(0)}%</span>
              </label>
              <input type="range" min="0" max="1" step="0.05" value={minConfidence} 
                     onChange={(e) => setMinConfidence(parseFloat(e.target.value))} 
                     className="w-full h-1 bg-[#333] rounded-lg appearance-none cursor-pointer accent-[#a855f7]" />
            </div>

            {/* Lag Days Slider */}
            <div className="flex flex-col">
              <label className="flex justify-between text-[#00f2ea] mb-1">
                <span>MAX_TRANS_LAG:</span>
                <span>{maxLag} DAYS</span>
              </label>
              <input type="range" min="0" max="60" step="1" value={maxLag} 
                     onChange={(e) => setMaxLag(parseFloat(e.target.value))} 
                     className="w-full h-1 bg-[#333] rounded-lg appearance-none cursor-pointer accent-[#00f2ea]" />
            </div>
          </div>
        )}

        
      </div>
    </div>
  );
}