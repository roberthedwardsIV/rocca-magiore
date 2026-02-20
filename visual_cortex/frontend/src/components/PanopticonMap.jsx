import React, { useEffect, useState, useMemo, useCallback } from 'react';
import Map, { Source, Layer, Popup } from 'react-map-gl';
import maplibregl from 'maplibre-gl'; 
import { Filter as FilterIcon } from 'lucide-react';
import 'maplibre-gl/dist/maplibre-gl.css';

const MAP_STYLE = "https://basemaps.cartocdn.com/gl/dark-matter-gl-style/style.json";

// =====================================================================
// SVG ICON GENERATOR (Raw strings for the WebGL Sprite Sheet)
// =====================================================================
const createSvgIcon = (path, color, isStroke = false) => {
  const fill = isStroke ? "none" : color;
  const stroke = isStroke ? color : "none";
  const strokeW = isStroke ? 'stroke-width="2" stroke-linecap="round" stroke-linejoin="round"' : '';
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="${fill}" stroke="${stroke}" ${strokeW}>${path}</svg>`;
};

const ICONS = {
  plane: (c) => createSvgIcon(`<path d="M17.8 19.2 16 11l3.5-3.5C21 6 21.5 4 21.5 4c0 0-2 .5-3.5 2L14.5 9.5 6.3 7.7l-1.6 1.6 6 4-4 4-2.8-1.2-1.4 1.4 3.7 2.1 2.1 3.7 1.4-1.4-1.2-2.8 4-4 4 6 1.6-1.6z"/>`, c),
  ship: (c) => createSvgIcon(`<path d="M2 12h20l-2 8H4Z"/><path d="M6 12V4h12v8"/>`, c),
  mine: (c) => createSvgIcon(`<path d="M12 2L2 22h20L12 2z"/>`, c), // Triangle
  refinery: (c) => createSvgIcon(`<path d="M2 20a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2V8l-7 5V8l-7 5V4H2v16Z"/>`, c), // Factory
  port: (c) => createSvgIcon(`<circle cx="12" cy="5" r="3"/><line x1="12" y1="22" x2="12" y2="8"/><path d="M5 12H2a10 10 0 0 0 20 0h-3"/>`, c, true), // Anchor
  power: (c) => createSvgIcon(`<path d="M13 2L3 14h9l-1 8 10-12h-9l1-8z"/>`, c), // Zap
  distribution: (c) => createSvgIcon(`<path d="M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z"/><polyline points="3.27 6.96 12 12.01 20.73 6.96"/><line x1="12" y1="22.08" x2="12" y2="12"/>`, c, true), // Package
  chokepoint: (c) => createSvgIcon(`<path d="m21.73 18-8-14a2 2 0 0 0-3.48 0l-8 14A2 2 0 0 0 4 21h16a2 2 0 0 0 1.73-3Z"/><line x1="12" y1="9" x2="12" y2="13"/><line x1="12" y1="17" x2="12.01" y2="17"/>`, c, true) // Alert Triangle
};

// =====================================================================
// MAIN MAP COMPONENT
// =====================================================================
export default function PanopticonMap({ assets, hubs, lines, chokepoints, onFetchRequest }) { 
  const [planes, setPlanes] = useState({});
  const [ships, setShips] = useState({});
  const [events, setEvents] = useState([]);
  
  const [viewState, setViewState] = useState({ longitude: -98.0, latitude: 38.0, zoom: 4 });
  const [hoverInfo, setHoverInfo] = useState(null);
  const [mapRef, setMapRef] = useState(null);
  const [showFilters, setShowFilters] = useState(true);
  const [iconsLoaded, setIconsLoaded] = useState(false);

  // --- DYNAMIC FILTER STATE ---
  const [filters, setFilters] = useState({
    mines: true, refineries: true, smelters: true, ports: true, power_plants: true,
    distribution: true, rail: true, road: true, pipeline: true, power_grid: true,
    chokepoints: true, ships: true, planes: true, events: true
  });

  const toggleFilter = (key) => setFilters(prev => ({ ...prev, [key]: !prev[key] }));

  // --- WEBGL SPRITE INJECTION ---
  const loadMapImages = async (map) => {
    const iconConfigs = [
      // Format: { id: 'icon_name', svg: Raw_SVG_String }
      { id: 'plane', svg: ICONS.plane('#ffffff') },
      { id: 'ship', svg: ICONS.ship('#3b82f6') },
      { id: 'mine-healthy', svg: ICONS.mine('#22d3ee') },
      { id: 'mine-critical', svg: ICONS.mine('#ef4444') },
      { id: 'refinery-healthy', svg: ICONS.refinery('#22d3ee') },
      { id: 'refinery-critical', svg: ICONS.refinery('#ef4444') },
      { id: 'smelter-healthy', svg: ICONS.power('#22d3ee') },
      { id: 'smelter-critical', svg: ICONS.power('#ef4444') },
      { id: 'port', svg: ICONS.port('#a855f7') },
      { id: 'power_plant', svg: ICONS.power('#a855f7') },
      { id: 'distribution', svg: ICONS.distribution('#a855f7') },
      { id: 'choke-healthy', svg: ICONS.chokepoint('#f97316') },
      { id: 'choke-critical', svg: ICONS.chokepoint('#ef4444') }
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

  // Initial Fetch on Load
  useEffect(() => {
    if (mapRef) onFetchRequest(mapRef.getBounds(), viewState.zoom);
  }, [mapRef]);

  // WebSocket Stream
  useEffect(() => {
    const handleStream = (e) => {
      const { channel, payload } = e.detail;
      if (channel === 'global_sky') setPlanes(prev => ({ ...prev, [payload.icao]: payload })); 
      if (channel === 'maritime_ais') setShips(prev => ({ ...prev, [payload.mmsi]: payload }));
      if (channel === 'raw_signals' && ['earthquake', 'wildfire'].includes(payload.entity_type)) {
        setEvents(prev => {
          if (prev.find(ev => ev.entity_id === payload.entity_id)) return prev;
          return [...prev, payload].slice(-20);
        });
      }
    };
    window.addEventListener('stream-event', handleStream);
    return () => window.removeEventListener('stream-event', handleStream);
  }, []);

  // --- GPU-OPTIMIZED GEOJSON GENERATORS ---
  const linesGeoJson = useMemo(() => {
    if (!lines) return null;
    const filtered = lines.filter(l => {
      const t = l.type || '';
      if (t.includes('rail') && !filters.rail) return false;
      if ((t.includes('highway') || t === 'road') && !filters.road) return false;
      if (t.includes('pipeline') && !filters.pipeline) return false;
      if (t.includes('power') && !filters.power_grid) return false;
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
        properties: { id: a.id, name: a.name, type: a.type, category: 'Asset', icon: icon + suffix }
      });
    });

    (hubs || []).forEach(h => {
      const t = (h.type || '').toLowerCase();
      let icon = 'distribution';
      if (t.includes('port')) { icon = 'port'; if (!filters.ports) return; }
      else if (t.includes('power_plant')) { icon = 'power_plant'; if (!filters.power_plants) return; }
      else if (!filters.distribution) return;

      features.push({
        type: 'Feature', geometry: { type: 'Point', coordinates: [h.lon, h.lat] },
        properties: { id: h.id, name: h.name, type: h.type, category: 'Hub', icon: icon }
      });
    });

    if (filters.chokepoints) {
      (chokepoints || []).forEach(cp => {
        const suffix = cp.structural_health < 1.0 ? '-critical' : '-healthy';
        features.push({
          type: 'Feature', geometry: { type: 'Point', coordinates: [cp.lon, cp.lat] },
          properties: { id: cp.id, name: cp.name, type: cp.type, category: 'Chokepoint', icon: 'choke' + suffix }
        });
      });
    }

    return { type: 'FeatureCollection', features };
  }, [assets, hubs, chokepoints, filters]);

  const vehiclesGeoJson = useMemo(() => {
    let features = [];
    if (filters.planes) {
      features.push(...Object.values(planes).map(p => ({
        type: 'Feature', geometry: { type: 'Point', coordinates: [p.lon, p.lat] },
        properties: { type: 'plane', id: p.icao, icon: 'plane', heading: p.heading || 0 }
      })));
    }
    if (filters.ships) {
      features.push(...Object.values(ships).map(s => ({
        type: 'Feature', geometry: { type: 'Point', coordinates: [s.lon, s.lat] },
        properties: { type: 'ship', id: s.mmsi, icon: 'ship', heading: s.heading || 0 }
      })));
    }
    return { type: 'FeatureCollection', features };
  }, [planes, ships, filters]);

  const eventsGeoJson = useMemo(() => {
    if (!filters.events) return { type: 'FeatureCollection', features: [] };
    return {
      type: 'FeatureCollection',
      features: events.map(ev => ({
        type: 'Feature', geometry: { type: 'Point', coordinates: [ev.data.lon, ev.data.lat] },
        properties: { type: ev.entity_type, magnitude: ev.entity_type === 'earthquake' ? ev.data.mag : (ev.data.frp / 20), color: ev.entity_type === 'earthquake' ? '#f97316' : '#ef4444' }
      }))
    };
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

  return (
    <div className="relative w-full h-full">
      <Map
        {...viewState}
        onMove={evt => setViewState(evt.viewState)}
        onMoveEnd={evt => onFetchRequest(evt.target.getBounds(), evt.viewState.zoom)}
        onMouseMove={onInteractiveHover}
        onLoad={handleMapLoad}
        interactiveLayerIds={['static-points-layer', 'vehicles-layer']} 
        ref={(ref) => setMapRef(ref && ref.getMap())}
        style={{width: '100%', height: '100%'}}
        mapStyle={MAP_STYLE}
        mapLib={maplibregl}
      >
        {/* PIPELINES AND ROADS */}
        {linesGeoJson && (
          <Source id="supply-lines" type="geojson" data={linesGeoJson}>
            <Layer id="line-layer" type="line" paint={{
                'line-color': ['match', ['get', 'type'], 'rail_mainline', '#f59e0b', 'rail', '#f59e0b', 'pipeline', '#10b981', 'shipping_lane', '#3b82f6', 'highway_trunk', '#ef4444', 'road', '#ef4444', 'power_grid', '#e879f9', 'power', '#e879f9', '#555'],
                'line-width': 1.5, 'line-opacity': 0.5
              }} />
          </Source>
        )}

        {/* EARTHQUAKES AND WILDFIRES (Pulses) */}
        {eventsGeoJson && (
          <Source id="events-source" type="geojson" data={eventsGeoJson}>
            <Layer id="events-pulse" type="circle" paint={{
                'circle-radius': ['interpolate', ['linear'], ['zoom'], 1, ['*', ['get', 'magnitude'], 2], 10, ['*', ['get', 'magnitude'], 15]],
                'circle-color': ['get', 'color'], 'circle-opacity': 0.4, 'circle-stroke-width': 1, 'circle-stroke-color': ['get', 'color']
              }} />
          </Source>
        )}

        {/* --- GPU SYMBOL LAYERS (Replaces Circles) --- */}
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
                'icon-size': 0.5,
                'icon-allow-overlap': true,
                'icon-rotate': ['get', 'heading'], // Vehicles physically turn on the map
                'icon-rotation-alignment': 'map'
              }} />
            </Source>
          </>
        )}

        {/* TOOLTIP */}
        {hoverInfo && (
          <Popup longitude={hoverInfo.longitude} latitude={hoverInfo.latitude} offset={10} closeButton={false} closeOnClick={false} anchor="top" className="z-50 pointer-events-none">
            <div className="text-slate-100 px-1">
              <div className="font-bold text-[10px] text-cyan-400 mb-0.5 uppercase tracking-wider">{hoverInfo.category}</div>
              <div className="font-semibold text-white text-sm">{hoverInfo.name}</div>
              <div className="italic text-slate-400 mt-0.5 text-xs">{hoverInfo.type}</div>
            </div>
          </Popup>
        )}
      </Map>

      {/* --- FLOATING FILTER PANEL --- */}
      <div className="absolute top-4 right-4 z-40 flex flex-col items-end">
        <button onClick={() => setShowFilters(!showFilters)} className="bg-void/90 border border-gray-700 p-2 rounded text-cyan-400 hover:bg-gray-800 transition-colors shadow-lg backdrop-blur flex items-center gap-2">
          <FilterIcon size={16} /><span className="text-xs font-bold">LAYERS</span>
        </button>

        {showFilters && (
          <div className="mt-2 bg-void/90 border border-gray-700 rounded p-3 shadow-xl backdrop-blur-md w-48 text-xs text-gray-300">
            <div className="mb-2 border-b border-gray-700 pb-1 font-bold text-cyan-500">ASSETS</div>
            {['mines', 'refineries', 'smelters'].map(k => (
              <label key={k} className="flex items-center justify-between mb-1 cursor-pointer hover:text-white">
                <span className="capitalize">{k}</span>
                <input type="checkbox" checked={filters[k]} onChange={() => toggleFilter(k)} className="accent-cyan-500" />
              </label>
            ))}

            <div className="mt-3 mb-2 border-b border-gray-700 pb-1 font-bold text-purple-400">HUBS</div>
            {['ports', 'power_plants', 'distribution'].map(k => (
              <label key={k} className="flex items-center justify-between mb-1 cursor-pointer hover:text-white">
                <span className="capitalize">{k.replace('_', ' ')}</span>
                <input type="checkbox" checked={filters[k]} onChange={() => toggleFilter(k)} className="accent-purple-500" />
              </label>
            ))}

            <div className="mt-3 mb-2 border-b border-gray-700 pb-1 font-bold text-yellow-500">INFRASTRUCTURE</div>
            {['rail', 'road', 'pipeline', 'power_grid', 'chokepoints'].map(k => (
              <label key={k} className="flex items-center justify-between mb-1 cursor-pointer hover:text-white">
                <span className="capitalize">{k.replace('_', ' ')}</span>
                <input type="checkbox" checked={filters[k]} onChange={() => toggleFilter(k)} className="accent-yellow-500" />
              </label>
            ))}

            <div className="mt-3 mb-2 border-b border-gray-700 pb-1 font-bold text-red-400">DYNAMIC</div>
            {['ships', 'planes', 'events'].map(k => (
              <label key={k} className="flex items-center justify-between mb-1 cursor-pointer hover:text-white">
                <span className="capitalize">{k}</span>
                <input type="checkbox" checked={filters[k]} onChange={() => toggleFilter(k)} className="accent-red-500" />
              </label>
            ))}
          </div>
        )}
      </div>
    </div>
  );
}