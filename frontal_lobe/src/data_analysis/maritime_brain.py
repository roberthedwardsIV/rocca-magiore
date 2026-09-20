import redis
import json
import psycopg2
import time
from geopy.distance import geodesic
from utils.db_config import db_config

# --- CONFIGURATION ---
DB_CONFIG = db_config()

# Two Redis connections:
# r_in: Reads the high-speed firehose (dedicated channel)
# r_out: Sends calculated signals to the C++ Core (shared channel)
r_in = redis.Redis(host='corpus_callosum', port=6379, db=0)
r_out = redis.Redis(host='corpus_callosum', port=6379, db=0)

# Memory Cache
watchlist_cache = set()
vessel_history = {} # MMSI -> {last_lat, last_lon, last_ts}

def get_db_connection():
    try:
        return psycopg2.connect(**DB_CONFIG)
    except Exception as e:
        print(f"[MARITIME] DB Connect Error: {e}")
        return None

def refresh_watchlist():
    """Loads the daily target list from DB into memory for speed."""
    global watchlist_cache
    conn = None
    try:
        conn = get_db_connection()
        if not conn: return
        cur = conn.cursor()
        cur.execute("SELECT mmsi FROM maritime_vessel_profiles WHERE is_watchlist = TRUE")
        watchlist_cache = {row[0] for row in cur.fetchall()}
        print(f"[MARITIME BRAIN] Watchlist Refreshed: {len(watchlist_cache)} targets.")
    except Exception as e:
        print(f"[DB ERR] {e}")
    finally:
        if conn: conn.close()

def log_voyage_data(conn, mmsi, lat, lon, speed, heading, draft, status):
    """Dumps raw data for the Trainer to learn from later."""
    try:
        cur = conn.cursor()
        cur.execute("""
            INSERT INTO maritime_voyage_logs (mmsi, lat, lon, speed, heading, nav_status, draft, timestamp)
            VALUES (%s, %s, %s, %s, %s, %s, %s, NOW())
        """, (mmsi, lat, lon, speed, heading, status, draft))
        conn.commit()
    except: pass

def inject_route_state(conn, mmsi, lat, lon, speed):
    """
    Maps the vessel to a specific Maritime Route and sends a 'presence' signal.
    This links the moving ship (Dynamic) to the supply line (Static).
    """
    try:
        with conn.cursor() as cur:
            # Spatial Query: Is this ship within 50km (0.5 deg) of a known route corridor?
            # We filter for 'maritime_route' types only.
            cur.execute("""
                SELECT id, name FROM supply_lines 
                WHERE type = 'maritime_route' 
                AND ST_DWithin(geom, ST_SetSRID(ST_MakePoint(%s, %s), 4326), 0.5)
                LIMIT 1
            """, (lon, lat))
            
            route = cur.fetchone()
            
            if route:
                route_id, route_name = route
                
                # SIGNAL GENERATION
                # We send this to the 'raw_signals' bus. 
                # The C++ Dispatcher picks this up and updates the MaritimeRoute object.
                signal = {
                    "entity_type": "maritime_route",
                    "line_id": route_id,
                    "category": "traffic_load", # New category for the C++ route to handle
                    "timestamp": int(time.time() * 1000),
                    "data": {
                        "mmsi": mmsi,
                        "value": 1.0, # Increment traffic count
                        "context": f"Watchlist vessel {mmsi} traversing {route_name}",
                        "lat": lat,
                        "lon": lon,
                        "speed": speed
                    }
                }
                r_out.lpush("raw_signals", json.dumps(signal))
                # Optional debug log
                # print(f"[MARITIME] Linked Vessel {mmsi} -> Route {route_id} ({route_name})")

    except Exception as e:
        print(f"[ROUTE MAP ERR] {e}")

# --- LOGIC GATES ---

def check_dark_activity(mmsi, lat, lon, timestamp):
    """Gate 1: Detects gaps in AIS transmission (Dark Fleet)."""
    if mmsi not in vessel_history:
        vessel_history[mmsi] = {"ts": timestamp, "lat": lat, "lon": lon}
        return None

    last = vessel_history[mmsi]
    gap = (timestamp - last["ts"]) / 1000 # seconds
    
    # Signal if gap > 6 hours (21600s) AND vessel moved significant distance (teleport/dark move)
    if gap > 21600:
        dist = geodesic((last['lat'], last['lon']), (lat, lon)).km
        if dist > 50: 
            # Update history before returning to prevent duplicate alerts
            vessel_history[mmsi] = {"ts": timestamp, "lat": lat, "lon": lon}
            return {
                "category": "threat", "severity": 0.8,
                "context": f"Dark Activity Detected. Gap: {gap/3600:.1f}hrs, Dist: {dist:.1f}km"
            }
    
    # Update history
    vessel_history[mmsi] = {"ts": timestamp, "lat": lat, "lon": lon}
    return None

