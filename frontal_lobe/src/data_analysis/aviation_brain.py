import redis
import json
import psycopg2
import time
import sys
import traceback

# --- DB CONFIG ---
DB_CONFIG = {
    "dbname": "rocco_commodities",
    "user": "rocco_admin",
    "password": "REMOVED",
    "host": "rocco_db", 
    "port": "5432"
}


"""
get_aircraft_profile: checks if icao_hex seen on radar is in database watchlist
"""
def get_aircraft_profile(cursor, icao_hex):
    cursor.execute("SELECT owner_entity, category FROM aircraft_profiles WHERE icao_hex = %s", (icao_hex.lower(),))
    res = cursor.fetchone()
    return {"owner": res[0], "cat": res[1]} if res else None

"""
analyze_hehavior: checks if planes are landing/taking-off or surveying an area via radar
"""
def analyze_behavior(alt, v_rate, velocity):
    """Heuristic behavior analysis for site arrivals/surveys"""
    behaviors = []
    if alt < 15000 and v_rate < -500:
        behaviors.append({"class": "site_arrival_profile", "conf": 0.90})
    if alt < 15000 and v_rate > 500:
        behaviors.append({"class": "site_departure_profile", "conf": 0.90})
    if alt < 10000 and velocity < 180:
        behaviors.append({"class": "survey_pattern", "conf": 0.75})
    return behaviors

"""
start_aviation_brain: main control function that processes 500 flights posted to redis
                      at a time from aviation_ingest.cpp. 

***CURRENTLY: we are saving all flight legs to eventually train our aviation AI***
"""
def start_aviation_brain():
    print("--- [SYSTEM] FULL-SPECTRUM BRAIN ACTIVE ---", flush=True)
    r_sky = redis.Redis(host='redis', port=6379, db=1)
    r_bus = redis.Redis(host='redis', port=6379, db=0)
    
    while True:
        conn = None
        try:
            print("[DB] Connecting...", flush=True)
            conn = psycopg2.connect(**DB_CONFIG)
            conn.autocommit = True
            cur = conn.cursor()
            print("[DB] Connection Hot.", flush=True)

            while True:
                #Only pull 500 aircraft at a time to prevent OOM
                batch = r_sky.zrange("global_sky", 0, 499)
                
                if not batch:
                    time.sleep(5)
                    continue

                print(f"[PROCESS] Handling slice of {len(batch)} hexes...", flush=True)

                for flight_member in batch:
                    try:
                        member_str = flight_member.decode()
                        if ":" not in member_str: continue
                        icao_hex, callsign = member_str.split(":")
                        
                        raw = r_sky.hgetall(f"flight_data:{icao_hex}")
                        if not raw: continue
                        
                        m = {k.decode(): float(v) for k, v in raw.items()}

                        cur.execute("""
                            INSERT INTO flight_legs (icao24, callsign, trajectory, max_alt, max_v_rate, avg_velocity, status)
                            VALUES (%s, %s, ST_SetSRID(ST_MakeLine(ST_MakePoint(%s, %s), ST_MakePoint(%s, %s)), 4326), %s, %s, %s, 'ACTIVE')
                            ON CONFLICT (icao24) WHERE status = 'ACTIVE' 
                            DO UPDATE SET 
                                trajectory = ST_AddPoint(flight_legs.trajectory, ST_SetSRID(ST_MakePoint(EXCLUDED.avg_velocity, EXCLUDED.max_v_rate), 4326)),
                                last_seen = CURRENT_TIMESTAMP,
                                max_alt = GREATEST(flight_legs.max_alt, EXCLUDED.max_alt),
                                max_v_rate = GREATEST(flight_legs.max_v_rate, EXCLUDED.max_v_rate),
                                avg_velocity = (flight_legs.avg_velocity + EXCLUDED.avg_velocity) / 2;
                        """, (icao_hex, callsign, m['lon'], m['lat'], m['lon'], m['lat'], m['alt'], m['v_rate'], m['vel']))

                        profile = get_aircraft_profile(cur, icao_hex)
                        detections = analyze_behavior(m['alt'], m['v_rate'], m['vel'])
                        
                        if profile:
                            detections.append({"class": f"watchlist_{profile['cat']}", "conf": 1.0})

                        if detections:
                            packet = {
                                "source": "aviation",
                                "icao_hex": icao_hex,
                                "callsign": callsign,
                                "coords": [m['lat'], m['lon']],
                                "detections": detections,
                                "metrics": {"alt": m['alt'], "vel": m['vel'], "owner": profile['owner'] if profile else "Unknown"},
                                "timestamp": time.time()
                            }
                            r_bus.publish("raw_signals", json.dumps(packet))

                    except Exception as inner_e:
                        continue
                
                # Clear current records
                r_sky.zremrangebyrank("global_sky", 0, 499)
                
                # End dead legs
                cur.execute("""
                    UPDATE flight_legs SET status = 'COMPLETED' 
                    WHERE status = 'ACTIVE' AND last_seen < CURRENT_TIMESTAMP - INTERVAL '10 minutes';
                """)

        except Exception as e:
            print(f"[CRITICAL] {e}", flush=True)
            traceback.print_exc()
            if conn: conn.close()
            time.sleep(5)

if __name__ == "__main__":
    start_aviation_brain()