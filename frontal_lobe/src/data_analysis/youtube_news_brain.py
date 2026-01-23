import asyncio
import redis.asyncio as redis
import json
import spacy
from gliner import GLiNER
from datetime import datetime

nlp = spacy.load("en_core_web_sm")
model = GLiNER.from_pretrained("urchade/gliner_medium-v2.1")

TARGET_LABELS = ["earthquake", "mine", "refinery", "port", "location", "magnitude", "intensity"]

class YoutubeNewsSignalProcessor:
    def __init__(self, spatial_index_path=None):
        self.buffers = {}
        # Assuming a local SQLite or GeoPackage for sub-millisecond coordinate lookups
        self.spatial_index_path = spatial_index_path

    def get_local_coordinates(self, location_name):
        """
        High-frequency local lookup. 
        In production, this queries a local KV store or SQLite R-Tree.
        """
        # Placeholder for your local fast-lookup logic (e.g., DuckDB or SQLite)
        # return db.execute("SELECT lat, lon FROM world_cities WHERE name = ?", (location_name,))
        return [0.0, 0.0] # Logic replaced by your local high-speed index

    def parse_signal_protocol(self, text, entities):
        """
        Extracts specific metrics based on the detected event/asset type.
        """
        signal = {
            "source_timestamp": datetime.utcnow().isoformat(),
            "event_type": None,
            "detected_assets": [],
            "spatial": {"name": None, "coords": None},
            "metrics": {}
        }

        # 1. Primary Classification
        for ent in entities:
            lbl, val = ent['label'], ent['text']
            
            if lbl in ["earthquake", "hurricane", "military_conflict"]:
                signal["event_type"] = lbl
            elif lbl in ["mine", "refinery", "port"]:
                signal["detected_assets"].append({"type": lbl, "name": val})
            elif lbl == "location":
                signal["spatial"]["name"] = val

        # 2. Event-Specific Protocols (Magnitude, Intensity, etc.)
        if signal["event_type"] == "earthquake":
            mags = [e['text'] for e in entities if e['label'] == "magnitude"]
            if mags: signal["metrics"]["magnitude"] = mags[0]

        # 3. Asset-Specific Protocols (Damage Assessment)
        if signal["detected_assets"]:
            damages = [e['text'] for e in entities if e['label'] == "damage_extent"]
            if damages: signal["metrics"]["damage_severity"] = damages[0]

        # 4. High-Speed Local Geocoding
        if signal["spatial"]["name"]:
            signal["spatial"]["coords"] = self.get_local_coordinates(signal["spatial"]["name"])

        return signal

    async def handle_message(self, channel, data, redis_client):
        ch_name = channel.decode()
        fragment = data.decode().strip()
        
        # Buffer fragments into coherent sentences
        if ch_name not in self.buffers: self.buffers[ch_name] = ""
        self.buffers[ch_name] += f" {fragment}"
        
        # Process once context is sufficient (sentence boundary)
        if any(mark in fragment for mark in [".", "!", "?"]):
            context = self.buffers[ch_name].strip()
            self.buffers[ch_name] = ""
            
            # Execute NLP in thread pool to prevent blocking the Redis consumer
            loop = asyncio.get_running_loop()
            signal = await loop.run_in_executor(None, self.process_text_sync, context)
            
            if signal["event_type"] or signal["detected_assets"]:
                # Post the signal back to the raw_signals bus
                await redis_client.publish("raw_signals", json.dumps(signal))

    def process_text_sync(self, text):
        entities = model.predict_entities(text, TARGET_LABELS, threshold=0.45)
        return self.parse_signal_protocol(text, entities)

async def run_processor():
    processor = YoutubeNewsSignalProcessor()
    # Using your specific corpus_collosum host
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