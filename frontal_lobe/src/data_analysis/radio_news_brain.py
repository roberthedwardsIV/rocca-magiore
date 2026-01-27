import asyncio
import redis.asyncio as redis
import json
import spacy
import asyncpg
import time
import random
from gliner import GLiNER
from datetime import datetime

# --- CONFIGURATION ---
# NLP Model for Entity Extraction
model = GLiNER.from_pretrained("urchade/gliner_medium-v2.1")

# [cite_start]Mapped to match C++ Dispatcher.cpp types [cite: 113-114]
TARGET_LABELS = [
    # Events
    "earthquake", "tsunami", 
    # Assets (Matches assets/MineAsset.cpp, RefineryAsset.cpp)
    "mine", "refinery",
    # Supply Lines (Matches supply_lines/*.cpp)
    "rail_line", "rail_yard", "maritime_route", "maritime_port",
    "pipeline_line", "pipeline_station", "highway", "airport",
    "airspace", "canal_route", "canal_lock",
    # Metrics
    "magnitude", "intensity", "location"
]

# Keywords to map text context to C++ internal categories (e.g. "integrity", "op", "flow")
CATEGORY_MAP = {
    "mine": {"strike": "op", "fire": "op", "collapse": "threat", "profit": "fin"},
    "refinery": {"fire": "op", "leak": "threat", "maintenance": "op", "revenue": "fin"},
    "rail_line": {"derail": "integrity", "blocked": "flow", "delayed": "flow"},
    "maritime_port": {"crane": "equipment", "congestion": "utilization", "strike": "yard"},
    "airport": {"closed": "runway", "delay": "atc", "fuel": "logistics"},
    "highway": {"accident": "navigability", "traffic": "traffic", "blocked": "navigability"},
    "canal_route": {"stuck": "navigability", "grounded": "depth"},
    "default": {"damage": "integrity", "delay": "flow", "risk": "threat"}
}

