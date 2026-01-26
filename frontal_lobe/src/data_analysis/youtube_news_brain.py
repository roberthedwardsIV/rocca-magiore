import asyncio
import redis.asyncio as redis
import json
import spacy
import asyncpg
from gliner import GLiNER
from datetime import datetime

nlp = spacy.load("en_core_web_sm")
model = GLiNER.from_pretrained("urchade/gliner_medium-v2.1")

TARGET_LABELS = ["earthquake", "military_conflict", "mine", "refinery", "port", "location", "magnitude", "intensity", "damage_extent"]

class YoutubeNewsSignalProcessor:
    def __init__(self, db_config):
        self.buffers = {}
        self.db_config = db_config
        self.db_pool = None

    async def initialize(self):
        self.db_pool = await asyncpg.create_pool(**self.db_config)

    async def get_local_coordinates(self, location_name):
        if not self.db_pool:
            return None
        async with self.db_pool.acquire() as conn:
            row = await conn.fetchrow('''
                SELECT ST_X(coords::geometry) as lon, ST_Y(coords::geometry) as lat 
                FROM spatial_ref.world_cities 
                WHERE name = $1 
                ORDER BY population DESC LIMIT 1
            ''', location_name)
            return [row['lat'], row['lon']] if row else None

    def parse_signal_protocol(self, entities):
        signal = {
            "source_timestamp": datetime.utcnow().isoformat(),
            "event_type": None,
            "detected_assets": [],
            "spatial": {"name": None, "coords": None},
            "metrics": {}
        }

        for ent in entities:
            lbl, val = ent['label'], ent['text']
            if lbl in ["earthquake", "hurricane", "military_conflict"]:
                signal["event_type"] = lbl
            elif lbl in ["mine", "refinery", "port"]:
                signal["detected_assets"].append({"type": lbl, "name": val})
            elif lbl == "location":
                signal["spatial"]["name"] = val

        if signal["event_type"] == "earthquake":
            mags = [e['text'] for e in entities if e['label'] == "magnitude"]
            if mags: signal["metrics"]["magnitude"] = mags[0]
            ints = [e['text'] for e in entities if e['label'] == "intensity"]
            if ints: signal["metrics"]["intensity"] = ints[0]

        if signal["detected_assets"]:
            damages = [e['text'] for e in entities if e['label'] == "damage_extent"]
            if damages: signal["metrics"]["damage_severity"] = damages[0]

        return signal

    async def handle_message(self, channel, data, redis_client):
        ch_name = channel.decode()
        fragment = data.decode().strip()
        
        if ch_name not in self.buffers: self.buffers[ch_name] = ""
        self.buffers[ch_name] += f" {fragment}"
        
        if any(mark in fragment for mark in [".", "!", "?"]):
            context = self.buffers[ch_name].strip()
            self.buffers[ch_name] = ""
            
            loop = asyncio.get_running_loop()
            entities = await loop.run_in_executor(None, self.process_text_sync, context)
            signal = self.parse_signal_protocol(entities)
            
            if signal["event_type"] or signal["detected_assets"]:
                if signal["spatial"]["name"]:
                    signal["spatial"]["coords"] = await self.get_local_coordinates(signal["spatial"]["name"])
                await redis_client.publish("raw_signals", json.dumps(signal))

    def process_text_sync(self, text):
        return model.predict_entities(text, TARGET_LABELS, threshold=0.45)

async def run_processor():
    db_config = {
        'user': 'postgres',
        'password': 'your_password',
        'database': 'SeismicMonitor',
        'host': 'your_postgres_host'
    }
    
    processor = YoutubeNewsSignalProcessor(db_config)
    await processor.initialize()
    
    r = await redis.from_url("redis://corpus_collosum:6379")
    pubsub = r.pubsub()
    await pubsub.psubscribe("raw:news:*")
    
    print(f"[{datetime.now()}] YT News Signal Processor Online...")

    async for message in pubsub.listen():
        if message['type'] == 'pmessage':
            asyncio.create_task(processor.handle_message(
                message['channel'], message['data'], r
            ))

if __name__ == "__main__":
    asyncio.run(run_processor())