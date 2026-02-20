import React, { useState } from 'react';
import { motion, AnimatePresence } from 'framer-motion';
import { Activity, Radio, Flame, Waves, Filter, DollarSign, Zap } from 'lucide-react';

const formatTime = (ts) => {
  if (!ts) return "--:--:--";
  return new Date(ts).toLocaleTimeString('en-US', { hour12: false });
};

export default function TapeSidebar({ signals }) {
  const [filter, setFilter] = useState('ALL'); // ALL, EXEC, INTEL, INFRA

  const filteredSignals = signals.filter(sig => {
    if (filter === 'ALL') return true;
    if (filter === 'EXEC') return (sig.sym || sig.symbol);
    if (filter === 'INTEL') return sig.source === 'radio' || sig.source === 'twitter';
    if (filter === 'INFRA') return sig.entity_type === 'earthquake' || sig.entity_type === 'wildfire' || sig.category === 'infrastructure';
    return true;
  });

  return (
    <div className="flex flex-col h-full bg-gunmetal border-l border-gray-800 font-mono text-xs">
      
      {/* 1. STICKY HEADER & FILTERS */}
      <div className="p-3 border-b border-gray-800 bg-void">
        <div className="flex justify-between items-center mb-3">
          <span className="text-cyan font-bold tracking-wider flex items-center gap-2">
            <Activity size={14} /> LIVE FEED
          </span>
          <span className="text-[10px] text-green-500 animate-pulse">● ONLINE</span>
        </div>
        
        <div className="flex gap-1">
          {['ALL', 'EXEC', 'INTEL', 'INFRA'].map(f => (
            <button
              key={f}
              onClick={() => setFilter(f)}
              className={`px-2 py-1 rounded text-[9px] font-bold transition-colors flex-1 
                ${filter === f ? 'bg-cyan text-void' : 'bg-gray-800 text-gray-400 hover:bg-gray-700'}`}
            >
              {f}
            </button>
          ))}
        </div>
      </div>

      {/* 2. THE FEED */}
      <div className="flex-1 overflow-y-auto p-2 space-y-2 scrollbar-thin scrollbar-thumb-gray-700">
        <AnimatePresence initial={false}>
          {filteredSignals.map((sig, i) => {
            const isTrade = sig.sym || sig.symbol;
            const isNews = sig.source === 'radio';
            const isEvent = sig.entity_type === 'earthquake' || sig.entity_type === 'wildfire';

            return (
              <motion.div
                key={sig.id || sig.entity_id || i}
                initial={{ opacity: 0, x: 20 }}
                animate={{ opacity: 1, x: 0 }}
                exit={{ opacity: 0, height: 0 }}
                className={`relative p-3 border rounded border-l-2 transition-all cursor-default
                  ${isTrade ? 'bg-yellow-900/10 border-yellow-600/50 border-l-yellow-500' : ''}
                  ${isNews ? 'bg-blue-900/10 border-blue-600/50 border-l-blue-500' : ''}
                  ${isEvent ? 'bg-red-900/10 border-red-600/50 border-l-red-500' : ''}
                  ${!isTrade && !isNews && !isEvent ? 'bg-gray-800/30 border-gray-700' : ''}
                `}
              >
                {/* EXECUTION CARD */}
                {isTrade && (
                  <div>
                    <div className="flex justify-between text-yellow-500 mb-1">
                      <span className="font-bold">{sig.action} {sig.sym || sig.symbol}</span>
                      <span className="opacity-70">{formatTime(sig.ts || sig.timestamp)}</span>
                    </div>
                    <div className="grid grid-cols-2 gap-2 text-gray-400">
                      <div>Price: <span className="text-white">${sig.mkt?.toFixed(2)}</span></div>
                      <div>Conf: <span className="text-white">{(sig.conf * 100).toFixed(0)}%</span></div>
                    </div>
                  </div>
                )}

                {/* INTEL CARD */}
                {isNews && (
                  <div>
                    <div className="flex justify-between text-blue-400 mb-1">
                      <span className="font-bold flex items-center gap-1"><Radio size={10} /> {sig.station}</span>
                      <span className="opacity-70">{formatTime(Date.now())}</span>
                    </div>
                    <p className="text-gray-300 italic leading-tight">"{sig.text}"</p>
                  </div>
                )}

                {/* EVENT CARD */}
                {isEvent && (
                  <div>
                    <div className="flex justify-between text-red-500 mb-1">
                      <span className="font-bold uppercase flex items-center gap-1">
                        {sig.entity_type === 'earthquake' ? <Waves size={10}/> : <Flame size={10}/>}
                        {sig.entity_type}
                      </span>
                      <span className="opacity-70">{formatTime(sig.timestamp)}</span>
                    </div>
                    <div className="text-gray-300">
                      {sig.entity_type === 'earthquake' ? `Mag: ${sig.data.mag.toFixed(1)}` : `FRP: ${sig.data.frp.toFixed(1)} MW`}
                    </div>
                  </div>
                )}

              </motion.div>
            );
          })}
        </AnimatePresence>
      </div>
    </div>
  );
}