class RadioNewsSignalProcessor:
    def __init__(self, db_config):
        self.buffers = {}
        self.db_config = db_config
        self.db_pool = None
        
        # Cache for ID resolution (Name -> DB_ID)
        self.asset_cache = {} 
        self.line_cache = {}

    async def initialize(self):
        """Connects to DB and loads Infrastructure Context"""
        for _ in range(5):
            try:
                self.db_pool = await asyncpg.create_pool(**self.db_config)
                await self.refresh_context_cache()
                print("[BRAIN] Connected to Hippocampus. Context Loaded.")
                return
            except Exception as e:
                print(f"[DB ERR] Retrying connection: {e}")
                await asyncio.sleep(2)

    async def refresh_context_cache(self):
        """
        Loads all known assets/lines from DB so we can map news names 
        (e.g. 'Rotterdam Port') to IDs (e.g. 402) for the C++ engine.
        """
        async with self.db_pool.acquire() as conn:
            # Load Assets
            rows = await conn.fetch("SELECT id, name, commodity_types FROM assets")
            for r in rows:
                self.asset_cache[r['name'].lower()] = r['id']
            
            # Load Supply Lines (simplified schema assumption based on context)
            # You might need to adjust table names if they differ in your SQL schema
            try:
                rows = await conn.fetch("SELECT id, name, type FROM supply_lines")
                for r in rows:
                    self.line_cache[r['name'].lower()] = {'id': r['id'], 'type': r['type']}
            except:
                pass # Supply table might be split, ignoring for safety if table missing

    async def get_coordinates(self, location_name):
        """Resolves city/location names to Lat/Lon"""
        async with self.db_pool.acquire() as conn:
            try:
                row = await conn.fetchrow('''
                    SELECT ST_X(coords::geometry) as lon, ST_Y(coords::geometry) as lat 
                    FROM spatial_ref.world_cities 
                    WHERE name ILIKE $1 
                    ORDER BY population DESC LIMIT 1
                ''', location_name)
                return (row['lat'], row['lon']) if row else (0.0, 0.0)
            except:
                return (0.0, 0.0)

    def determine_category_and_severity(self, entity_type, text_context):
        """
        Scans text for keywords to assign the correct C++ packet category.
        Returns (category, severity)
        """
        text = text_context.lower()
        mapping = CATEGORY_MAP.get(entity_type, CATEGORY_MAP["default"])
        
        best_cat = "none"
        severity = 0.0
        
        for keyword, cat in mapping.items():
            if keyword in text:
                best_cat = cat
                # Simple sentiment heuristic: modifiers boost severity
                severity = 0.7 if "severe" in text or "massive" in text else 0.4
                break
        
        # Default fallback if no keywords found but entity detected
        if best_cat == "none":
            if entity_type in ["mine", "refinery"]: best_cat = "op"
            else: best_cat = "integrity"
            severity = 0.1

        return best_cat, severity

    async def construct_thalamus_signal(self, entities, text_context):
        """
        Constructs the EXACT JSON format required by Thalamus C++ Dispatcher.
        """
        timestamp = int(time.time() * 1000)
        signal_list = []

        # 1. Parse Entities
        extracted = {e['label']: e['text'] for e in entities}
        
        # --- CASE A: EARTHQUAKE (Matches EarthquakeTracker.cpp) ---
        if "earthquake" in extracted:
            lat, lon = 0.0, 0.0
            if "location" in extracted:
                lat, lon = await self.get_coordinates(extracted["location"])
            
            try:
                mag = float(extracted.get("magnitude", "0").replace("M", ""))
            except: mag = 5.0

            # [cite_start]EXACT FORMAT required by EarthquakeTracker.cpp [cite: 231]
            payload = {
                "entity_id": f"NEWS_EQ_{timestamp}",
                "entity_type": "earthquake",
                "timestamp": timestamp,
                "reliability_noise": 0.8, # News is fairly reliable, low noise
                "data": {
                    "lat": lat,
                    "lon": lon,
                    "mag": mag,
                    "mmi": 1.0 # Default if unknown
                }
            }
            signal_list.append(payload)

        # --- CASE B: ASSETS (Matches Dispatcher.cpp / BaseAsset.cpp) ---
        for label, name in extracted.items():
            if label in ["mine", "refinery"]:
                # Try to find ID in cache
                db_id = -1
                for cache_name, cache_id in self.asset_cache.items():
                    if name.lower() in cache_name:
                        db_id = cache_id
                        break
                
                if db_id != -1:
                    cat, sev = self.determine_category_and_severity(label, text_context)
                    
                    # [cite_start]EXACT FORMAT required by Dispatcher.cpp [cite: 126]
                    payload = {
                        "entity_id": f"NEWS_ASSET_{db_id}_{timestamp}",
                        "entity_type": label,
                        "timestamp": timestamp,
                        "reliability_noise": 1.0, 
                        "data": {
                            "asset_id": db_id,
                            "category": cat,
                            "severity": sev,
                            "reliability": 0.7,
                            "timestamp": timestamp
                        }
                    }
                    signal_list.append(payload)

        # --- CASE C: SUPPLY LINES (Matches Dispatcher.cpp / BaseSupplyLine.cpp) ---
            elif label in ["rail_line", "maritime_port", "highway", "airport"]:
                # Try to find ID in cache
                db_id = -1
                for cache_name, info in self.line_cache.items():
                    if name.lower() in cache_name:
                        db_id = info['id']
                        break
                
                if db_id != -1:
                    cat, sev = self.determine_category_and_severity(label, text_context)

                    # [cite_start]EXACT FORMAT required by Dispatcher.cpp [cite: 128]
                    payload = {
                        "entity_id": f"NEWS_LINE_{db_id}_{timestamp}",
                        "entity_type": label,
                        "timestamp": timestamp,
                        "reliability_noise": 1.0,
                        "data": {
                            "line_id": db_id,
                            "category": cat,
                            "severity": sev,
                            "reliability": 0.7,
                            "timestamp": timestamp
                        }
                    }
                    signal_list.append(payload)

        return signal_list

    async def handle_message(self, channel, raw_data, redis_client):
        try:
            # Decode Radio Engine JSON
            payload = json.loads(raw_data.decode())
            text_chunk = payload.get("text", "").strip()
            print(f"[DEBUG] Heard on {channel}: {text_chunk}")
            if not text_chunk: return

            ch_name = channel.decode()
            if ch_name not in self.buffers: self.buffers[ch_name] = ""
            
            self.buffers[ch_name] += " " + text_chunk
            
            # Sentence Boundary Detection
            if any(mark in text_chunk for mark in [".", "!", "?"]):
                context = self.buffers[ch_name].strip()
                self.buffers[ch_name] = "" 
                print(f"[DEBUG] Triggering AI Model on: {context[:30]}...")
                # GLiNER Inference
                loop = asyncio.get_running_loop()
                entities = await loop.run_in_executor(None, self.process_text_sync, context)
                print(f"[DEBUG] AI Finished. Found: {len(entities)} entities.")
                # Generate Thalamus-Compatible Signals
                signals = await self.construct_thalamus_signal(entities, context)
                
                for sig in signals:
                    print(f"[BRAIN] Generated Signal: {sig['entity_type']} -> {sig.get('data')}")
                    await redis_client.lpush("raw_signals", json.dumps(sig))

        except Exception as e:
            print(f"[ERR] Processing Error: {e}")

    def process_text_sync(self, text):
        return model.predict_entities(text, TARGET_LABELS, threshold=0.45)

async def run_processor():
    db_config = {
        'user': 'rocco_admin',
        'password': 'REMOVED',
        'database': 'rocco_commodities',
        'host': 'hippocampus',
        'port': 5432
    }
    
    processor = RadioNewsSignalProcessor(db_config)
    await processor.initialize()
    
    r = await redis.from_url("redis://corpus_callosum:6379")
    pubsub = r.pubsub()
    await pubsub.psubscribe("raw:news:*")
    
    print(f"[{datetime.now()}] Radio Brain Online. Integrating Signals...")

    async for message in pubsub.listen():
        if message['type'] == 'pmessage':
            asyncio.create_task(processor.handle_message(
                message['channel'], message['data'], r
            ))

if __name__ == "__main__":
    asyncio.run(run_processor())