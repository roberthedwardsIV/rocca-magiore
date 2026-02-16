import React, { useEffect, useState } from 'react';
import Map, { Marker } from 'react-map-gl';
import maplibregl from 'maplibre-gl'; 
import { Triangle, Anchor, Factory, Plane, Ship } from 'lucide-react';
import 'maplibre-gl/dist/maplibre-gl.css';

// Free, open-source dark tiles (CartoDB Dark Matter)
const MAP_STYLE = "https://basemaps.cartocdn.com/gl/dark-matter-gl-style/style.json";

export default function PanopticonMap({ assets }) {
  const [planes, setPlanes] = useState({});
  const [ships, setShips] = useState({});
  const [viewState, setViewState] = useState({
    longitude: -40, latitude: 20, zoom: 1.5
  });

  useEffect(() => {
    const handleStream = (e) => {
      const { channel, payload } = e.detail;
      
      if (channel === 'global_sky') {
        // Flight data update
        setPlanes(prev => ({ ...prev, [payload.icao]: payload })); 
      }
      if (channel === 'maritime_ais') {
        // Ship data update
        setShips(prev => ({ ...prev, [payload.mmsi]: payload }));
      }
    };

    window.addEventListener('stream-event', handleStream);
    return () => window.removeEventListener('stream-event', handleStream);
  }, []);

  return (
    <Map
      {...viewState}
      onMove={evt => setViewState(evt.viewState)}
      style={{width: '100%', height: '100%'}}
      mapStyle={MAP_STYLE}
      mapLib={maplibregl} // Uses the free engine
    >
      {/* STATIC ASSETS */}
      {assets.map((asset) => (
        <Marker key={asset.id} longitude={asset.lon} latitude={asset.lat} anchor="bottom">
          <div className="text-cyan hover:text-white cursor-pointer transition-colors group relative">
            {asset.type === 'mine' && <Triangle size={16} fill="currentColor" />}
            {asset.type === 'port' && <Anchor size={16} />}
            {asset.type === 'refinery' && <Factory size={16} />}
            
            {/* Tooltip */}
            <div className="absolute bottom-6 left-1/2 -translate-x-1/2 w-48 bg-gunmetal border border-cyan/30 p-2 rounded shadow-xl opacity-0 group-hover:opacity-100 pointer-events-none z-50">
              <div className="text-xs font-bold text-cyan">{asset.name}</div>
              <div className="text-[10px] text-gray-400">{asset.commodity_types}</div>
              <div className="text-[10px] mt-1">Health: {(asset.op_health * 100).toFixed(0)}%</div>
            </div>
          </div>
        </Marker>
      ))}

      {/* DYNAMIC PLANES */}
      {Object.values(planes).map((plane) => (
        <Marker key={plane.icao} longitude={plane.lon} latitude={plane.lat} rotation={plane.heading || 0}>
          <Plane size={12} className="text-white/60 rotate-45" />
        </Marker>
      ))}

      {/* DYNAMIC SHIPS */}
      {Object.values(ships).map((ship) => (
        <Marker key={ship.mmsi} longitude={ship.lon} latitude={ship.lat}>
          <Ship size={10} className="text-blue-400/80" />
        </Marker>
      ))}
    </Map>
  );
}