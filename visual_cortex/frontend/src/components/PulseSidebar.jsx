import React from 'react';
import { Activity, AlertOctagon, TrendingUp, TrendingDown, DollarSign, Database } from 'lucide-react';

export default function PulseSidebar({ data, assets }) {
  // Metric: Aggregate Threat Level (0-5) based on asset health
  const threatLevel = assets 
    ? assets.filter(a => a.op_health < 0.8).length 
    : 0;

  return (
    <div className="h-full flex flex-col bg-void border-r border-gray-800 font-mono text-xs overflow-hidden">
      
      {/* 1. HEADER & KPI */}
      <div className="p-4 border-b border-gray-800 bg-gunmetal/20">
        <h1 className="text-lg font-serif font-bold text-parchment tracking-widest mb-4">ROCCO MAGGIORE</h1>
        
        <div className="grid grid-cols-2 gap-3">
          <div className="p-2 bg-nautical/30 border border-nautical rounded">
            <span className="text-[10px] text-cyan/70 block mb-1">DAILY P&L</span>
            <div className={`text-xl font-bold flex items-center gap-1 ${data.pnl >= 0 ? 'text-green-400' : 'text-crimson'}`}>
              {data.pnl >= 0 ? <TrendingUp size={14}/> : <TrendingDown size={14}/>}
              ${Math.abs(data.pnl).toLocaleString()}
            </div>
          </div>
          
          <div className="p-2 bg-crimson/10 border border-crimson/30 rounded">
            <span className="text-[10px] text-red-400/70 block mb-1">THREAT LEVEL</span>
            <div className="text-xl font-bold text-red-500 flex items-center gap-2">
              <AlertOctagon size={16} />
              DEFCON {Math.max(1, 5 - threatLevel)}
            </div>
          </div>
        </div>
      </div>

      {/* 2. PHYSICAL ASSET MATRIX (The "Slow" Variable) */}
      <div className="flex-1 overflow-y-auto scrollbar-thin scrollbar-thumb-gray-800 p-3 space-y-4">
        <div>
          <h3 className="text-gray-500 text-[10px] uppercase tracking-widest mb-2 flex items-center gap-2">
            <Database size={10} /> Physical Assets
          </h3>
          <div className="space-y-1">
            {(assets || []).slice().sort((a,b) => a.op_health - b.op_health).map(asset => {
              const healthPct = Math.round((asset.op_health || 0) * 100);
              const color = healthPct > 90 ? 'bg-green-500' : healthPct > 60 ? 'bg-yellow-500' : 'bg-red-500';
              
              return (
                <div key={asset.id} className="group flex flex-col p-2 hover:bg-white/5 rounded transition-colors cursor-pointer border border-transparent hover:border-gray-700">
                  <div className="flex justify-between items-center mb-1">
                    <span className="text-gray-300 font-bold">{asset.name || 'Unknown'}</span>
                    <span className={`${healthPct < 100 ? 'text-red-400' : 'text-gray-600'} text-[10px]`}>
                      {healthPct}% OP
                    </span>
                  </div>
                  {/* Health Bar */}
                  <div className="h-1 w-full bg-gray-800 rounded-full overflow-hidden">
                    <div className={`h-full ${color} transition-all duration-500`} style={{ width: `${healthPct}%` }}></div>
                  </div>
                  <div className="flex justify-between mt-1 text-[9px] text-gray-600 group-hover:text-gray-400">
                    <span>{asset.commodity || 'Unknown'}</span>
                    <span>{asset.type || 'asset'}</span>
                  </div>
                </div>
              )
            })}
          </div>
        </div>
      </div>

      {/* 3. ACTIVE TICKERS (The "Fast" Variable) */}
      <div className="h-1/3 border-t border-gray-800 p-3 overflow-y-auto">
        <h3 className="text-gray-500 text-[10px] uppercase tracking-widest mb-2 flex items-center gap-2">
          <Activity size={10} /> Active Markets
        </h3>
        <table className="w-full text-left border-collapse">
          <thead>
            <tr className="text-[9px] text-gray-600 border-b border-gray-800">
              <th className="pb-1">SYM</th>
              <th className="pb-1 text-right">PRICE</th>
              <th className="pb-1 text-right">VOL</th>
            </tr>
          </thead>
          <tbody>
            {(data?.tickers || []).map(t => (
              <tr key={t.symbol} className="border-b border-gray-800/50 hover:bg-white/5">
                <td className="py-2 text-cyan font-bold">{t.symbol}</td>
                <td className="py-2 text-right text-parchment">${(t.price || 0).toFixed(2)}</td>
                <td className="py-2 text-right text-gray-500">{((t.vol || 0) * 100).toFixed(1)}%</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>

    </div>
  );
}