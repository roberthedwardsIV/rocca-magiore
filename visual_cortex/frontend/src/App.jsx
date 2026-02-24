import React, { useState, useEffect } from 'react';
import PanopticonMap from './components/PanopticonMap';
import { ShieldAlert, AlertTriangle, RadioTower, Flame, Activity } from 'lucide-react';

// --- NEW: RAW SVG SPARKLINE COMPONENT ---
// Ultra-lightweight, zero-dependency chart for the terminal aesthetic
const Sparkline = ({ data, width = 60, height = 18 }) => {
  if (!data || data.length < 2) {
    return <div style={{width, height}} className="flex items-center justify-end text-[8px] text-gray-700 italic">AWAITING_DATA</div>;
  }
  
  const min = Math.min(...data);
  const max = Math.max(...data);
  const range = max - min || 1;
  
  const points = data.map((d, i) => {
    const x = (i / (data.length - 1)) * width;
    const y = height - ((d - min) / range) * height;
    return `${x},${y}`;
  }).join(' ');

  // Color logic: If the oldest point is less than the newest point, it's trending UP (Green)
  const isUp = data[0] <= data[data.length - 1];
  const colorClass = isUp ? 'text-term_green' : 'text-term_red';

  return (
    <svg width={width} height={height} className={`overflow-visible ${colorClass} stroke-current opacity-80`}>
      <polyline points={points} fill="none" strokeWidth="1.5" strokeLinejoin="miter" strokeLinecap="square" />
    </svg>
  );
};


