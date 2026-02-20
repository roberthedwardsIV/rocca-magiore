import React, { useState, useEffect } from 'react';
import PanopticonMap from './components/PanopticonMap';
import { Activity, AlertTriangle, ShieldAlert, Cpu, TerminalSquare, X, LineChart, TrendingUp, TrendingDown, DollarSign } from 'lucide-react';

function App() {
  const [worldState, setWorldState] = useState({ assets: [], hubs: [], lines: [], chokepoints: [] });
  const [pulse, setPulse] = useState({ pnl: 0, avg_health: 1.0, critical_threats: 0, active_events: 0, tickers: [] });
  const [liveFeed, setLiveFeed] = useState([]);
  
  const [isDiagnosticsOpen, setIsDiagnosticsOpen] = useState(false);
  const [isQuantOpen, setIsQuantOpen] = useState(false); // NEW: Quant Modal State
  const [tradeSignals, setTradeSignals] = useState([]);  // NEW: Execution Tape State
  
  // State to hold our live Docker logs
  const [systemLogs, setSystemLogs] = useState({
    frontal_lobe: [],
    sensory_receptors: [],
    ibkr_gateway: [],
    thalamus: [],
    hippocampus: [],
    visual_cortex_backend: []
  });

  const fetchWorldState = async (bounds, zoom) => {
    try {
      const bbox = `${bounds.getWest()},${bounds.getSouth()},${bounds.getEast()},${bounds.getNorth()}`;
      const res = await fetch(`http://${window.location.hostname}:8000/api/world_state?bbox=${bbox}&zoom=${Math.round(zoom)}`);
      
      if (!res.ok) return; 

      const data = await res.json();
      
      if (data && Array.isArray(data.assets)) {
        setWorldState(data);
      }
    } catch (e) {
      console.error("Failed to fetch world state:", e);
    }
  };

  // Periodic Pulse Fetcher
  useEffect(() => {
    const fetchPulse = async () => {
      try {
        const res = await fetch(`http://${window.location.hostname}:8000/api/portfolio_pulse`);
        
        if (!res.ok) return; 

        const data = await res.json();
        if (data) {
          setPulse({
            pnl: data.pnl || 0,
            avg_health: data.avg_health !== null && data.avg_health !== undefined ? data.avg_health : 1.0,
            critical_threats: data.critical_threats || 0,
            active_events: data.active_events || 0,
            tickers: data.tickers || []
          });
        }
      } catch (e) {
        console.error("Pulse fetch error:", e);
      }
    };
    fetchPulse();
    const int = setInterval(fetchPulse, 5000);
    return () => clearInterval(int);
  }, []);

  // Main Event Stream (Map & Tape)
  useEffect(() => {
    const ws = new WebSocket(`ws://${window.location.hostname}:8000/ws/stream`);
    ws.onmessage = (e) => {
      try {
        const msg = JSON.parse(e.data);
        
        const event = new CustomEvent('stream-event', { detail: msg });
        window.dispatchEvent(event);

        if (['raw_signals', 'execution_signals'].includes(msg.channel)) {
          const textStr = typeof msg.payload === 'object' ? JSON.stringify(msg.payload) : msg.payload;
          const feedItem = `[${msg.channel.toUpperCase()}] ${textStr}`;
          setLiveFeed(prev => [feedItem, ...prev].slice(0, 50));
        }

        // Capture execution signals for the Quant Desk
        if (msg.channel === 'execution_signals') {
           setTradeSignals(prev => [msg.payload, ...prev].slice(0, 20)); // Keep last 20 trades
        }
      } catch (err) {
        console.warn("Non-JSON WebSocket message received:", e.data);
      }
    };
    return () => ws.close();
  }, []);

  // Dedicated Diagnostics Log Streamer
  useEffect(() => {
    // Only connect and stream logs if the modal is actually open
    if (!isDiagnosticsOpen) return;

    const wsLogs = new WebSocket(`ws://${window.location.hostname}:8000/ws/logs`);
    
    wsLogs.onmessage = (e) => {
      try {
        const msg = JSON.parse(e.data);
        if (msg.container && msg.log) {
          setSystemLogs(prev => ({
            ...prev,
            // Append new log and slice to keep only the last 50 lines per container
            [msg.container]: [...(prev[msg.container] || []), msg.log].slice(-50)
          }));
        }
      } catch (err) {
        console.warn("Log parse error", err);
      }
    };

    return () => wsLogs.close();
  }, [isDiagnosticsOpen]);

  // Definitions for the 6 diagnostic panes
  const diagnosticPanes = [
    { id: 'frontal_lobe', title: 'FRONTAL LOBE (AI & PARSING)', color: 'text-cyan-500' },
    { id: 'sensory_receptors', title: 'SENSORY RECEPTORS (INGESTION)', color: 'text-yellow-500' },
    { id: 'ibkr_gateway', title: 'IBKR GATEWAY (EXECUTION)', color: 'text-orange-500' },
    { id: 'thalamus', title: 'THALAMUS (SIGNAL ROUTING)', color: 'text-green-500' },
    { id: 'hippocampus', title: 'HIPPOCAMPUS (DATABASE)', color: 'text-purple-500' },
    { id: 'visual_cortex_backend', title: 'VISUAL CORTEX (UI/API)', color: 'text-rose-500' },
  ];

  return (
    <div className="h-screen w-screen bg-[#0a0f1c] text-slate-200 font-sans overflow-hidden flex flex-col">
      
      {/* GLOBAL CSS OVERRIDE */}
      <style>{`
        .maplibregl-popup-content {
          background: rgba(15, 23, 42, 0.95) !important;
          border: 1px solid #0891b2 !important;
          border-radius: 0.5rem !important;
          padding: 0.5rem !important;
          box-shadow: 0 10px 25px -5px rgba(0, 0, 0, 0.8) !important;
        }
        .maplibregl-popup-tip {
          border-top-color: rgba(15, 23, 42, 0.95) !important;
        }
      `}</style>

      {/* HEADER */}
      <div className="h-14 border-b border-slate-800 bg-[#0d1424] flex items-center justify-between px-6 shadow-md z-10">
        <div className="flex items-center gap-4">
          <Activity className="text-cyan-400" size={20} />
          <h1 className="text-xl font-bold text-white tracking-widest">
            ROCCO<span className="text-cyan-500">.AI</span> <span className="text-slate-500 font-light">// PANOPTICON</span>
          </h1>
        </div>
        
        <div className="flex items-center gap-6">
          <div className="flex items-center gap-2 text-sm text-cyan-100 bg-cyan-900/30 px-3 py-1 rounded-full border border-cyan-800/50">
            <Cpu size={14} className="text-cyan-400" />
            <span>SYSTEM: <span className="text-cyan-400 font-bold">NOMINAL</span></span>
          </div>

          <button 
            onClick={() => setIsQuantOpen(true)}
            className="flex items-center gap-2 text-sm bg-indigo-900/40 hover:bg-indigo-800 text-indigo-100 px-4 py-1.5 rounded border border-indigo-700/50 transition-colors shadow-sm"
          >
            <LineChart size={16} className="text-indigo-400" />
            <span className="font-bold tracking-wide">QUANT DESK</span>
          </button>
          
          <button 
            onClick={() => setIsDiagnosticsOpen(true)}
            className="flex items-center gap-2 text-sm bg-slate-800 hover:bg-slate-700 text-white px-4 py-1.5 rounded border border-slate-600 transition-colors shadow-sm"
          >
            <TerminalSquare size={16} className="text-green-400" />
            <span className="font-bold tracking-wide">DIAGNOSTICS</span>
          </button>
        </div>
      </div>

      {/* MAIN CONTENT */}
      <div className="flex-1 flex overflow-hidden">
        
        {/* LEFT PANEL */}
        <div className="w-80 border-r border-slate-800 bg-[#0d1424]/90 p-4 flex flex-col gap-4 z-10 backdrop-blur-md shadow-xl">
          
          <div className="bg-slate-900 border border-slate-700 rounded-lg p-4 shadow-sm">
            <div className="text-xs font-bold text-white mb-1 uppercase tracking-wider flex items-center gap-2">
              <ShieldAlert size={14} className="text-cyan-400"/> Global Op Health
            </div>
            <div className="text-3xl font-light text-white">{Number(pulse?.avg_health ?? 1.0).toFixed(2)}</div>
            <div className="text-xs text-cyan-400 mt-1">Network Stability</div>
          </div>

          <div className="bg-slate-900 border border-slate-700 rounded-lg p-4 shadow-sm">
            <div className="text-xs font-bold text-white mb-1 uppercase tracking-wider flex items-center gap-2">
              <AlertTriangle size={14} className="text-red-400"/> Critical Threats
            </div>
            <div className="text-3xl font-light text-white">{pulse?.critical_threats || 0}</div>
            <div className="text-xs text-red-400 mt-1">Active Chokepoints / Outages</div>
          </div>

          <div className="flex-1 bg-slate-900 border border-slate-700 rounded-lg p-4 flex flex-col shadow-sm">
            <h2 className="text-xs font-bold text-white mb-3 uppercase tracking-wider">Live Signal Feed</h2>
            <div className="flex-1 bg-[#05080f] rounded p-3 font-mono text-[10px] overflow-y-auto border border-slate-800 shadow-inner">
              {liveFeed.length === 0 && <div className="text-white italic">Awaiting signals from Thalamus...</div>}
              {liveFeed.map((msg, i) => (
                <div key={i} className="mb-2 text-green-400 border-b border-slate-800/50 pb-1 break-words">
                  {msg}
                </div>
              ))}
            </div>
          </div>

        </div>

        {/* MAP CONTAINER */}
        <div className="flex-1 relative bg-void">
          <PanopticonMap 
            {...worldState} 
            onFetchRequest={fetchWorldState} 
          />
        </div>
      </div>

      {/* QUANT DESK MODAL */}
      {isQuantOpen && (
        <div className="absolute inset-0 z-50 bg-black/90 backdrop-blur-md flex items-center justify-center p-6">
          <div className="bg-[#0a0f1c] border border-indigo-900/50 w-full h-full rounded-xl shadow-2xl flex flex-col overflow-hidden">
            
            {/* Header */}
            <div className="h-12 bg-slate-900 border-b border-indigo-900/50 flex items-center justify-between px-4 shadow-md">
              <h2 className="text-indigo-100 font-bold tracking-widest flex items-center gap-2">
                <LineChart className="text-indigo-400"/> ROCCO.AI QUANTITATIVE INTELLIGENCE
              </h2>
              <button onClick={() => setIsQuantOpen(false)} className="text-red-400 hover:text-red-300 transition-colors bg-slate-800 hover:bg-slate-700 p-1 rounded">
                <X size={20} />
              </button>
            </div>
            
            {/* 3-Column Layout */}
            <div className="flex-1 grid grid-cols-3 gap-4 p-4 min-h-0 bg-[#05080f]">
              
              {/* COL 1: Risk & Exposure */}
              <div className="flex flex-col gap-4 min-h-0">
                <div className="bg-slate-900 border border-slate-800 p-4 rounded shadow-sm">
                  <div className="text-xs text-white mb-1 font-bold tracking-wider">DAILY P&L (PAPER)</div>
                  <div className={`text-4xl font-light flex items-center gap-2 ${pulse?.pnl >= 0 ? 'text-green-400' : 'text-red-400'}`}>
                    {pulse?.pnl >= 0 ? <TrendingUp size={24}/> : <TrendingDown size={24}/>}
                    ${Math.abs(pulse?.pnl || 0).toLocaleString(undefined, {minimumFractionDigits: 2})}
                  </div>
                </div>
                
                <div className="bg-slate-900 border border-slate-800 p-4 rounded shadow-sm flex-1">
                  <div className="text-xs text-white mb-3 font-bold tracking-wider">PORTFOLIO METRICS</div>
                  <div className="space-y-3 font-mono text-sm">
                    <div className="flex justify-between border-b border-slate-800 pb-1">
                      <span className="text-white">Global Health Factor</span>
                      <span className="text-cyan-400">{Number(pulse?.avg_health ?? 1.0).toFixed(2)}</span>
                    </div>
                    <div className="flex justify-between border-b border-slate-800 pb-1">
                      <span className="text-white">Active Alpha Signals</span>
                      <span className="text-yellow-400">{tradeSignals.length}</span>
                    </div>
                    <div className="flex justify-between border-b border-slate-800 pb-1">
                      <span className="text-white">Risk Utilization</span>
                      <span className="text-green-400">NOMINAL</span>
                    </div>
                  </div>
                </div>
              </div>

              {/* COL 2: Market Matrix (Tickers) */}
              <div className="bg-slate-900 border border-slate-800 rounded flex flex-col min-h-0">
                <div className="p-3 border-b border-slate-800 text-xs text-indigo-400 font-bold tracking-wider">
                  MARKET MATRIX
                </div>
                <div className="flex-1 overflow-y-auto p-2 scrollbar-thin scrollbar-thumb-slate-700">
                  <table className="w-full text-left font-mono text-xs">
                    <thead>
                      <tr className="text-white border-b border-slate-800">
                        <th className="pb-2 pl-2">SYM</th>
                        <th className="pb-2 text-right">PRICE</th>
                        <th className="pb-2 text-right">IMPLIED VOL</th>
                      </tr>
                    </thead>
                    <tbody>
                      {(pulse?.tickers || []).map((t, i) => (
                        <tr key={i} className="border-b border-slate-800/50 hover:bg-white/5 transition-colors">
                          <td className="py-2 pl-2 text-white font-bold">{t.symbol}</td>
                          <td className="py-2 text-right text-white">${(t.price || 0).toFixed(2)}</td>
                          <td className="py-2 text-right text-white">{((t.vol || 0) * 100).toFixed(1)}%</td>
                        </tr>
                      ))}
                      {(!pulse?.tickers || pulse.tickers.length === 0) && (
                        <tr><td colSpan="3" className="text-center py-4 text-white italic">No market data linked.</td></tr>
                      )}
                    </tbody>
                  </table>
                </div>
              </div>

              {/* COL 3: Live Execution Tape */}
              <div className="bg-slate-900 border border-slate-800 rounded flex flex-col min-h-0">
                <div className="p-3 border-b border-slate-800 text-xs text-yellow-500 font-bold tracking-wider flex justify-between items-center">
                  <span>EXECUTION TAPE</span>
                  <span className="text-green-500 animate-pulse">●</span>
                </div>
                <div className="flex-1 overflow-y-auto p-2 scrollbar-thin scrollbar-thumb-slate-700 flex flex-col gap-2">
                  {tradeSignals.length === 0 && (
                    <div className="text-white italic text-center mt-4 text-xs font-mono">Awaiting Brainstem signals...</div>
                  )}
                  {tradeSignals.map((sig, i) => (
                    <div key={i} className="bg-[#0a0f1c] border-l-2 border-yellow-500 p-2 rounded text-xs font-mono">
                      <div className="flex justify-between items-center mb-1">
                        <span className="font-bold text-yellow-500">{sig.action} {sig.sym || sig.symbol}</span>
                        <span className="text-slate-500">{new Date(sig.ts || sig.timestamp).toLocaleTimeString()}</span>
                      </div>
                      <div className="grid grid-cols-2 gap-x-4 text-white">
                        <div>Target: <span className="text-green-400">${sig.tgt?.toFixed(2) || 'N/A'}</span></div>
                        <div>Stop: <span className="text-red-400">${sig.hard?.toFixed(2) || 'N/A'}</span></div>
                        <div>Conf: <span className="text-white">{(sig.conf * 100).toFixed(0)}%</span></div>
                        <div>Risk: <span className="text-white">${sig.risk?.toFixed(0) || 'N/A'}</span></div>
                      </div>
                    </div>
                  ))}
                </div>
              </div>

            </div>
          </div>
        </div>
      )}

      {/* DIAGNOSTICS MODAL */}
      {isDiagnosticsOpen && (
        <div className="absolute inset-0 z-50 bg-black/90 backdrop-blur-md flex items-center justify-center p-6">
          <div className="bg-[#0a0f1c] border border-slate-600 w-full h-full rounded-xl shadow-2xl flex flex-col overflow-hidden">
            
            <div className="h-12 bg-slate-800 border-b border-slate-600 flex items-center justify-between px-4 shadow-md">
              <h2 className="text-white font-bold tracking-widest flex items-center gap-2">
                <TerminalSquare className="text-cyan-400"/> ROCCO.AI SYSTEM DIAGNOSTICS
              </h2>
              <button onClick={() => setIsDiagnosticsOpen(false)} className="text-red-400 hover:text-red-300 transition-colors bg-slate-700/50 hover:bg-slate-700 p-1 rounded">
                <X size={20} />
              </button>
            </div>
            
            {/* 3x2 GRID LAYOUT - FIXED OVERFLOW AND STYLING */}
            <div className="flex-1 grid grid-cols-3 grid-rows-2 gap-2 p-2 bg-black min-h-0">
              {diagnosticPanes.map((pane) => (
                <div key={pane.id} className="bg-[#05080f] border border-slate-800 p-3 font-mono text-[10px] overflow-hidden flex flex-col min-h-0 min-w-0 rounded">
                  <div className={`${pane.color} font-bold mb-2 border-b border-slate-800 pb-1 flex justify-between shrink-0`}>
                    <span>{pane.title}</span>
                    <span className="text-green-500 animate-pulse">●</span>
                  </div>
                  
                  {/* flex-col-reverse pushes items to the bottom. Reversed array anchors newest logs to the bottom perfectly. */}
                  <div className="flex-1 overflow-y-auto flex flex-col-reverse scrollbar-thin scrollbar-thumb-slate-700 pr-2">
                    {(systemLogs[pane.id] || []).length === 0 && (
                      <div className="text-slate-600 italic mt-auto">Waiting for log stream...</div>
                    )}
                    {(systemLogs[pane.id] || []).slice().reverse().map((logLine, idx) => (
                      <div key={idx} className="!text-white break-all mb-1 border-b border-slate-800/50 pb-1 shrink-0 leading-tight">
                        {logLine}
                      </div>
                    ))}
                  </div>
                </div>
              ))}
            </div>

          </div>
        </div>
      )}

    </div>
  );
}

export default App;