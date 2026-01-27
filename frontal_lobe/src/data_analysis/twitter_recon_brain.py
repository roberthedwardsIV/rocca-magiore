import redis
import json
import time
import math

# Keywords for intensity
INTENSITY_KEYWORDS = {
    "felt": 2, "shaking": 3, "woke": 4, "strong": 5, 
    "violent": 7, "damage": 8, "collapse": 10
}

def analyze_earthquake_mmi(posts):
    score = 0
    hits = 0
    for p in posts:
        text = p.get('record', {}).get('text', '').lower()
        hits += 1
        for word, weight in INTENSITY_KEYWORDS.items():
            if word in text:
                score += weight
        
    if hits == 0: return None
    
    # Calculate MMI
    avg = score / hits
    vol_bonus = min(3.0, math.log(hits + 1))
    mmi = round(min(12.0, avg + vol_bonus), 1) # Max MMI is 12
    
    # Calculate Reliability (More tweets = higher trust)
    reliability = min(0.95, 0.5 + (math.log(hits + 1) / 10))
    
    return mmi, reliability

def start_twitter_brain():
    r = redis.Redis(host='corpus_callosum', port=6379)
    print("[TWITTER BRAIN] Online...", flush=True)

    while True:
        _, data = r.brpop("twitter_stream_buffer")
        
        try:
            packet = json.loads(data)
            task_id = packet['task_id'] # This is the Original ID (e.g., EQ_2026_01)
            event_type = packet['type']
            posts = packet['raw_data'].get('posts', [])
            
            if not posts: continue

            final_signal = None

            # --- DYNAMIC ANALYSIS ---
            if event_type == "earthquake":
                result = analyze_earthquake_mmi(posts)
                if result:
                    mmi, reliability = result
                    
                    # STRUCTURE: Matches standard earthquake signal
                    final_signal = {
                        "entity_id": task_id,       # MERGE TARGET: Original ID
                        "entity_type": "earthquake",
                        "timestamp": int(time.time() * 1000),
                        "reliability_noise": reliability,
                        "data": {
                            # "mag" is omitted (social can't measure magnitude)
                            "mmi": mmi,             # Social MMI (Update Only)
                            # Lat/Lon omitted to avoid shifting epicenter based on tweet location
                        }
                    }

            elif event_type == "wildfire":
                # Placeholder for future logic
                pass

            # --- PUBLISH MERGE REQUEST ---
            if final_signal:
                print(f"[BRAIN] Verified {task_id}. MMI: {final_signal['data']['mmi']}")
                r.publish("raw_signals", json.dumps(final_signal))

        except Exception as e:
            print(f"[ERR] {e}")

if __name__ == "__main__":
    start_twitter_brain()