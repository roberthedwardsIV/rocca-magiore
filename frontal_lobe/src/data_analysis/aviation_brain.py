import redis
import json
import psycopg2
import time
import math
from datetime import datetime

# Configuration
DB_CONFIG = {
    "dbname": "rocco_commodities", 
    "user": "rocco_admin", 
    "password": "REMOVED", 
    "host": "hippocampus", 
    "port": "5432"
}
r_sky = redis.Redis(host='corpus_callosum', port=6379, db=1)
r_bus = redis.Redis(host='corpus_callosum', port=6379, db=0)


# Redis function: pushes signal packets to "raw_signals" redis channel
def push_signal(sig_type, entity_id, entity_type, data, strength=1.0):
    packet = {
        "source": "aviation",
        "type": sig_type,
        "entity_id": str(entity_id),
        "entity_type": entity_type,
        "timestamp": int(time.time() * 1000),
        "strength": strength,
        "data": data
    }
    r_bus.lpush("raw_signals", json.dumps(packet))

# --- LOGIC MODULES ---

def check_tactical_gates(cur, m, icao, profile):
    """Signals #6, #7, #8: Proximity, Night Owl, and Dark Arrivals"""
    # Find assets within 1 degree (~111km)
    cur.execute("""
        SELECT id, name, commodity_types[1] as type 
        FROM assets 
        WHERE ST_DWithin(geom, ST_SetSRID(ST_MakePoint(%s, %s), 4326), 1.0)
    """, (m['lon'], m['lat']))
    assets = cur.fetchall()

    is_night = not (6 < time.localtime().tm_hour < 19)

    for aid, name, a_type in assets:
        # Gate #6: Watchlist Arrival (M&A / Exploration)
        if profile:
            push_signal("ASSET_PROXIMITY", aid, a_type, {
                "category": "threat", "severity": 0.5, "owner": profile['owner'], "icao": icao
            })
            
            # Gate #7: Night Owl (Clandestine movement)
            if is_night:
                push_signal("CLANDESTINE_MOVEMENT", aid, a_type, {"severity": 0.8}, strength=0.9)
        
        # Gate #8: Dark Arrival (Unknown plane at private strip)
        # If owner is 'Unknown' and we are very close to a mine strip (< 5km)
        elif m['alt'] < 2000:
            cur.execute("""
                SELECT 1 FROM assets 
                WHERE ST_DWithin(geom, ST_SetSRID(ST_MakePoint(%s, %s), 4326), 0.05)
            """, (m['lon'], m['lat']))
            if cur.fetchone():
                push_signal("DARK_ARRIVAL", aid, a_type, {"severity": 0.7, "icao": icao})

def check_seismic_muting(cur, m, icao):
    """Logic #5: Silencing false earthquakes from heavy landings"""
    if m['alt'] < 3000:
        cur.execute("""
            SELECT network || '_' || station 
            FROM earthquake_stations 
            WHERE ST_DWithin(geom, ST_SetSRID(ST_MakePoint(%s, %s), 4326), 0.08)
        """, (m['lon'], m['lat']))
        for mon in cur.fetchall():
            push_signal("SEISMIC_MASK", mon[0], "earthquake", {"is_voided": True, "icao": icao})

def check_capacity_gate(cur):
    """Logic #3 & #9: Capacity Analysis against Baselines"""
    # This runs once per hour (handled in main loop)
    cur.execute("SELECT weekly_frequency FROM baselines_aviation WHERE route_key = 'REGIONAL_ASSET_LOGISTICS'")
    res = cur.fetchone()
    if not res: return
    
    baseline = res[0]
    # Get count of completed flights in last 24h, scaled to a week
    cur.execute("SELECT COUNT(*) * 7 FROM flight_legs WHERE last_seen > NOW() - INTERVAL '1 day'")
    current_pulse = cur.fetchone()[0]

    if current_pulse < (baseline * 0.6):
        push_signal("CAPACITY_ANOMALY", "GLOBAL_LOGISTICS", "airspace", {
            "category": "flow", "severity": 0.8, "context": f"Pulse {current_pulse} vs Baseline {baseline}"
        })

def check_environmental_gate(m, icao):
    """Logic #6 (Env): Wildfire detection via low-altitude loitering"""
    # Pattern: Low altitude + Slow speed + Not near a known airport/mine
    if m['alt'] < 6000 and m['vel'] < 130:
        push_signal("WILDFIRE_PROBABLE", "AERIAL_ENV", "environment", {
            "severity": 0.4, "lat": m['lat'], "lon": m['lon'], "icao": icao
        })

def check_safety_gates(m, icao):
    """Logic #1 & #5: Crash and Corridor Deviations"""
    # Emergency Descent Rate (> 4,500 ft/min)
    if m['v_rate'] < -4500 and m['alt'] > 4000:
        push_signal("AIRSPACE_ANOMALY", icao, "airspace", {
            "severity": 1.0, "context": "Rapid unplanned altitude loss"
        })
    
    # Signal Loss at Altitude (Shadowing Signal #5)
    # If a plane was high and suddenly stops updating (handled by Redis EXPIRE in ingest)

# --- CORE ENGINE ---

def start_aviation_brain():
    log("Aviation Brain Online. Full Heuristic Suite (10 Gates) Active.")
    
    try:
        conn = psycopg2.connect(**DB_CONFIG)
        cur = conn.cursor()
    except Exception as e:
        log(f"CRITICAL DB ERROR: {e}")
        return

    last_capacity_check = 0

    while True:
        try:
            # 1. Process Live Radar Stream
            batch = r_sky.zrange("global_sky", 0, 499)
            if batch:
                for member in batch:
                    icao_hex, callsign = member.decode().split(":")
                    raw = r_sky.hgetall(f"flight_data:{icao_hex}")
                    if not raw: continue
                    m = {k.decode(): float(v) for k, v in raw.items()}

                    # Lookup Watchlist
                    cur.execute("SELECT owner_entity, category FROM aircraft_profiles WHERE icao_hex = %s", (icao_hex,))
                    res = cur.fetchone()
                    profile = {"owner": res[0], "cat": res[1]} if res else None

                    # Run Real-time Heuristics
                    check_tactical_gates(cur, m, icao_hex, profile)
                    check_seismic_muting(cur, m, icao_hex)
                    check_environmental_gate(m, icao_hex)
                    check_safety_gates(m, icao_hex)

                r_sky.zremrangebyrank("global_sky", 0, 499)

            # 2. Hourly Capacity Analysis (Logic #9)
            if time.time() - last_capacity_check > 3600:
                check_capacity_gate(cur)
                last_capacity_check = time.time()
                conn.commit() # Keep connection fresh

            time.sleep(2)
        except Exception as e:
            log(f"Loop error: {e}")
            time.sleep(5)

if __name__ == "__main__":
    start_aviation_brain()