def check_loitering(mmsi, lat, lon, speed, conn):
    """Gate 2: Detects unexpected stops at sea (not in a port)."""
    if speed < 1.0:
        # Verify we are NOT in a known port
        try:
            with conn.cursor() as cur:
                cur.execute("""
                    SELECT 1 FROM supply_lines 
                    WHERE type = 'maritime_port' 
                    AND ST_DWithin(geom, ST_SetSRID(ST_MakePoint(%s, %s), 4326), 0.15)
                """, (lon, lat))
                
                if not cur.fetchone():
                    return {
                        "category": "flow", "severity": 0.5,
                        "context": "Loitering in open ocean (Potential Delay/Transfer)"
                    }
        except Exception: pass
    return None

def start_maritime_brain():
    global r_in, r_out
    print("[MARITIME BRAIN] Online. Initializing Watchlist...", flush=True)
    refresh_watchlist()
    conn = get_db_connection()
    
    last_refresh = time.time()

    while True:
        try:
            # 1. Periodic Watchlist Refresh (Every 10 mins)
            if time.time() - last_refresh > 600:
                refresh_watchlist()
                last_refresh = time.time()

            # 2. Ingest
            # Blocking Pop with 0 timeout waits indefinitely for data
            _, raw_data = r_in.brpop("maritime_ais", timeout=0)
            msg = json.loads(raw_data)
            
            meta = msg.get("MetaData", {})
            mmsi = meta.get("MMSI")
            
            # --- LOGGING (For Trainer) ---
            # We log EVERYTHING (sampled) so the trainer can find new correlations
            if msg.get("MessageType") == "PositionReport":
                pos = msg.get("Message", {}).get("PositionReport", {})
                if conn:
                    log_voyage_data(conn, mmsi, pos.get("Latitude"), pos.get("Longitude"), 
                                    pos.get("Sog"), pos.get("Cog"), None, pos.get("NavigationalStatus"))
            
            elif msg.get("MessageType") == "ShipStaticData":
                # Static data contains DRAFT (Crucial for cargo tracking)
                static = msg.get("Message", {}).get("ShipStaticData", {})
                # Note: Static data updates are rarer, good place to update draft in logs if needed
                pass
                
            # --- REAL-TIME ANALYSIS (Watchlist Only) ---
            if mmsi not in watchlist_cache:
                continue # Ignore noise

            if msg.get("MessageType") == "PositionReport":
                pos = msg.get("Message", {}).get("PositionReport", {})
                lat = pos.get("Latitude")
                lon = pos.get("Longitude")
                speed = pos.get("Sog", 0)
                
                if not lat or not lon: continue

                ts = int(time.time() * 1000)

                # Gate 1: Dark Activity
                dark_signal = check_dark_activity(mmsi, lat, lon, ts)
                if dark_signal:
                    packet = {
                        "source": "maritime", "entity_type": "vessel", "entity_id": str(mmsi),
                        "timestamp": ts, "data": dark_signal
                    }
                    r_out.lpush("raw_signals", json.dumps(packet))

                # Gate 2: Loitering
                if conn:
                    loiter_signal = check_loitering(mmsi, lat, lon, speed, conn)
                    if loiter_signal:
                        packet = {
                            "source": "maritime", "entity_type": "vessel", "entity_id": str(mmsi),
                            "timestamp": ts, "data": loiter_signal
                        }
                        r_out.lpush("raw_signals", json.dumps(packet))

                # Gate 3: Route Injection (State Propagation)
                if conn:
                    inject_route_state(conn, mmsi, lat, lon, speed)

        except redis.ConnectionError:
            print("[MARITIME] Redis connection lost. Reconnecting...")
            time.sleep(2)
            r_in = redis.Redis(host='corpus_callosum', port=6379, db=0)
            r_out = redis.Redis(host='corpus_callosum', port=6379, db=0)
            
        except Exception as e:
            print(f"[BRAIN ERR] {e}")
            if conn and conn.closed: 
                conn = get_db_connection()
            elif not conn:
                conn = get_db_connection()

if __name__ == "__main__":
    start_maritime_brain()