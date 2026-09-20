import redis
import json
import psycopg2
import time
from datetime import datetime
from utils.db_config import db_config

# --- CONFIG ---
DB_CONFIG = db_config()

# Redis: db=0 is the standard bus for raw_signals
r_in = redis.Redis(host='corpus_callosum', port=6379, db=0)
r_out = redis.Redis(host='corpus_callosum', port=6379, db=0)

def get_db_connection():
    try:
        conn = psycopg2.connect(**DB_CONFIG)
        # Enable autocommit for read-only speed
        conn.autocommit = True
        return conn
    except Exception as e:
        print(f"[WILDFIRE] DB Connect Error: {e}", flush=True)
        return None

def is_industrial_flare(temp_k, frp):
    """
    Physics Heuristic:
    - Gas flares are extremely hot (>500K) but small area (Low FRP).
    - Wildfires are cooler (<400K avg) but massive area (High FRP).
    """
    if temp_k > 450 and frp < 15:
        return True # Likely a stack/chimney
    return False

def check_spatial_impact(cur, lat, lon):
    """
    Uses PostGIS to check if fire is within critical radius of assets.
    """
    # 1. CHECK ASSETS (50km Radius)
    # 0.5 degrees approx 55km. Using ST_DWithin is faster than calculating exact distance for everything.
    cur.execute("""
        SELECT id, name, commodity_types[1] as type,
               ST_Distance(geom::geography, ST_SetSRID(ST_MakePoint(%s, %s), 4326)::geography) / 1000.0 as dist_km
        FROM assets 
        WHERE ST_DWithin(geom, ST_SetSRID(ST_MakePoint(%s, %s), 4326), 0.5)
        ORDER BY dist_km ASC LIMIT 1
    """, (lon, lat, lon, lat))
    
    asset = cur.fetchone()
    if asset:
        return {
            "target_type": "asset",
            "target_id": asset[0],
            "target_name": asset[1],
            "dist_km": float(asset[3])
        }

    # 2. CHECK SUPPLY LINES (20km Radius)
    # Tighter radius for lines (0.2 deg ~ 22km)
    cur.execute("""
        SELECT id, name, type,
               ST_Distance(geom::geography, ST_SetSRID(ST_MakePoint(%s, %s), 4326)::geography) / 1000.0 as dist_km
        FROM supply_lines 
        WHERE ST_DWithin(geom, ST_SetSRID(ST_MakePoint(%s, %s), 4326), 0.2)
        ORDER BY dist_km ASC LIMIT 1
    """, (lon, lat, lon, lat))

    line = cur.fetchone()
    if line:
        return {
            "target_type": "supply_line",
            "target_id": line[0],
            "target_name": line[1], # Supply lines use 'type' as subclass, name as ID string usually
            "dist_km": float(line[3])
        }

    return None

def run_brain():
    print("[WILDFIRE] Spatial Logic Engine Online.", flush=True)
    
    conn = get_db_connection()
    if not conn: return
    cur = conn.cursor()

    while True:
        # Blocking Pop: Waits here until C++ ingestor pushes data
        _, packet = r_in.brpop("fire_stream_buffer")
        
        try:
            fire = json.loads(packet)
            lat = fire['lat']
            lon = fire['lon']
            frp = fire['frp']
            temp_k = fire['temp_k']

            # GATE 1: Flare Filter
            if is_industrial_flare(temp_k, frp):
                # print(f"[WILDFIRE] Filtered flare at {lat}, {lon}", flush=True)
                continue

            # GATE 2: Spatial Intersection
            impact = check_spatial_impact(cur, lat, lon)
            
            if impact:
                print(f"[WILDFIRE] THREAT: {impact['target_name']} is {impact['dist_km']:.1f}km from fire (FRP: {frp})", flush=True)
                
                # GATE 3: Generate Thalamus Signal
                # Entity ID combines target + rough time to group updates
                # We round time to nearest hour to group multiple satellite pixels into one "Event"
                time_bucket = int(time.time() / 3600) 
                
                signal = {
                    "entity_id": f"FIRE_{impact['target_type']}_{impact['target_id']}_{time_bucket}",
                    "entity_type": "wildfire",
                    "timestamp": int(time.time() * 1000),
                    "reliability_noise": 0.9, # NASA/VIIRS is high confidence
                    "data": {
                        "lat": lat,
                        "lon": lon,
                        "frp": frp,
                        "dist_km": impact['dist_km'],
                        "target_id": impact['target_id'],
                        "target_type": impact['target_type']
                    }
                }
                
                r_out.lpush("raw_signals", json.dumps(signal))
                r_out.publish("raw_signals", json.dumps(signal))

        except psycopg2.OperationalError:
            print("[WILDFIRE] DB Connection lost. Reconnecting...", flush=True)
            conn = get_db_connection()
            cur = conn.cursor()
        except Exception as e:
            print(f"[WILDFIRE] Processing Error: {e}", flush=True)

if __name__ == "__main__":
    run_brain()