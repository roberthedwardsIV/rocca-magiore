import React, { useState, useEffect, useRef } from 'react';
import PanopticonMap from './components/PanopticonMap';

// --- UTILS: Authenticity Generators ---
const toHex = (str) => {
  let hex = '';
  for(let i = 0; i < str.length && i < 24; i++) {
    hex += '' + str.charCodeAt(i).toString(16).padStart(2, '0');
  }
  return hex.toUpperCase();
};

export default function App() {
  // --- STATE ---
  const [assets, setAssets] = useState([]);
  const [links, setLinks] = useState([]);
  const [matrix, setMatrix] = useState([]);
  const [selectedAsset, setSelectedAsset] = useState(null);

  // Streams
  const [rawSignals, setRawSignals] = useState([]);
  const [thalamusLogs, setThalamusLogs] = useState([]);
  const [brainstemLogs, setBrainstemLogs] = useState([]);
  const [portfolio, setPortfolio] = useState({});

  // Refs for auto-scroll
  const rawRef = useRef(null);
  const thalamusRef = useRef(null);
  const brainstemRef = useRef(null);

  const scrollToBottom = (ref) => ref.current?.scrollIntoView({ behavior: 'smooth' });
  useEffect(() => scrollToBottom(rawRef), [rawSignals]);
  useEffect(() => scrollToBottom(thalamusRef), [thalamusLogs]);
  useEffect(() => scrollToBottom(brainstemRef), [brainstemLogs]);

  // --- DATA FETCHING ---
  const fetchMapData = async () => {
    try {
      const [assetsRes, linksRes, matrixRes] = await Promise.all([
        fetch(`http://${window.location.hostname}:8001/api/assets`),
        fetch(`http://${window.location.hostname}:8001/api/links`),
        fetch(`http://${window.location.hostname}:8001/api/matrix`)
      ]);
      if (assetsRes.ok) setAssets(await assetsRes.json());
      if (linksRes.ok) setLinks(await linksRes.json());
      if (matrixRes.ok) setMatrix(await matrixRes.json());
    } catch (e) {
      console.log("[API] Endpoints pending...");
    }
  };

  useEffect(() => { fetchMapData(); }, []);

  // --- WEBSOCKET LISTENER ---
  // --- WEBSOCKET LISTENER ---
  useEffect(() => {
    const ws = new WebSocket(`ws://${window.location.hostname}:8001/ws/stream`);  
      
    ws.onmessage = (event) => {
      try {
        const msg = JSON.parse(event.data);
        window.dispatchEvent(new CustomEvent('stream-event', { detail: msg })); // For the map

        // Route: Raw Signals (Sensory)
        if (msg.channel === 'raw_signals') {
          setRawSignals(prev => [...prev.slice(-49), msg.payload]);
        }
        
        // Route: Execution Signals (Brainstem) - IGNORED FOR NOW
        // We leave this block empty so the UI doesn't try to parse the raw JSON trade packet
        else if (msg.channel === 'execution_signals') {
          // Do nothing. We are relying on the formatted string from system_logs instead.
        }
        
        // Route: System Logs
        else if (msg.channel === 'system_logs') {
          const logEntry = `[${new Date().toLocaleTimeString()}] ${msg.log}`;
          
          // Route Thalamus Logs
          if (msg.container === 'thalamus') {
              // If it's an ALPHA FOUND log, send it to the execution panel
              if (msg.log.includes("ALPHA FOUND")) {
                  setBrainstemLogs(prev => [...prev.slice(-49), logEntry]);
              } else {
                  // Otherwise, it's a standard routing hit, send it to the routing panel
                  setThalamusLogs(prev => [...prev.slice(-49), logEntry]);
              }
          }
          
          // Route Brainstem Logs (If the actual Brainstem container comes online later)
          if (msg.container === 'brainstem') {
              setBrainstemLogs(prev => [...prev.slice(-49), logEntry]);
          }
        }

        // Route: Portfolio Updates
        else if (msg.channel === 'state_vectors' && msg.payload.type === 'portfolio_update') {
          setPortfolio(prev => ({ ...prev, [msg.payload.symbol]: msg.payload }));
        }

      } catch (err) {}
    };

    return () => ws.close();
  }, []);

  return (
    <div className="w-full h-screen flex bg-[#000000] overflow-hidden text-[#e0e0e0] font-mono text-[10px]">
      
      {/* ================= LEFT: SENSORY & THALAMUS ================= */}
      <div className="w-80 h-full border-r border-[#333333] flex flex-col z-10 bg-[#000000]/90">
        
        {/* Raw Signals */}
        <div className="flex-1 flex flex-col p-2 border-b border-[#333333] min-h-0">
          <div className="text-[#00f2ea] border-b border-[#333333] pb-1 mb-2 font-bold flex justify-between">
            <span>&gt; SENSORY_RECEPTORS / FRONTAL_LOBE</span> <span className="animate-pulse">●</span>
          </div>
          <div className="flex-1 overflow-y-auto space-y-1 pr-1 scrollbar-thin">
            {rawSignals.map((sig, i) => (
              <div key={i} className="flex flex-col opacity-80 hover:opacity-100 bg-[#111] p-1 border border-[#222]">
                <span className="text-[#555] leading-none">0x{toHex(sig.entity_id || sig.symbol || 'UNK')}</span>
                <span className="text-[#ffbf00]">TYPE: {sig.entity_type || 'MARKET'}</span>
                <span className="text-gray-400 break-all">{JSON.stringify(sig.data || sig)}</span>
              </div>
            ))}
            <div ref={rawRef} />
          </div>
        </div>

        {/* Thalamus Logs */}
        <div className="flex-1 flex flex-col p-2 min-h-0">
          <div className="text-[#00f2ea] border-b border-[#333333] pb-1 mb-2 font-bold">
            &gt; THALAMUS
          </div>
          <div className="flex-1 overflow-y-auto space-y-1 pr-1 scrollbar-thin">
            {thalamusLogs.map((log, i) => (
              <div key={i} className="opacity-80 hover:opacity-100">{log}</div>
            ))}
            <div ref={thalamusRef} />
          </div>
        </div>
      </div>

      {/* ================= CENTER: ORBITAL SYNAPSE ================= */}
      <div className="flex-1 relative">
        <PanopticonMap 
          assets={assets}
          links={links} // <--- Passed directly to DeckGL
          onFetchRequest={() => {}}
          onSelect={setSelectedAsset}
          selectedId={selectedAsset?.id}
        />
        
        {/* HUD OVERLAY if asset/arc clicked */}
        {selectedAsset && (
          <div className="absolute top-4 left-1/2 -translate-x-1/2 bg-[#000]/95 border border-[#00f2ea] p-4 z-50 text-xs shadow-2xl holo-text w-80">
            <button onClick={() => setSelectedAsset(null)} className="absolute top-1 right-2 text-[#555] hover:text-[#ff0000]">X</button>
            
            {/* ROUTE 1: ARC (SUPPLY CHAIN LINK) HUD */}
            {selectedAsset.origin_asset_id !== undefined ? (() => {
              // Cross-reference the assets array to get the actual names
              const oName = assets.find(a => a.id === selectedAsset.origin_asset_id)?.name || `Asset_${selectedAsset.origin_asset_id}`;
              const tName = assets.find(a => a.id === selectedAsset.target_asset_id)?.name || `Asset_${selectedAsset.target_asset_id}`;
              
              return (
                <>
                  <div className="text-[#ffbf00] font-bold border-b border-[#333] pb-1 mb-2">TARGET_LOCK: SUPPLY_LINK</div>
                  <div className="text-[#00f2ea] truncate" title={oName}>ORIGIN : {oName}</div>
                  <div className="text-[#ff0080] truncate" title={tName}>TARGET : {tName}</div>
                  
                  <div className="mt-2 border-t border-[#333] pt-1 text-[#e0e0e0]">
                    <div>CONFIDENCE : {(selectedAsset.confidence_score * 100).toFixed(1)}%</div>
                    <div>DEPENDENCY : {(selectedAsset.dependency_weight * 100).toFixed(1)}%</div>
                    <div>TRANS_LAG  : {selectedAsset.transport_lag_days} DAYS</div>
                  </div>
                </>
              );
            })() : 
            
            /* ROUTE 2: PHYSICAL ASSET HUD */
            (
              <>
                <div className="text-[#ffbf00] font-bold border-b border-[#333] pb-1 mb-2 truncate" title={selectedAsset.name}>
                  TARGET_LOCK: {selectedAsset.name}
                </div>
                <div className="text-[#555]">LAT: {selectedAsset.lat?.toFixed(4)} | LON: {selectedAsset.lon?.toFixed(4)}</div>
                
                <div className="mt-2 border-t border-[#333] pt-1 text-[#00f2ea]">
                  <div>TYPE    : {selectedAsset.type || 'UNKNOWN'}</div>
                  <div>SOURCE  : {selectedAsset.source || 'INTERNAL_SYS'}</div>
                  <div className="truncate" title={selectedAsset.commodity_types}>
                    COMMOD  : {selectedAsset.commodity_types ? selectedAsset.commodity_types.replace(/[{}]/g, '') : 'N/A'}
                  </div>
                  <div className="truncate" title={selectedAsset.operator || selectedAsset.company || selectedAsset.owner_name_raw}>
                    OWNER   : {selectedAsset.operator || selectedAsset.company || selectedAsset.owner_name_raw || 'UNKNOWN'}
                  </div>
                  <div className={selectedAsset.is_private ? 'text-[#ffbf00] mt-1' : 'text-[#00ff00] mt-1'}>
                    CLASS   : {selectedAsset.is_private ? 'PRIVATE' : 'PUBLIC'}
                  </div>
                </div>

                {/* NEW: EMBEDDED MATRIX LEVERAGE */}
                {selectedAsset.matrix_entries && selectedAsset.matrix_entries.length > 0 && (
                  <div className="mt-2 border-t border-[#333] pt-2">
                    <div className="text-[#ffbf00] font-bold mb-1">&gt; SYNAPTIC_LINKS:</div>
                    <div className="max-h-24 overflow-y-auto scrollbar-thin space-y-1 pr-1">
                      {selectedAsset.matrix_entries.map((entry, idx) => (
                        <div key={idx} className="flex justify-between items-center bg-[#111] p-1 border border-[#222]">
                          <span className="text-[#00f2ea] font-bold">{entry.ticker}</span>
                          <span className="text-[9px] text-[#555] mx-1 truncate">{entry.event_type}</span>
                          <span className={entry.beta > 0 ? 'text-[#00ff00]' : 'text-[#ff0000]'}>
                            β:{entry.beta?.toFixed(2)}
                          </span>
                        </div>
                      ))}
                    </div>
                  </div>
                )}
              </>
            )}
          </div>
        )}
      </div>

      {/* ================= RIGHT: BRAINSTEM & MATRIX ================= */}
      <div className="w-96 h-full border-l border-[#333333] flex flex-col z-10 bg-[#000000]/90">
        
        {/* Brainstem Executions (Now 50% Height) */}
        <div className="h-1/2 flex flex-col p-2 border-b border-[#333333] min-h-0">
          <div className="text-[#ffbf00] border-b border-[#333333] pb-1 mb-2 font-bold flex justify-between">
            <span>&gt; BRAINSTEM_EXECUTION</span> <span className="animate-pulse">●</span>
          </div>
          <div className="flex-1 overflow-y-auto space-y-1 pr-1 scrollbar-thin text-[#e0e0e0]">
            {brainstemLogs.map((log, i) => (
              <div key={i} className="hover:bg-[#111]">{log}</div>
            ))}
            <div ref={brainstemRef} />
          </div>
        </div>

        {/* Sensitivity Matrix (Now 50% Height, bottom border removed) */}
        <div className="h-1/2 flex flex-col p-2 min-h-0">
          <div className="text-[#00f2ea] border-b border-[#333333] pb-1 mb-2 font-bold">
            &gt; SENSITIVITY_MATRIX
          </div>
          <div className="flex-1 overflow-y-auto scrollbar-thin">
            <table className="w-full text-left">
              <thead className="text-[#555]">
                <tr><th>ASSET</th><th>TICKER</th><th>BETA</th><th>CONF</th></tr>
              </thead>
              <tbody>
                {matrix.map((m, i) => (
                  <tr key={i} className="border-b border-[#222] hover:bg-[#111]">
                    <td className="py-1 truncate max-w-[80px]" title={m.asset_name}>{m.asset_name}</td>
                    <td className="text-[#ffbf00] font-bold">{m.ticker}</td>
                    <td className={m.beta_coefficient > 0 ? 'text-[#00ff00]' : 'text-[#ff0000]'}>{m.beta_coefficient?.toFixed(2)}</td>
                    <td>{(m.confidence_score * 100).toFixed(0)}%</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </div>


        

      </div>
    </div>
  );
}