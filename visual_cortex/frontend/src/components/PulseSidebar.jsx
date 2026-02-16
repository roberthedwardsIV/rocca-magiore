import React from 'react';
import { Server, Database, Radio, Globe, Zap } from 'lucide-react';

const StatusRow = ({ label, icon: Icon, active }) => (
  <div className="flex items-center justify-between p-2 hover:bg-white/5 rounded transition-colors cursor-default">
    <div className="flex items-center gap-3">
      <Icon size={14} className={active ? "text-cyan" : "text-gray-600"} />
      <span className={active ? "text-gray-300" : "text-gray-600"}>{label}</span>
    </div>
    <div className={`w-1.5 h-1.5 rounded-full ${active ? "bg-cyan shadow-[0_0_8px_rgba(0,242,234,0.8)]" : "bg-gray-700"}`} />
  </div>
);

export default function PulseSidebar({ assets }) {
  // Aggregate stats from the assets prop
  const totalAssets = assets.length;
  const criticalThreats = assets.filter(a => a.threat_level > 0.5).length;
  const avgHealth = assets.reduce((acc, curr) => acc + (curr.op_health || 0), 0) / (totalAssets || 1);

  return (
    <div className="h-full flex flex-col p-4 space-y-6 font-mono text-xs">
      
      {/* BRAND */}
      <div className="pb-4 border-b border-gray-800">
        <h1 className="text-xl font-bold tracking-widest text-white">ROCCO<span className="text-cyan">MAGGIORE</span></h1>
        <p className="text-[10px] text-gray-500 mt-1">QUANTITATIVE LOGISTICS ENGINE</p>
      </div>

      {/* PNL CARD */}
      <div className="bg-void border border-gray-800 p-4 rounded-lg">
        <span className="text-gray-500 block mb-1">DAILY PnL (PAPER)</span>
        <div className="text-2xl font-bold text-cyan flex items-baseline gap-2">
          +$14,203.50
          <span className="text-xs font-normal text-green-500">(+1.2%)</span>
        </div>
        <div className="mt-3 h-1 w-full bg-gray-800 rounded-full overflow-hidden">
          <div className="h-full bg-gradient-to-r from-cyan to-blue-600 w-[65%]" />
        </div>
        <div className="flex justify-between mt-2 text-[10px] text-gray-400">
          <span>Risk Util: 65%</span>
          <span>Max: $1.0M</span>
        </div>
      </div>

      {/* SYSTEM HEALTH */}
      <div>
        <h3 className="text-gray-500 mb-2 uppercase tracking-wider text-[10px]">System Status</h3>
        <div className="space-y-1">
          <StatusRow label="Thalamus Core" icon={Zap} active={true} />
          <StatusRow label="Hippocampus DB" icon={Database} active={true} />
          <StatusRow label="Sensory Arrays" icon={Radio} active={true} />
          <StatusRow label="Global Network" icon={Globe} active={true} />
          <StatusRow label="Brainstem Exec" icon={Server} active={true} />
        </div>
      </div>

      {/* ASSET SUMMARY */}
      <div className="flex-1">
        <h3 className="text-gray-500 mb-2 uppercase tracking-wider text-[10px]">Asset Watch</h3>
        <div className="grid grid-cols-2 gap-2 mb-4">
          <div className="bg-void p-2 rounded border border-gray-800">
            <span className="block text-2xl font-bold text-white">{totalAssets}</span>
            <span className="text-[9px] text-gray-500">TRACKED ASSETS</span>
          </div>
          <div className="bg-void p-2 rounded border border-gray-800">
            <span className="block text-2xl font-bold text-crimson">{criticalThreats}</span>
            <span className="text-[9px] text-gray-500">CRITICAL THREATS</span>
          </div>
        </div>
        
        <div className="bg-void p-2 rounded border border-gray-800">
          <div className="flex justify-between mb-1">
            <span className="text-gray-400">Global Health</span>
            <span className="text-cyan">{(avgHealth * 100).toFixed(1)}%</span>
          </div>
          <div className="w-full bg-gray-800 h-1.5 rounded-full overflow-hidden">
            <div 
              className="h-full bg-cyan transition-all duration-1000" 
              style={{ width: `${avgHealth * 100}%` }}
            />
          </div>
        </div>
      </div>

      <div className="text-[9px] text-gray-600 text-center">
        v1.0.4-alpha | LAT: 24ms | MEM: 42%
      </div>

    </div>
  );
}