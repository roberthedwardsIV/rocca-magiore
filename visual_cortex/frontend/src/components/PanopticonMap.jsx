import React, { useEffect, useState, useMemo, useCallback } from 'react';
import Map, { Source, Layer, Popup } from 'react-map-gl';
import maplibregl from 'maplibre-gl'; 
import 'maplibre-gl/dist/maplibre-gl.css';

const MAP_STYLE = "https://basemaps.cartocdn.com/gl/dark-matter-gl-style/style.json";

// =====================================================================
// TERMINAL SVG ICONS (Raw strings for WebGL Sprite Sheet)
// =====================================================================
const createSvgIcon = (path, color, isStroke = false) => {
  const fill = isStroke ? "none" : color;
  const stroke = isStroke ? color : "none";
  const strokeW = isStroke ? 'stroke-width="2" stroke-linecap="square" stroke-linejoin="miter"' : '';
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="${fill}" stroke="${stroke}" ${strokeW}>${path}</svg>`;
};

const ICONS = {
  plane: (c) => createSvgIcon(`<path d="M17.8 19.2 16 11l3.5-3.5C21 6 21.5 4 21.5 4c0 0-2 .5-3.5 2L14.5 9.5 6.3 7.7l-1.6 1.6 6 4-4 4-2.8-1.2-1.4 1.4 3.7 2.1 2.1 3.7 1.4-1.4-1.2-2.8 4-4 4 6 1.6-1.6z"/>`, c),
  ship: (c) => createSvgIcon(`<path d="M2 12h20l-2 8H4Z"/><path d="M6 12V4h12v8"/>`, c),
  mine: (c) => createSvgIcon(`<path d="M12 2L2 22h20L12 2z"/>`, c),
  refinery: (c) => createSvgIcon(`<path d="M2 20a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2V8l-7 5V8l-7 5V4H2v16Z"/>`, c),
  port: (c) => createSvgIcon(`<circle cx="12" cy="5" r="3"/><line x1="12" y1="22" x2="12" y2="8"/><path d="M5 12H2a10 10 0 0 0 20 0h-3"/>`, c, true),
  power: (c) => createSvgIcon(`<path d="M13 2L3 14h9l-1 8 10-12h-9l1-8z"/>`, c),
  distribution: (c) => createSvgIcon(`<path d="M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z"/><polyline points="3.27 6.96 12 12.01 20.73 6.96"/><line x1="12" y1="22.08" x2="12" y2="12"/>`, c, true),
  airport: (c) => createSvgIcon(`<path d="M21 16v-2l-8-5V3.5c0-.83-.67-1.5-1.5-1.5S10 2.67 10 3.5V9l-8 5v2l8-2.5V19l-2 1.5V22l3.5-1 3.5 1v-1.5L13 19v-5.5l8 2.5z"/>`, c),
  energy: (c) => createSvgIcon(`<path d="M2 12V7c0-3 4-4 10-4s10 1 10 4v5"/><path d="M2 17v-5"/><path d="M22 17v-5"/><path d="M2 17c0 3 4 4 10 4s10-1 10-4"/>`, c, true),
  chokepoint: (c) => createSvgIcon(`<path d="m21.73 18-8-14a2 2 0 0 0-3.48 0l-8 14A2 2 0 0 0 4 21h16a2 2 0 0 0 1.73-3Z"/><line x1="12" y1="9" x2="12" y2="13"/><line x1="12" y1="17" x2="12.01" y2="17"/>`, c, true)
};

// =====================================================================
// MAIN MAP COMPONENT
// =====================================================================
// --- ADDED onSelect and selectedId PROPS ---
export default function PanopticonMap({ assets, hubs, lines, chokepoints, onFetchRequest, onSelect, selectedId }) { 
  const [planes, setPlanes] = useState({});
  const [ships, setShips] = useState({});
  const [events, setEvents] = useState([]);
  
  const [viewState, setViewState] = useState({ longitude: -98.0, latitude: 38.0, zoom: 4 });
  const [hoverInfo, setHoverInfo] = useState(null);
  const [mapRef, setMapRef] = useState(null);
  const [iconsLoaded, setIconsLoaded] = useState(false);

  // --- STRICT TERMINAL TOGGLE STATE ---
  const [filters, setFilters] = useState({
    mines: true, refineries: true, smelters: true,
    ports: true, airports: true, rail_nodes: true, distribution: true, power_plants: true, energy_terminals: true,
    roads: true, rail: true, maritime: true, pipelines: true, power_grid: true,
    chokepoints: true, planes: true, ships: true, earthquakes: true, wildfires: true
  });

  const toggleFilter = (key) => setFilters(prev => ({ ...prev, [key]: !prev[key] }));

  // --- WEBGL SPRITE INJECTION ---
  const loadMapImages = async (map) => {
    const iconConfigs = [
      // DYNAMIC -> Red (#ff0000)
      { id: 'plane', svg: ICONS.plane('#ff0000') },
      { id: 'ship', svg: ICONS.ship('#ff0000') },
      
      // ASSETS -> Cyan (#00f2ea)
      { id: 'mine-healthy', svg: ICONS.mine('#00f2ea') },
      { id: 'mine-critical', svg: ICONS.mine('#ff0000') },
      { id: 'refinery-healthy', svg: ICONS.refinery('#00f2ea') },
      { id: 'refinery-critical', svg: ICONS.refinery('#ff0000') },
      { id: 'smelter-healthy', svg: ICONS.power('#00f2ea') },
      { id: 'smelter-critical', svg: ICONS.power('#ff0000') },
      
      // HUBS -> Amber (#ffbf00)
      { id: 'port', svg: ICONS.port('#ffbf00') },
      { id: 'airport', svg: ICONS.airport('#ffbf00') },
      { id: 'rail_node', svg: ICONS.distribution('#ffbf00') },
      { id: 'power_plant', svg: ICONS.power('#ffbf00') },
      { id: 'distribution', svg: ICONS.distribution('#ffbf00') },
      { id: 'energy_terminal', svg: ICONS.energy('#ffbf00') },
      
      // CHOKEPOINTS -> Green (#00ff00)
      { id: 'choke-healthy', svg: ICONS.chokepoint('#00ff00') },
      { id: 'choke-critical', svg: ICONS.chokepoint('#ff0000') }
    ];

    for (const config of iconConfigs) {
      if (!map.hasImage(config.id)) {
        const img = new Image();
        img.src = `data:image/svg+xml;charset=utf-8,${encodeURIComponent(config.svg)}`;
        await new Promise(res => img.onload = res);
        map.addImage(config.id, img);
      }
    }
    setIconsLoaded(true);
  };

  const handleMapLoad = useCallback((evt) => {
    const map = evt.target;
    loadMapImages(map);
  }, []);

  useEffect(() => {
    if (mapRef) onFetchRequest(mapRef.getBounds(), viewState.zoom);
  }, [mapRef]);

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

  // --- GPU-OPTIMIZED GEOJSON GENERATORS ---
  const linesGeoJson = useMemo(() => {
    if (!lines) return null;
    const filtered = lines.filter(l => {
      const t = (l.type || '').toLowerCase();
      if ((t.includes('highway') || t === 'road') && !filters.roads) return false;
      if (t.includes('rail') && !filters.rail) return false;
      if (t.includes('pipeline') && !filters.pipelines) return false;
      if (t.includes('power') && !filters.power_grid) return false;
      if ((t.includes('maritime') || t.includes('shipping')) && !filters.maritime) return false;
      return true;
    });

    return {
      type: 'FeatureCollection',
      features: filtered.map(l => ({
        type: 'Feature',
        geometry: typeof l.geojson === 'string' ? JSON.parse(l.geojson) : l.geojson,
        properties: { type: l.type, name: l.name }
      }))
    };
  }, [lines, filters]);

  const pointEntitiesGeoJson = useMemo(() => {
    const features = [];
    
    (assets || []).forEach(a => {
      const t = (a.type || '').toLowerCase();
      let icon = 'mine';
      if (t.includes('mine')) { if (!filters.mines) return; }
      else if (t.includes('refinery')) { icon = 'refinery'; if (!filters.refineries) return; }
      else if (t.includes('smelter')) { icon = 'smelter'; if (!filters.smelters) return; }

      const suffix = a.op_health < 0.5 ? '-critical' : '-healthy';
      features.push({
        type: 'Feature', geometry: { type: 'Point', coordinates: [a.lon, a.lat] },
        properties: { id: a.id, name: a.name, type: a.type, category: 'ASSET', icon: icon + suffix }
      });
    });

    (hubs || []).forEach(h => {
      const t = (h.type || '').toLowerCase();
      let icon = 'distribution';
      if (t.includes('port')) { icon = 'port'; if (!filters.ports) return; }
      else if (t.includes('airport')) { icon = 'airport'; if (!filters.airports) return; }
      else if (t.includes('rail_node')) { icon = 'rail_node'; if (!filters.rail_nodes) return; }
      else if (t.includes('power_plant')) { icon = 'power_plant'; if (!filters.power_plants) return; }
      else if (t.includes('energy_terminal')) { icon = 'energy_terminal'; if (!filters.energy_terminals) return; }
      else if (!filters.distribution) return;

      features.push({
        type: 'Feature', geometry: { type: 'Point', coordinates: [h.lon, h.lat] },
        properties: { id: h.id, name: h.name, type: h.type, category: 'HUB', icon: icon }
      });
    });

    if (filters.chokepoints) {
      (chokepoints || []).forEach(cp => {
        const suffix = cp.structural_health < 1.0 ? '-critical' : '-healthy';
        features.push({
          type: 'Feature', geometry: { type: 'Point', coordinates: [cp.lon, cp.lat] },
          properties: { id: cp.id, name: cp.name, type: cp.type, category: 'CHOKEPOINT', icon: 'choke' + suffix }
        });
      });
    }

    return { type: 'FeatureCollection', features };
  }, [assets, hubs, chokepoints, filters]);

  const vehiclesGeoJson = useMemo(() => {
    let features = [];
    if (filters.planes) {
      features.push(...Object.values(planes).slice(0, 400).map(p => ({
        type: 'Feature', geometry: { type: 'Point', coordinates: [p.lon, p.lat] },
        properties: { type: 'plane', id: p.icao, icon: 'plane', heading: p.heading || 0 }
      })));
    }
    if (filters.ships) {
      features.push(...Object.values(ships).slice(0, 400).map(s => ({
        type: 'Feature', geometry: { type: 'Point', coordinates: [s.lon, s.lat] },
        properties: { type: 'ship', id: s.mmsi, icon: 'ship', heading: s.heading || 0 }
      })));
    }
    return { type: 'FeatureCollection', features };
  }, [planes, ships, filters]);

  const eventsGeoJson = useMemo(() => {
    const features = [];
    events.forEach(ev => {
      if (ev.entity_type === 'earthquake' && !filters.earthquakes) return;
      if (ev.entity_type === 'wildfire' && !filters.wildfires) return;
      
      features.push({
        type: 'Feature', geometry: { type: 'Point', coordinates: [ev.data.lon, ev.data.lat] },
        properties: { 
          type: ev.entity_type, 
          magnitude: ev.entity_type === 'earthquake' ? ev.data.mag : (ev.data.frp / 20), 
          color: '#ff0000' // All Dynamic Events map to Red
        }
      });
    });
    return { type: 'FeatureCollection', features };
  }, [events, filters]);

  const onInteractiveHover = useCallback(event => {
    const { features, lngLat } = event;
    const hoveredFeature = features && features[0];
    if (hoveredFeature) {
      setHoverInfo({
        longitude: lngLat.lng, latitude: lngLat.lat,
        name: hoveredFeature.properties.name || hoveredFeature.properties.id,
        category: hoveredFeature.properties.category || hoveredFeature.properties.type,
        type: hoveredFeature.properties.type
      });
    } else setHoverInfo(null);
  }, []);

  // --- ADDED: TARGET LOCK CLICK HANDLER ---
  const onMapClick = useCallback((event) => {
    if (!onSelect) return;
    const { features } = event;
    const clickedFeature = features && features[0];
    
    if (clickedFeature) {
      // Find the full database object matching the clicked feature's ID
      const entity = [...(assets || []), ...(hubs || []), ...(chokepoints || [])]
        .find(e => e.id == clickedFeature.properties.id);
      
      if (entity) {
        onSelect(entity);
      } else {
        onSelect(null);
      }
    } else {
      onSelect(null);
    }
  }, [assets, hubs, chokepoints, onSelect]);

  // --- ADDED: CAMERA FLIGHT LOGIC ---
  useEffect(() => {
    if (mapRef && selectedId) {
      const entity = [...(assets || []), ...(hubs || []), ...(chokepoints || [])]
        .find(e => e.id == selectedId);
      
      if (entity) {
        mapRef.flyTo({
          center: [entity.lon, entity.lat],
          zoom: 12,
          speed: 1.2,
          curve: 1.4,
          essential: true
        });
      }
    }
  }, [selectedId, mapRef, assets, hubs, chokepoints]);

  // --- THE FIX: Inlined toggle rendering helper ---
  // This prevents React from unmounting and destroying the click listener on every socket render
  const renderToggle = (label, stateKey) => (
    <div 
      key={stateKey}
      className="flex items-center justify-between cursor-pointer hover:text-white hover:bg-[#222] px-1 py-[2px] transition-colors"
      onPointerDown={(e) => e.stopPropagation()} // Prevent map pan initiation
      onClick={(e) => {
        e.stopPropagation(); // Stop click from bleeding through to Map
        toggleFilter(stateKey);
      }}
    >
      <span className="uppercase">{label}</span>
      <span className="text-term_cyan font-bold text-[11px]">[{filters[stateKey] ? 'X' : ' '}]</span>
    </div>
  );

  return (
    <div className="relative w-full h-full bg-term_black">
      <Map
        {...viewState}
        onMove={evt => setViewState(evt.viewState)}
        onMoveEnd={evt => onFetchRequest(evt.target.getBounds(), evt.viewState.zoom)}
        onMouseMove={onInteractiveHover}
        onClick={onMapClick}          
        onLoad={handleMapLoad}
        interactiveLayerIds={['static-points-layer']} 
        ref={(ref) => setMapRef(ref && ref.getMap())}
        style={{width: '100%', height: '100%'}}
        mapStyle={MAP_STYLE}
        mapLib={maplibregl}
      >
        {/* ROUTES */}
        {linesGeoJson && (
          <Source id="supply-lines" type="geojson" data={linesGeoJson}>
            <Layer id="line-layer" type="line" paint={{
                'line-color': [
                  'match', ['get', 'type'], 
                  // Routes = Purple shades. Roads = Dim Gray.
                  'rail_mainline', '#c084fc', 'rail', '#c084fc', 
                  'pipeline', '#a855f7', 
                  'shipping_lane', '#e879f9', 'maritime_route', '#e879f9',
                  'power_grid', '#9333ea', 'power', '#9333ea', 
                  'highway_trunk', '#444444', 'road', '#444444', 
                  '#333333'
                ],
                'line-width': 1.5, 'line-opacity': 0.7
              }} />
          </Source>
        )}

        {/* EVENTS (Pulses) */}
        {eventsGeoJson && (
          <Source id="events-source" type="geojson" data={eventsGeoJson}>
            <Layer id="events-pulse" type="circle" paint={{
                'circle-radius': ['interpolate', ['linear'], ['zoom'], 1, ['*', ['get', 'magnitude'], 2], 10, ['*', ['get', 'magnitude'], 15]],
                'circle-color': ['get', 'color'], 'circle-opacity': 0.4, 'circle-stroke-width': 1, 'circle-stroke-color': ['get', 'color']
              }} />
          </Source>
        )}

        {/* SYMBOLS (Assets, Hubs, Chokes, Vehicles) */}
        {iconsLoaded && (
          <>
            <Source id="static-points-source" type="geojson" data={pointEntitiesGeoJson}>
              <Layer id="static-points-layer" type="symbol" layout={{
                'icon-image': ['get', 'icon'],
                'icon-size': 0.05,
                'icon-allow-overlap': true
              }} />
            </Source>

            <Source id="vehicles-source" type="geojson" data={vehiclesGeoJson}>
              <Layer id="vehicles-layer" type="symbol" layout={{
                'icon-image': ['get', 'icon'],
                'icon-size': 0.05,
                'icon-allow-overlap': true,
                'icon-rotate': ['get', 'heading'], 
                'icon-rotation-alignment': 'map'
              }} />
            </Source>
          </>
        )}

        {/* TERMINAL TOOLTIP */}
        {/* THE FIX: Number() coercion prevents TypeError if API returns coordinates as strings */}
        {hoverInfo && (
          <Popup longitude={hoverInfo.longitude} latitude={hoverInfo.latitude} offset={10} closeButton={false} closeOnClick={false} anchor="top" className="z-50 pointer-events-none">
            <div className="text-gray-300">
              <div className="font-bold text-[10px] text-term_cyan mb-[2px] uppercase tracking-widest border-b border-term_border pb-[2px]">
                 {hoverInfo.category || 'UNKNOWN'}
              </div>
              <div className="font-bold text-white text-xs mt-[2px]">{hoverInfo.name || 'UNNAMED_ENTITY'}</div>
              <div className="text-[10px] text-gray-500 uppercase mt-[2px]">TYPE: {hoverInfo.type || 'N/A'}</div>
              <div className="text-[10px] text-gray-500 uppercase">LAT: {Number(hoverInfo.latitude).toFixed(4)}</div>
              <div className="text-[10px] text-gray-500 uppercase">LON: {Number(hoverInfo.longitude).toFixed(4)}</div>
            </div>
          </Popup>
        )}
      </Map>

      {/* --- TERMINAL FILTER PANEL --- */}
      <div className="absolute top-2 right-2 z-40 bg-term_black/90 border border-term_border p-2 shadow-2xl w-56 text-[10px] text-gray-400 select-none backdrop-blur-sm pointer-events-auto">
        
        <div className="mb-1 border-b border-term_border pb-1 font-bold text-term_cyan tracking-widest"> ASSETS</div>
        {renderToggle('Mines', 'mines')}
        {renderToggle('Refineries', 'refineries')}
        {renderToggle('Smelters', 'smelters')}

        <div className="mt-2 mb-1 border-b border-term_border pb-1 font-bold text-term_amber tracking-widest"> HUBS</div>
        {renderToggle('Ports', 'ports')}
        {renderToggle('Airports', 'airports')}
        {renderToggle('Rail Nodes', 'rail_nodes')}
        {renderToggle('Dist. Centers', 'distribution')}
        {renderToggle('Power Plants', 'power_plants')}
        {renderToggle('Energy Terms', 'energy_terminals')}

        <div className="mt-2 mb-1 border-b border-term_border pb-1 font-bold text-[#a855f7] tracking-widest"> ROUTES</div>
        {renderToggle('Roads', 'roads')}
        {renderToggle('Railways', 'rail')}
        {renderToggle('Maritime', 'maritime')}
        {renderToggle('Pipelines', 'pipelines')}
        {renderToggle('Power Grid', 'power_grid')}

        <div className="mt-2 mb-1 border-b border-term_border pb-1 font-bold text-term_green tracking-widest"> CHOKEPOINTS</div>
        {renderToggle('All Chokepoints', 'chokepoints')}

        <div className="mt-2 mb-1 border-b border-term_border pb-1 font-bold text-term_red tracking-widest"> DYNAMIC</div>
        {renderToggle('Planes', 'planes')}
        {renderToggle('Ships', 'ships')}
        {renderToggle('Earthquakes', 'earthquakes')}
        {renderToggle('Wildfires', 'wildfires')}

      </div>
    </div>
  );
}