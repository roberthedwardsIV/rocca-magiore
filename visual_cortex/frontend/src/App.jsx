import React, { useEffect, useState } from 'react';
import PulseSidebar from './components/PulseSidebar';
import PanopticonMap from './components/PanopticonMap';
import TapeSidebar from './components/TapeSidebar';

export default function App() {
  const [socket, setSocket] = useState(null);
  const [signals, setSignals] = useState([]);
  const [assets, setAssets] = useState([]);

  // 1. Initialize Data Streams
  useEffect(() => {
    // Fetch Static Assets
    fetch(`${import.meta.env.VITE_API_URL}/api/assets`)
      .then(res => res.json())
      .then(data => setAssets(data))
      .catch(err => console.error("Asset Fetch Error:", err));

    // Connect WebSocket
    const ws = new WebSocket(`${import.meta.env.VITE_API_URL.replace('http', 'ws')}/ws/stream`);
    ws.onmessage = (event) => {
      const msg = JSON.parse(event.data);
      
      // Dispatch to components based on channel
      if (msg.channel === 'execution_signals' || msg.channel === 'raw_signals') {
        setSignals(prev => [msg.payload, ...prev].slice(0, 50)); // Keep last 50
      }
      // Note: High frequency channels (aviation/maritime) will be handled by the Map component directly 
      // via a custom event bus to avoid re-rendering the whole App tree.
      window.dispatchEvent(new CustomEvent('stream-event', { detail: msg }));
    };
    
    setSocket(ws);
    return () => ws.close();
  }, []);

  return (
    // MAIN CONTAINER: Forces full viewport size
    <div className="flex h-full w-full bg-void text-offwhite overflow-hidden font-mono">
      
      {/* ZONE 1: THE PULSE (Left Sidebar - Fixed Width) */}
      <div className="w-80 flex-shrink-0 border-r border-gray-800 bg-gunmetal z-20 flex flex-col">
        <PulseSidebar assets={assets} />
      </div>

      {/* ZONE 2: THE PANOPTICON (Center - Grows to fill space) */}
      <div className="flex-1 relative z-10 bg-black">
        {/* We wrap the map in a relative container to ensure it bounds correctly */}
        <div className="absolute inset-0">
          <PanopticonMap assets={assets} />
        </div>
      </div>

      {/* ZONE 3: THE TAPE (Right Sidebar - Fixed Width) */}
      <div className="w-96 flex-shrink-0 border-l border-gray-800 bg-gunmetal z-20 flex flex-col">
        <TapeSidebar signals={signals} />
      </div>

    </div>
  );
}