function App() {
  const [worldState, setWorldState] = useState({ assets: [], hubs: [], lines: [], chokepoints: [] });
  const [pulse, setPulse] = useState({ pnl: 0, balance: 0, avg_health: 1.0, critical_threats: 0, active_events: 0, tickers: [] });
  const [liveFeed, setLiveFeed] = useState([]);
  const [tradeSignals, setTradeSignals] = useState([]);
  const [activePositions, setActivePositions] = useState({}); 
  const [systemLogs, setSystemLogs] = useState({
    frontal_lobe: [], sensory_receptors: [], ibkr_gateway: [], thalamus: [], hippocampus: [], visual_cortex_backend: []
  });
  
  const [recentThreats, setRecentThreats] = useState([]);
  const [selectedEntity, setSelectedEntity] = useState(null);

  const handleEntitySelect = (entity) => {
    if (!entity) {
      setSelectedEntity(null);
      return;
    }
    setSelectedEntity(entity);
  };

  const [activeTab, setActiveTab] = useState('MAP'); 

  const fetchWorldState = async (bounds, zoom) => {
    try {
      const bbox = `${bounds.getWest()},${bounds.getSouth()},${bounds.getEast()},${bounds.getNorth()}`;
      const res = await fetch(`http://${window.location.hostname}:8000/api/world_state?bbox=${bbox}&zoom=${Math.round(zoom)}`);
      if (!res.ok) return; 
      const data = await res.json();
      if (data && Array.isArray(data.assets)) setWorldState(data);
    } catch (e) {
      console.error("Failed to fetch world state:", e);
    }
  };

  useEffect(() => {
    const fetchPulse = async () => {
      try {
        const res = await fetch(`http://${window.location.hostname}:8000/api/portfolio_pulse`);
        if (!res.ok) return; 
        const data = await res.json();
        if (data) {
          setPulse({
            pnl: data.pnl || 0,
            balance: data.balance || 0, 
            avg_health: data.avg_health !== null && data.avg_health !== undefined ? data.avg_health : 1.0,
            critical_threats: data.critical_threats || 0,
            active_events: data.active_events || 0,
            tickers: data.tickers || []
          });
        }
      } catch (e) { console.error("Pulse fetch error:", e); }
    };
    fetchPulse();
    const int = setInterval(fetchPulse, 5000);
    return () => clearInterval(int);
  }, []);

  useEffect(() => {
    const ws = new WebSocket(`ws://${window.location.hostname}:8000/ws/stream`);
    ws.onmessage = (e) => {
      try {
        const msg = JSON.parse(e.data);
        const event = new CustomEvent('stream-event', { detail: msg });
        window.dispatchEvent(event);

        if (['raw_signals', 'execution_signals'].includes(msg.channel)) {
          let feedStr = "";
          if (typeof msg.payload === 'object') {
              if (msg.channel === 'execution_signals') {
                  feedStr = `> [EXEC] ${msg.payload.action} ${msg.payload.sym || msg.payload.symbol} @ $${msg.payload.mkt?.toFixed(2)}`;
              } else if (msg.channel === 'raw_signals') {
                  const eType = msg.payload.entity_type ? msg.payload.entity_type.toUpperCase() : 'UNK';
                  const eId = msg.payload.entity_id ? msg.payload.entity_id.toString().substring(0,15) : '';
                  feedStr = `> [RAW] ${eType} | ID: ${eId}`;
                  
                  const cat = msg.payload.data?.category;
                  if (eType === 'EARTHQUAKE' || eType === 'WILDFIRE' || cat === 'threat' || cat === 'integrity') {
                    const newThreat = {
                      id: msg.payload.entity_id,
                      type: eType,
                      ts: msg.payload.timestamp,
                      severity: msg.payload.data?.severity || msg.payload.data?.mag || msg.payload.data?.frp || 'WARN',
                      lat: msg.payload.data?.lat?.toFixed(4) || 'N/A',
                      lon: msg.payload.data?.lon?.toFixed(4) || 'N/A'
                    };
                    setRecentThreats(prev => [newThreat, ...prev.filter(t => t.id !== newThreat.id)].slice(0, 5));
                  }
              } else {
                  feedStr = `> [${msg.channel.toUpperCase()}] DATA_RX`;
              }
          } else {
              feedStr = `> [${msg.channel.toUpperCase()}] ${msg.payload}`;
          }
          
          const timestamp = new Date().toLocaleTimeString('en-US', {hour12: false, hour: '2-digit', minute:'2-digit', second:'2-digit'});
          const feedItem = { time: timestamp, text: feedStr, channel: msg.channel };
          
          setLiveFeed(prev => [feedItem, ...prev].slice(0, 100));
        }

        if (msg.channel === 'execution_signals') {
          setTradeSignals(prev => [msg.payload, ...prev].slice(0, 20)); 
        }

        if (msg.channel === 'state_vectors' && msg.payload.type === 'portfolio_update') {
           setActivePositions(prev => ({ ...prev, [msg.payload.symbol]: msg.payload }));
        }
      } catch (err) {
        console.warn("Non-JSON WebSocket message received");
      }
    };
    return () => ws.close();
  }, []);

  useEffect(() => {
    if (activeTab !== 'SYS') return; 

    const wsLogs = new WebSocket(`ws://${window.location.hostname}:8000/ws/logs`);
    wsLogs.onmessage = (e) => {
      try {
        const msg = JSON.parse(e.data);
        if (msg.container && msg.log) {
          setSystemLogs(prev => ({
            ...prev,
            [msg.container]: [...(prev[msg.container] || []), msg.log].slice(-50)
          }));
        }
      } catch (err) { console.warn("Log parse error", err); }
    };
    return () => wsLogs.close();
  }, [activeTab]);

  const diagnosticPanes = [
    { id: 'frontal_lobe', title: 'FRONTAL LOBE', color: 'text-term_cyan' },
    { id: 'sensory_receptors', title: 'SENSORY RECEPTORS', color: 'text-term_amber' },
    { id: 'ibkr_gateway', title: 'IBKR GATEWAY', color: 'text-term_amber' },
    { id: 'thalamus', title: 'THALAMUS', color: 'text-term_green' },
    { id: 'hippocampus', title: 'HIPPOCAMPUS', color: 'text-term_cyan' },
    { id: 'visual_cortex_backend', title: 'VISUAL CORTEX', color: 'text-term_green' },
  ];

  const marqueeString = `ROCCO MAGGIORE V1.0  |  NET LIQ: $${(pulse?.balance || 0).toLocaleString(undefined, {minimumFractionDigits: 2})}  |  DAILY PNL: $${(pulse?.pnl || 0).toLocaleString(undefined, {minimumFractionDigits: 2})}  |  ` + 
    (pulse?.tickers || []).map(t => `${t.symbol}: $${t.price?.toFixed(2)} (${((t.vol || 0)*100).toFixed(1)}% IV)`).join('  |  ');

  return (
    <div className="h-screen w-screen bg-term_black text-gray-300 font-mono flex flex-col">     

      <style>{`
        @keyframes marquee {
          0% { transform: translateX(100vw); }
          100% { transform: translateX(-100%); }
        }
        .animate-marquee {
          display: inline-block;
          white-space: nowrap;
          animation: marquee 100s linear infinite;
        }
        .no-scrollbar::-webkit-scrollbar {
          display: none;
        }
        .no-scrollbar {
          -ms-overflow-style: none;
          scrollbar-width: none;
        }
      `}</style>

      {/* HEADER / TAB RIBBON */}
      <div className="h-8 border-b border-term_border bg-term_black flex items-center justify-between select-none shrink-0">
        <div className="flex items-center h-full">
          <div className="px-4 text-term_amber font-bold tracking-widest border-r border-term_border flex items-center h-full text-xs">
            ROCCO.MAGGIORE <span className="text-gray-600 font-normal ml-2">V1.0</span>
          </div>
          <button onClick={() => setActiveTab('MAP')}
            className={`h-full px-6 text-xs font-bold uppercase transition-none border-r border-term_border ${activeTab === 'MAP' ? 'bg-term_gray text-term_cyan border-t-2 border-t-term_cyan' : 'bg-term_black text-gray-500 hover:text-gray-300 border-t-2 border-t-transparent'}`}>
            1 MAP
          </button>
          <button onClick={() => setActiveTab('QUANT')}
            className={`h-full px-6 text-xs font-bold uppercase transition-none border-r border-term_border ${activeTab === 'QUANT' ? 'bg-term_gray text-term_amber border-t-2 border-t-term_amber' : 'bg-term_black text-gray-500 hover:text-gray-300 border-t-2 border-t-transparent'}`}>
            2 QUANT
          </button>
          <button onClick={() => setActiveTab('SYS')}
            className={`h-full px-6 text-xs font-bold uppercase transition-none border-r border-term_border ${activeTab === 'SYS' ? 'bg-term_gray text-term_green border-t-2 border-t-term_green' : 'bg-term_black text-gray-500 hover:text-gray-300 border-t-2 border-t-transparent'}`}>
            3 SYS
          </button>
        </div>
        <div className="px-4 text-[10px] text-gray-500 flex gap-4 h-full items-center">
          <span>DATAFEED: <span className="text-term_green font-bold">OK</span></span>
          <span>EXEC: <span className="text-term_cyan font-bold">LIVE</span></span>
        </div>
      </div>

      {/* MAIN CONTENT WORKSPACE */}
      <div className="flex-1 flex overflow-hidden">
        
        {/* GLOBAL LEFT PANEL */}
        <div className="w-80 border-r border-term_border bg-term_black flex flex-col z-10 shrink-0">   
          <div className="grid grid-cols-2 gap-[1px] bg-term_border border-b border-term_border shrink-0">
            <div className="bg-term_black p-2">
               <span className="text-[9px] text-gray-500 tracking-widest block mb-1">GLOBAL_HEALTH</span>
               <div className="text-term_cyan text-lg leading-none">{Number(pulse?.avg_health ?? 1.0).toFixed(2)}</div>
            </div>
            <div className="bg-term_black p-2">
               <span className="text-[9px] text-gray-500 tracking-widest block mb-1">CRIT_THREATS</span>
               <div className="text-term_red text-lg leading-none">{pulse?.critical_threats || 0}</div>
            </div>
          </div>
          <div className="p-2 border-b border-term_border shrink-0">
             <div className="text-[9px] text-gray-500 tracking-widest mb-1 border-b border-term_border pb-1">VIEWPORT_TOPOLOGY</div>
             <div className="grid grid-cols-2 gap-x-4 gap-y-1 text-[10px] text-gray-400 mt-2">
               <div className="flex justify-between"><span>ASSETS:</span> <span className="text-white">{worldState.assets?.length || 0}</span></div>
               <div className="flex justify-between"><span>HUBS:</span> <span className="text-white">{worldState.hubs?.length || 0}</span></div>
               <div className="flex justify-between"><span>ROUTES:</span> <span className="text-white">{worldState.lines?.length || 0}</span></div>
               <div className="flex justify-between"><span>CHOKES:</span> <span className="text-white">{worldState.chokepoints?.length || 0}</span></div>
             </div>
          </div>

          {/* --- THE FIX: ADDED SPARKLINE TO MARKET MATRIX --- */}
          <div className="p-2 border-b border-term_border flex flex-col h-48 shrink-0">
             <div className="text-[9px] text-gray-500 tracking-widest mb-1 border-b border-term_border pb-1">MARKET_MATRIX</div>
             <div className="overflow-y-auto scrollbar-thin flex-1 mt-1">
               <table className="w-full text-[10px] text-left border-collapse">
                 <thead>
                   <tr className="text-gray-600">
                     <th className="font-normal pb-1">SYM</th>
                     <th className="font-normal pb-1 text-right">PX</th>
                     <th className="font-normal pb-1 text-right">CHART</th>
                   </tr>
                 </thead>
                 <tbody>
                   {(pulse?.tickers || []).slice(0, 10).map(t => (
                     <tr key={t.symbol} className="border-b border-term_border/50 hover:bg-[#111]">
                       <td className="text-white py-1">{t.symbol}</td>
                       <td className="text-term_amber text-right py-1">${t.price?.toFixed(2) || '0.00'}</td>
                       <td className="py-1 flex justify-end">
                         <Sparkline data={t.history} />
                       </td>
                     </tr>
                   ))}
                   {(!pulse?.tickers || pulse.tickers.length === 0) && (
                      <tr><td colSpan="3" className="text-center py-2 text-gray-600 italic">No market data linked.</td></tr>
                   )}
                 </tbody>
               </table>
             </div>
          </div>

          <div className="flex-1 flex flex-col p-2 min-h-0 bg-term_black">
             <div className="text-[9px] text-gray-500 tracking-widest mb-1 border-b border-term_border pb-1 flex justify-between">
               <span>SYS_TAPE_OUTPUT</span>
               <span className="text-term_green animate-pulse">●</span>
             </div>
             <div className="flex-1 overflow-y-auto text-[9px] pr-1 space-y-[2px] mt-1 scrollbar-thin">
                {liveFeed.length === 0 && <div className="text-gray-600">AWAITING_DATA...</div>}
                {liveFeed.map((msg, i) => (
                  <div key={i} className="flex gap-2">
                    <span className="text-gray-500 shrink-0">{msg.time}</span>
                    <span className={`break-words ${msg.channel === 'execution_signals' ? 'text-term_amber font-bold' : 'text-gray-400'}`}>
                      {msg.text}
                    </span>
                  </div>
                ))}
             </div>
          </div>
        </div>

        {/* TAB WORKSPACE */}
        <div className="flex-1 relative bg-term_gray flex flex-col">
          
          {/* TAB 1: MAP + HUD */}
          {activeTab === 'MAP' && (
            <div className="flex-1 relative">
              <PanopticonMap 
                {...worldState} 
                onFetchRequest={fetchWorldState} 
                onSelect={handleEntitySelect}
                selectedId={selectedEntity?.id}
              />
              
              {/* --- HUD OVERLAY --- */}
              <div className="absolute bottom-0 left-0 right-0 z-30 pointer-events-none flex flex-col">
                <div className="px-4 pb-2 pointer-events-auto flex gap-2 overflow-x-auto no-scrollbar items-end min-h-[80px]">
                  {/* CONDITIONAL HUD DECK */}
                  {selectedEntity ? (
                    // TARGET LOCK TELEMETRY
                    <div className="bg-term_black/95 border border-term_cyan p-3 flex flex-col gap-2 backdrop-blur-md w-full max-w-4xl shadow-2xl relative animate-in fade-in slide-in-from-bottom-2">
                      <button 
                        onClick={() => setSelectedEntity(null)}
                        className="absolute top-1 right-2 text-gray-500 hover:text-white text-[10px]"
                      >
                        [CLOSE_X]
                      </button>
                      <div className="flex justify-between items-start border-b border-term_border pb-1">
                        <div>
                          <span className="text-term_cyan font-bold text-xs uppercase tracking-tighter"> TARGET_LOCK: {selectedEntity.name}</span>
                          <div className="text-[10px] text-gray-400 uppercase">UID: {selectedEntity.id} | TYPE: {selectedEntity.type}</div>
                        </div>
                        <div className="text-right">
                          <span className="text-[10px] text-gray-500 block">LOCAL_OP_STATUS</span>
                          <span className={`font-bold text-sm ${selectedEntity.op_health > 0.8 ? 'text-term_green' : 'text-term_red'}`}>
                            {((selectedEntity.op_health || 1) * 100).toFixed(1)}% NOMINAL
                          </span>
                        </div>
                      </div>
                      
                      {/* DYNAMIC TELEMETRY GRID */}
                      <div className="grid grid-cols-4 gap-4 py-1">
                        <div className="border-l border-term_border pl-2">
                          <span className="text-[9px] text-gray-500 block">EST_VALUATION</span>
                          <span className="text-white text-xs font-bold">${selectedEntity.npv ? selectedEntity.npv.toLocaleString() : '---'}</span>
                        </div>
                        <div className="border-l border-term_border pl-2">
                          <span className="text-[9px] text-gray-500 block">DISCOUNT_RATE (WACC)</span>
                          <span className="text-term_amber text-xs font-bold">{selectedEntity.wacc ? (selectedEntity.wacc * 100).toFixed(2) + '%' : '---'}</span>
                        </div>
                        <div className="border-l border-term_border pl-2">
                          <span className="text-[9px] text-gray-500 block">THROUGHPUT_OUTPUT</span>
                          <span className="text-term_cyan text-xs font-bold">{selectedEntity.throughput_tonnes || selectedEntity.output_mw || 'SECURE'}</span>
                        </div>
                        <div className="border-l border-term_border pl-2">
                          <span className="text-[9px] text-gray-500 block">LAST_SYNC_TS</span>
                          <span className="text-gray-400 text-[9px]">{selectedEntity.last_update ? new Date(selectedEntity.last_update).toLocaleTimeString() : 'N/A'}</span>
                        </div>
                      </div>
                    </div>
                  ) : (
                    // THREAT HORIZON (Default state)
                    recentThreats.length === 0 ? (
                      <div className="bg-term_black/80 border border-term_border p-2 text-[10px] text-gray-500 backdrop-blur-sm">
                        [SYS] NO RECENT ANOMALIES DETECTED
                      </div>
                    ) : (
                      recentThreats.map((t, idx) => {
                        const isEQ = t.type === 'EARTHQUAKE';
                        const isFire = t.type === 'WILDFIRE';
                        const color = (isEQ || isFire) ? 'border-term_red text-term_red' : 'border-term_amber text-term_amber';
                        const icon = isEQ ? <Activity size={10}/> : isFire ? <Flame size={10}/> : <AlertTriangle size={10}/>;
                        
                        return (
                          <div key={idx} className={`bg-term_black/90 border ${color} p-2 flex flex-col gap-1 backdrop-blur-sm w-64 shrink-0 shadow-lg cursor-pointer hover:bg-[#111] transition-colors`}>
                            <div className="flex justify-between items-center border-b border-gray-800 pb-1">
                              <span className="font-bold text-[10px] flex items-center gap-1">{icon} {t.type}</span>
                              <span className="text-[9px] text-gray-400">{new Date(t.ts).toLocaleTimeString('en-US', {hour12: false})}</span>
                            </div>
                            <div className="text-[10px] text-white">
                              SEVERITY: <span className="font-bold">{typeof t.severity === 'number' ? t.severity.toFixed(1) : t.severity}</span>
                            </div>
                            <div className="text-[9px] text-gray-400 flex justify-between">
                              <span>LAT: {t.lat}</span>
                              <span>LON: {t.lon}</span>
                            </div>
                          </div>
                        );
                      })
                    )
                  )}
                </div>

                {/* GLOBAL MARQUEE */}
                <div className="h-6 bg-term_black border-t border-term_border flex items-center overflow-hidden pointer-events-auto select-none">
                  <div className="animate-marquee text-[11px] text-term_cyan font-bold tracking-widest">
                    {marqueeString} &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp; {marqueeString}
                  </div>
                </div>
              </div>
            </div>
          )}

          {/* TAB 2: QUANT */}
          {activeTab === 'QUANT' && (
            <div className="h-full flex flex-col p-2 gap-2 bg-term_black">
              <div className="grid grid-cols-4 gap-[1px] bg-term_border shrink-0 border border-term_border">
                <div className="bg-term_black p-2 flex flex-col">
                  <span className="text-[9px] text-gray-500 tracking-widest">NET_LIQUIDATION</span>
                  <span className="text-xl text-white">${(pulse?.balance || 0).toLocaleString(undefined, {minimumFractionDigits: 2})}</span>
                </div>
                <div className="bg-term_black p-2 flex flex-col">
                  <span className="text-[9px] text-gray-500 tracking-widest">DAILY_PNL</span>
                  <span className={`text-xl ${pulse?.pnl >= 0 ? 'text-term_green' : 'text-term_red'}`}>
                    {pulse?.pnl >= 0 ? '+' : '-'}${(Math.abs(pulse?.pnl || 0)).toLocaleString(undefined, {minimumFractionDigits: 2})}
                  </span>
                </div>
                <div className="bg-term_black p-2 flex flex-col">
                  <span className="text-[9px] text-gray-500 tracking-widest">ACTIVE_POSITIONS</span>
                  <span className="text-xl text-term_cyan">{Object.keys(activePositions).length}</span>
                </div>
                <div className="bg-term_black p-2 flex flex-col">
                  <span className="text-[9px] text-gray-500 tracking-widest">PENDING_SIGNALS</span>
                  <span className="text-xl text-term_amber">{tradeSignals.length}</span>
                </div>
              </div>
              
              <div className="flex-1 grid grid-cols-12 gap-2 min-h-0">
                <div className="col-span-5 border border-term_border bg-term_black flex flex-col">
                   <div className="bg-term_gray text-term_cyan text-[10px] font-bold p-1 px-2 border-b border-term_border tracking-widest uppercase">
                     &gt; PORTFOLIO_LEDGER
                   </div>
                   <div className="flex-1 overflow-auto scrollbar-thin">
                      <table className="w-full text-[10px] text-right border-collapse">
                         <thead className="sticky top-0 bg-term_black border-b border-term_border text-gray-500">
                            <tr>
                              <th className="font-normal p-1 pl-2 text-left">SYM</th>
                              <th className="font-normal p-1">POS</th>
                              <th className="font-normal p-1">AVG_COST</th>
                              <th className="font-normal p-1">MKT_PX</th>
                              <th className="font-normal p-1 pr-2">U_PNL</th>
                            </tr>
                         </thead>
                         <tbody>
                            {Object.values(activePositions).map((pos, i) => (
                              <tr key={i} className="border-b border-term_border/30 hover:bg-[#111]">
                                <td className="p-1 pl-2 text-left text-white font-bold">{pos.symbol}</td>
                                <td className={`p-1 font-bold ${pos.position > 0 ? 'text-term_green' : pos.position < 0 ? 'text-term_red' : 'text-gray-400'}`}>
                                  {pos.position > 0 ? '+' : ''}{pos.position}
                                </td>
                                <td className="p-1 text-gray-300">${pos.average_cost?.toFixed(2)}</td>
                                <td className="p-1 text-term_amber">${pos.market_price?.toFixed(2)}</td>
                                <td className={`p-1 pr-2 ${pos.unrealized_pnl >= 0 ? 'text-term_green' : 'text-term_red'}`}>
                                  {pos.unrealized_pnl > 0 ? '+' : ''}${pos.unrealized_pnl?.toFixed(2)}
                                </td>
                              </tr>
                            ))}
                            {Object.keys(activePositions).length === 0 && (
                              <tr><td colSpan="5" className="text-center p-4 text-gray-600 italic">NO ACTIVE POSITIONS</td></tr>
                            )}
                         </tbody>
                      </table>
                   </div>
                </div>
                
                <div className="col-span-4 border border-term_border bg-term_black flex flex-col">
                   <div className="bg-term_gray text-term_amber text-[10px] font-bold p-1 px-2 border-b border-term_border tracking-widest uppercase flex justify-between">
                     <span>&gt; EXECUTION_BLOTTER</span>
                     <span className="text-term_green animate-pulse">●</span>
                   </div>
                   <div className="flex-1 overflow-auto scrollbar-thin">
                      <table className="w-full text-[10px] text-right border-collapse">
                         <thead className="sticky top-0 bg-term_black border-b border-term_border text-gray-500">
                            <tr>
                              <th className="font-normal p-1 pl-2 text-left">TIME</th>
                              <th className="font-normal p-1 text-left">ACT</th>
                              <th className="font-normal p-1 text-left">SYM</th>
                              <th className="font-normal p-1">CONF</th>
                              <th className="font-normal p-1 pr-2">RISK</th>
                            </tr>
                         </thead>
                         <tbody>
                            {tradeSignals.map((sig, i) => {
                               const timeStr = new Date(sig.ts || sig.timestamp).toLocaleTimeString('en-US', {hour12:false});
                               const isBuy = sig.side === 'BUY';
                               return (
                                 <tr key={i} className="border-b border-term_border/30 hover:bg-[#111]">
                                   <td className="p-1 pl-2 text-left text-gray-500">{timeStr}</td>
                                   <td className={`p-1 text-left font-bold ${isBuy ? 'text-term_green' : 'text-term_red'}`}>{sig.side}</td>
                                   <td className="p-1 text-left text-white">{sig.sym || sig.symbol}</td>
                                   <td className="p-1 text-term_cyan">{(sig.conf * 100).toFixed(0)}%</td>
                                   <td className="p-1 pr-2 text-gray-300">${sig.risk?.toFixed(0)}</td>
                                 </tr>
                               )
                            })}
                            {tradeSignals.length === 0 && (
                              <tr><td colSpan="5" className="text-center p-4 text-gray-600 italic">AWAITING SIGNALS...</td></tr>
                            )}
                         </tbody>
                      </table>
                   </div>
                </div>

                {/* --- THE FIX: ADDED SPARKLINE TO QUANT DESK WATCHLIST --- */}
                <div className="col-span-3 border border-term_border bg-term_black flex flex-col">
                   <div className="bg-term_gray text-[#a855f7] text-[10px] font-bold p-1 px-2 border-b border-term_border tracking-widest uppercase">
                     &gt; WATCHLIST
                   </div>
                   <div className="flex-1 overflow-auto scrollbar-thin">
                      <table className="w-full text-[10px] text-right border-collapse">
                         <thead className="sticky top-0 bg-term_black border-b border-term_border text-gray-500">
                            <tr>
                              <th className="font-normal p-1 pl-2 text-left">SYM</th>
                              <th className="font-normal p-1">LAST</th>
                              <th className="font-normal p-1 text-right">CHART</th>
                            </tr>
                         </thead>
                         <tbody>
                            {(pulse?.tickers || []).map((t, i) => (
                              <tr key={i} className="border-b border-term_border/30 hover:bg-[#111]">
                                <td className="p-1 pl-2 text-left text-white font-bold">{t.symbol}</td>
                                <td className="p-1 text-term_amber">${t.price?.toFixed(2) || '0.00'}</td>
                                <td className="py-1 flex justify-end pr-2">
                                  <Sparkline data={t.history} />
                                </td>
                              </tr>
                            ))}
                            {(!pulse?.tickers || pulse.tickers.length === 0) && (
                              <tr><td colSpan="3" className="text-center p-4 text-gray-600 italic">NO DATA</td></tr>
                            )}
                         </tbody>
                      </table>
                   </div>
                </div>
              </div>
            </div>
          )}

          {/* TAB 3: SYS DIAGNOSTICS */}
          {activeTab === 'SYS' && (
            <div className="h-full flex flex-col p-2 gap-2 bg-term_black">
              <div className="bg-term_gray text-term_green text-[10px] font-bold p-1 px-2 border border-term_border tracking-widest uppercase flex justify-between shrink-0">
                <span>&gt; SYSTEM_TTY_MULTIPLEXER</span>
                <span className="animate-pulse">_</span>
              </div>
              <div className="flex-1 grid grid-cols-3 grid-rows-2 gap-[1px] bg-term_border min-h-0 border border-term_border">
                {diagnosticPanes.map((pane) => {
                  const logs = systemLogs[pane.id] || [];
                  return (
                    <div key={pane.id} className="bg-term_black p-2 flex flex-col min-h-0 relative group">
                      <div className="text-[9px] font-bold mb-1 border-b border-[#222] pb-1 flex justify-between select-none">
                        <span className="text-gray-600">
                          root@rocco-nexus:~# tail -f <span className={pane.color}>{pane.id}.log</span>
                        </span>
                        <span className={`${pane.color} animate-pulse opacity-80`}>●</span>
                      </div>
                      <div className="flex-1 overflow-y-auto flex flex-col-reverse text-[9px] font-mono scrollbar-thin leading-tight pr-1">
                        {logs.length === 0 && (
                          <div className="text-gray-600 mt-auto flex items-center">
                            <span className={pane.color + " mr-2"}>&gt;</span> AWAITING_STDOUT... <span className="animate-pulse ml-1 bg-gray-500 w-1.5 h-3 inline-block"></span>
                          </div>
                        )}
                        {logs.slice().reverse().map((logLine, idx) => (
                          <div key={idx} className={`break-all mb-[3px] hover:bg-[#111] transition-colors ${idx === 0 ? 'text-gray-100 font-bold' : 'text-gray-500'}`}>
                            <span className={`${pane.color} opacity-40 mr-2 select-none`}>&gt;</span>
                            {logLine}
                          </div>
                        ))}
                      </div>
                    </div>
                  );
                })}
              </div>
            </div>
          )}

        </div>
      </div>
    </div>
  );
}

export default App;