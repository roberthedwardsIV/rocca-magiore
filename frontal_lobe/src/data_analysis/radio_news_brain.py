import asyncio
import redis.asyncio as redis
import json
import spacy
import asyncpg
import time
import re
from datetime import datetime

# --- LOAD LIGHTWEIGHT NLP ---
print("[BRAIN] Loading Spacy (Rule-Based)...")
nlp = spacy.load("en_core_web_sm")
print("[BRAIN] Spacy Loaded.")

CATEGORY_MAP = {
    "mine": {"strike": "op", "fire": "op", "collapse": "threat", "profit": "fin"},
    "refinery": {"fire": "op", "leak": "threat", "maintenance": "op", "revenue": "fin"},
    "rail_line": {"derail": "integrity", "blocked": "flow", "delayed": "flow"},
    "maritime_port": {"crane": "equipment", "congestion": "utilization", "strike": "yard"},
    "airport": {"closed": "runway", "delay": "atc", "fuel": "logistics"},
    "highway": {"accident": "navigability", "traffic": "traffic", "blocked": "navigability"},
    "default": {"damage": "integrity", "delay": "flow", "risk": "threat"}
}

class RadioNewsSignalProcessor:
    def __init__(self, db_config):
        self.buffers = {}
        self.db_config = db_config
        self.db_pool = None
        self.context_cache = {} 

    async def initialize(self):
        for _ in range(5):
            try:
                self.db_pool = await asyncpg.create_pool(**self.db_config)
                await self.refresh_context_cache()
                print("[BRAIN] Connected to Hippocampus. Context Loaded.")
                break
            except Exception as e:
                print(f"[DB ERR] Retrying connection: {e}")
                await asyncio.sleep(2)

        # DIAGNOSTIC
        print("[DIAGNOSTIC] Running Startup Self-Test (Deterministic)...")
        test_eq = self.process_text_deterministic("Breaking: A magnitude 7.2 earthquake struck Chile.")
        test_asset = self.process_text_deterministic("Fire reported at El Teniente mine.")
        
        if any(e['label'] == 'earthquake' for e in test_eq) and any(e['text'] == 'el teniente' for e in test_asset):
            print("[DIAGNOSTIC] PASSED. Logic is sound.")
        else:
            print(f"[DIAGNOSTIC] WARNING. Tests Failed: EQ={test_eq}, Asset={test_asset}")

    async def refresh_context_cache(self):
        # 1. HARDCODED FALLBACKS
        self.context_cache["el teniente"] = {"id": 101, "type": "mine", "lat": -34.09, "lon": -70.35}
        self.context_cache["chevron"] = {"id": 102, "type": "refinery", "lat": 37.9, "lon": -122.4}
        self.context_cache["rotterdam"] = {"id": 201, "type": "maritime_port", "lat": 51.9, "lon": 4.5}

        if not self.db_pool: return
        
        async with self.db_pool.acquire() as conn:
            try:
                # Fetch Lat/Lon from Database
                rows = await conn.fetch("SELECT id, name, commodity_types[1], latitude, longitude FROM assets")
                for r in rows:
                    self.context_cache[r['name'].lower()] = {
                        "id": r['id'], 
                        "type": r.get('type', 'mine'),
                        "lat": float(r['latitude']) if r['latitude'] else 0.0,
                        "lon": float(r['longitude']) if r['longitude'] else 0.0
                    } 
            except Exception as e:
                print(f"[CACHE ERR] Assets: {e}")
            
            try:
                rows = await conn.fetch("SELECT id, name, type FROM supply_lines")
                for r in rows:
                    self.context_cache[r['name'].lower()] = {
                        "id": r['id'], 
                        "type": r['type'],
                        "lat": 0.0, 
                        "lon": 0.0
                    }
            except Exception: pass

    async def get_coordinates(self, location_name):
        if not self.db_pool: return (0.0, 0.0)
        try:
            async with self.db_pool.acquire() as conn:
                row = await conn.fetchrow('''
                    SELECT ST_X(coords::geometry) as lon, ST_Y(coords::geometry) as lat 
                    FROM spatial_ref.world_cities 
                    WHERE name ILIKE $1 
                    ORDER BY population DESC LIMIT 1
                ''', location_name)
                return (row['lat'], row['lon']) if row else (0.0, 0.0)
        except: return (0.0, 0.0)

    def determine_category_and_severity(self, entity_type, text_context):
        text = text_context.lower()
        mapping = CATEGORY_MAP.get(entity_type, CATEGORY_MAP["default"])
        best_cat, severity = "none", 0.1
        
        for keyword, cat in mapping.items():
            if keyword in text:
                best_cat = cat
                severity = 0.7 if "severe" in text or "massive" in text else 0.4
                break
        
        if best_cat == "none":
            if entity_type in ["mine", "refinery"]: best_cat = "op"
            else: best_cat = "integrity"
        
        return best_cat, severity

    async def construct_thalamus_signal(self, entities, text_context):
        timestamp = int(time.time() * 1000)
        signal_list = []
        
        # 1. Handle Earthquakes
        eq_entity = next((e for e in entities if e['label'] == 'earthquake'), None)
        if eq_entity:
            loc_name = next((e['text'] for e in entities if e['label'] == 'location'), None)
            
            lat, lon = 0.0, 0.0
            
            if loc_name:
                # A. Try Standard City Lookup
                lat, lon = await self.get_coordinates(loc_name)
                
                # B. CRITICAL FIX: If City Lookup fails (0.0), check Asset Cache (Rolodex)
                if lat == 0.0 and lon == 0.0:
                    asset_info = self.context_cache.get(loc_name.lower())
                    if asset_info:
                        lat = asset_info.get('lat', 0.0)
                        lon = asset_info.get('lon', 0.0)
                        print(f"[BRAIN] Location '{loc_name}' matched to Asset ID {asset_info['id']}. Coords: {lat}, {lon}")

            mag_ent = next((e for e in entities if e['label'] == 'magnitude'), None)
            mag = float(mag_ent['text']) if mag_ent else 5.0

            signal_list.append({
                "entity_id": f"NEWS_EQ_{timestamp}",
                "entity_type": "earthquake",
                "timestamp": timestamp,
                "reliability_noise": 0.8,
                "data": {"lat": lat, "lon": lon, "mag": mag, "mmi": 1.0}
            })

        # 2. Handle Assets
        for ent in entities:
            if ent['label'] in ["location", "earthquake", "magnitude"]: continue
            
            db_id = ent['id']
            e_type = ent['label']
            cat, sev = self.determine_category_and_severity(e_type, text_context)
            
            cached_info = self.context_cache.get(ent['text'].lower(), {})
            asset_lat = cached_info.get("lat", 0.0)
            asset_lon = cached_info.get("lon", 0.0)

            signal_list.append({
                "entity_id": f"NEWS_INFRA_{db_id}_{timestamp}",
                "entity_type": e_type,
                "timestamp": timestamp,
                "reliability_noise": 1.0,
                "data": {
                    "asset_id": db_id,
                    "line_id": db_id,
                    "category": cat,
                    "severity": sev,
                    "reliability": 0.8,
                    "timestamp": timestamp,
                    "lat": asset_lat,
                    "lon": asset_lon
                }
            })

        return signal_list

    def process_text_deterministic(self, text):
        text_lower = text.lower()
        doc = nlp(text)
        entities = []

        # A. ASSET DETECTOR (The "Rolodex") - Run FIRST to prioritize asset names
        # This ensures "El Teniente" is identified as a known entity early
        for name, info in self.context_cache.items():
            if name in text_lower:
                entities.append({
                    "label": info['type'],
                    "text": name,
                    "id": info['id']
                })
                # If we find a known asset, ALSO treat it as a location candidate
                entities.append({"label": "location", "text": name})

        # B. EARTHQUAKE DETECTOR
        if "earthquake" in text_lower or "quake" in text_lower:
            entities.append({"label": "earthquake", "text": "earthquake"})
            mag_match = re.search(r"(?:magnitude|mag)\s*([\d\.]+)", text_lower)
            if mag_match:
                entities.append({"label": "magnitude", "text": mag_match.group(1)})
            
            # Add Spacy entities ONLY if they aren't already found by Rolodex
            # (Simplistic de-duplication)
            existing_locs = {e['text'] for e in entities if e['label'] == 'location'}
            for ent in doc.ents:
                if ent.label_ in ["GPE", "LOC"] and ent.text.lower() not in existing_locs:
                    entities.append({"label": "location", "text": ent.text})

        return entities

    async def handle_message(self, channel, raw_data, redis_client):
        try:
            payload = json.loads(raw_data.decode())
            text_chunk = payload.get("text", "").strip()
            if not text_chunk: return

            ch_name = channel.decode()
            if ch_name not in self.buffers: self.buffers[ch_name] = ""
            self.buffers[ch_name] += " " + text_chunk
            
            if any(mark in text_chunk for mark in [".", "!", "?"]):
                context = self.buffers[ch_name].strip()
                self.buffers[ch_name] = "" 
                
                print(f"[DEBUG] Analyzing: {context[:40]}...")
                entities = self.process_text_deterministic(context)
                print(f"[DEBUG] Found Entities: {len(entities)}")
                
                signals = await self.construct_thalamus_signal(entities, context)
                for sig in signals:
                    print(f"[BRAIN] SIGNAL GENERATED: {sig['entity_type']} -> ID: {sig['data'].get('asset_id', 'EQ')}")
                    await redis_client.lpush("raw_signals", json.dumps(sig))

        except Exception as e:
            print(f"[ERR] Processing Error: {e}")

async def run_processor():
    db_config = {
        'user': 'rocco_admin', 'password': 'REMOVED',
        'database': 'rocco_commodities', 'host': 'hippocampus', 'port': 5432
    }
    
    processor = RadioNewsSignalProcessor(db_config)
    await processor.initialize()
    
    r = await redis.from_url("redis://corpus_callosum:6379")
    pubsub = r.pubsub()
    await pubsub.psubscribe("raw:news:*")
    
    print(f"[{datetime.now()}] Radio Brain Online (Deterministic Mode).")

    async for message in pubsub.listen():
        if message['type'] == 'pmessage':
            asyncio.create_task(processor.handle_message(
                message['channel'], message['data'], r
            ))

if __name__ == "__main__":
    asyncio.run(run_processor())