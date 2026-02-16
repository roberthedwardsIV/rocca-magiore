import React from 'react';
import { motion, AnimatePresence } from 'framer-motion';
import { Activity, Terminal, TrendingUp, TrendingDown, AlertTriangle } from 'lucide-react';

export default function TapeSidebar({ signals }) {
  return (
    <div className="flex flex-col h-full bg-gunmetal border-l border-gray-800 font-mono text-xs">
      
      {/* HEADER */}
      <div className="p-3 border-b border-gray-700 bg-void flex items-center justify-between">
        <div className="flex items-center gap-2 text-cyan">
          <Activity size={16} />
          <span className="font-bold tracking-wider">STRATEGY.TAPE</span>
        </div>
        <div className="flex items-center gap-2">
          <span className="w-2 h-2 rounded-full bg-green-500 animate-pulse"></span>
          <span className="text-gray-500">LIVE</span>
        </div>
      </div>

      {/* SIGNAL FEED (SCROLLABLE) */}
      <div className="flex-1 overflow-y-auto p-2 space-y-2 scrollbar-thin scrollbar-thumb-gray-700">
        <AnimatePresence initial={false}>
          {signals.map((sig, i) => (
            <motion.div
              key={sig.id || i}
              initial={{ opacity: 0, x: 20 }}
              animate={{ opacity: 1, x: 0 }}
              exit={{ opacity: 0 }}
              transition={{ duration: 0.2 }}
              className="relative p-3 bg-void border border-gray-800 rounded hover:border-gray-600 transition-colors group"
            >
              {/* Signal Header */}
              <div className="flex justify-between items-start mb-2">
                <div className="flex flex-col">
                  <span className="text-lg font-bold text-white">{sig.sym}</span>
                  <span className="text-[10px] text-gray-500">{new Date(sig.ts).toLocaleTimeString()}</span>
                </div>
                <div className={`px-2 py-1 rounded text-[10px] font-bold flex items-center gap-1 ${
                  sig.side === 'BUY' ? 'bg-cyan/10 text-cyan' : 'bg-crimson/10 text-crimson'
                }`}>
                  {sig.side === 'BUY' ? <TrendingUp size={10} /> : <TrendingDown size={10} />}
                  {sig.side}
                </div>
              </div>

              {/* Data Grid */}
              <div className="grid grid-cols-2 gap-2 text-[10px] mb-2 text-gray-400">
                <div>
                  <span className="block text-gray-600">FAIR VAL</span>
                  <span className="text-white font-semibold">${sig.fv?.toFixed(2)}</span>
                </div>
                <div>
                  <span className="block text-gray-600">MARKET</span>
                  <span className="text-white font-semibold">${sig.mkt?.toFixed(2)}</span>
                </div>
                <div>
                  <span className="block text-gray-600">RISK</span>
                  <span className="text-amber">${sig.risk?.toFixed(0)}</span>
                </div>
                <div>
                  <span className="block text-gray-600">Z-SCORE</span>
                  <span className="text-white">{( (sig.fv - sig.mkt)/sig.vol ).toFixed(2)}σ</span>
                </div>
              </div>

              {/* Confidence Meter */}
              <div className="w-full bg-gray-800 h-1 mt-2 rounded-full overflow-hidden">
                <div 
                  className={`h-full ${sig.conf > 0.8 ? 'bg-cyan' : 'bg-amber'}`} 
                  style={{ width: `${sig.conf * 100}%` }}
                />
              </div>
            </motion.div>
          ))}
        </AnimatePresence>
        
        {signals.length === 0 && (
          <div className="text-center text-gray-600 mt-10 italic">
            Waiting for thalamus signals...
          </div>
        )}
      </div>

      {/* SYSTEM LOG CONSOLE (BOTTOM) */}
      <div className="h-1/3 border-t border-gray-700 bg-black p-2 overflow-hidden flex flex-col">
        <div className="flex items-center gap-2 text-gray-500 mb-2 border-b border-gray-800 pb-1">
          <Terminal size={12} />
          <span>SYSTEM.LOG</span>
        </div>
        <div className="flex-1 overflow-y-auto font-mono text-[10px] text-gray-400 space-y-1">
          <div className="text-cyan"> [SYS] Visual Cortex Online...</div>
          <div> [NET] Connected to Corpus Callosum (Redis)</div>
          <div className="text-amber"> [WARN] Latency spike detected in aviation_ingest (140ms)</div>
          <div> [INFO] Maritime module sync complete.</div>
        </div>
      </div>

    </div>
  );
}