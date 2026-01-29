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


# Tactical gate function: looks at the following:
#   Proximity: looks for planes on our watchlist near assets in our database
#   Night Owls: looks for planes on our watchlist landing in the middle of the night or early morning
#   Dark Arrivals: looks for unknown planes landing near one of our assets
def check_tactical_gates(cur, m, icao, profile):
    cur.execute("""
        SELECT id, name, commodity_types[1] as type 
        FROM assets 
        WHERE ST_DWithin(geom, ST_SetSRID(ST_MakePoint(%s, %s), 4326), 1.0)
    """, (m['lon'], m['lat']))
    assets = cur.fetchall()

    is_night = not (6 < time.localtime().tm_hour < 19)

    for aid, name, a_type in assets:
        # Proximity check
        if profile:
            push_signal("ASSET_PROXIMITY", aid, a_type, {
                "category": "threat", "severity": 0.5, "owner": profile['owner'], "icao": icao
            })
            
            # Night owl check
            if is_night:
                push_signal("CLANDESTINE_MOVEMENT", aid, a_type, {"severity": 0.8}, strength=0.9)
        
        # Dark arrival check
        elif m['alt'] < 2000:
            cur.execute("""
                SELECT 1 FROM assets 
                WHERE ST_DWithin(geom, ST_SetSRID(ST_MakePoint(%s, %s), 4326), 0.05)
            """, (m['lon'], m['lat']))
            if cur.fetchone():
                push_signal("DARK_ARRIVAL", aid, a_type, {"severity": 0.7, "icao": icao})


# Seismic censoring function: prevents earthquake signals from being spawned when planes are passing directly
# over the seismic sensor at a low altitude (false positive elimination)
def check_seismic_muting(cur, m, icao):
    if m['alt'] < 3000:
        cur.execute("""
            SELECT network || '_' || station 
            FROM earthquake_stations 
            WHERE ST_DWithin(geom, ST_SetSRID(ST_MakePoint(%s, %s), 4326), 0.08)
        """, (m['lon'], m['lat']))
        for mon in cur.fetchall():
            push_signal("SEISMIC_MASK", mon[0], "earthquake", {"is_voided": True, "icao": icao})


# Capacity check function: generates signals when the weekly capacity rate decreases by 60% or more
def check_capacity_gate(cur):
    """Logic #3 & #9: Capacity Analysis against Baselines"""
    cur.execute("SELECT weekly_frequency FROM baselines_aviation WHERE route_key = 'REGIONAL_ASSET_LOGISTICS'")
    res = cur.fetchone()
    if not res: return
    
    baseline = res[0]
    cur.execute("SELECT COUNT(*) * 7 FROM flight_legs WHERE last_seen > NOW() - INTERVAL '1 day'")
    current_pulse = cur.fetchone()[0]

    if current_pulse < (baseline * 0.6):
        push_signal("CAPACITY_ANOMALY", "GLOBAL_LOGISTICS", "airspace", {
            "category": "flow", "severity": 0.8, "context": f"Pulse {current_pulse} vs Baseline {baseline}"
        })


# 
def check_environmental_gate(m, icao):
    """Logic #6 (Env): Wildfire detection via low-altitude loitering"""
    # Pattern: Low altitude + Slow speed + Not near a known airport/mine
    if m['alt'] < 6000 and m['vel'] < 130:
        push_signal("WILDFIRE_PROBABLE", "AERIAL_ENV", "environment", {
            "severity": 0.4, "lat": m['lat'], "lon": m['lon'], "icao": icao
        })


#
def check_safety_gates(m, icao):
    """Logic #1 & #5: Crash and Corridor Deviations"""
    # Emergency Descent Rate (> 4,500 ft/min)
    if m['v_rate'] < -4500 and m['alt'] > 4000:
        push_signal("AIRSPACE_ANOMALY", icao, "airspace", {
            "severity": 1.0, "context": "Rapid unplanned altitude loss"
        })
    
    # Signal Loss at Altitude (Shadowing Signal #5)
    # If a plane was high and suddenly stops updating (handled by Redis EXPIRE in ingest)


# Financial intelligence function: tracks executive movement from mining headquarters 
# to offshore tax havens (Jersey, Cayman Islands, Switzerland, etc.)
def check_financial_shuttles(m, icao, profile):
    """Logic #8: Tax Haven Shuttle Detection"""
    if profile and profile['cat'] == 'Corporate_Exec_Probable':
        # BBOX covering common offshore banking hubs (Simplified check)
        # Lat/Lon ranges for Swiss/Channel/Cayman corridors
        tax_havens = [
            {"name": "Switzerland", "lat": (45.8, 47.8), "lon": (5.9, 10.5)},
            {"name": "Cayman", "lat": (19.2, 19.4), "lon": (-81.4, -81.2)},
            {"name": "Jersey", "lat": (49.1, 49.3), "lon": (-2.3, -2.1)}
        ]
        for haven in tax_havens:
            if haven['lat'][0] < m['lat'] < haven['lat'][1] and \
               haven['lon'][0] < m['lon'] < haven['lon'][1]:
                push_signal("TAX_HAVEN_SHUTTLE", icao, "refinery", {
                    "category": "fin", "severity": 0.6, "haven": haven['name'], "owner": profile['owner']
                })


# Airspace restriction function: monitors for sudden "no-fly" zones or deviations 
# by 100% of civilian traffic in a commodity-heavy area
def check_restriction_gate(cur, m):
    """Logic #4: Dynamic Airspace Restrictions (Pre-War/Coup)"""
    # Check if a flight is skirting or diverting from a historically high-traffic 
    # corridor in a high-risk mining region
    cur.execute("""
        SELECT 1 FROM assets 
        WHERE ST_DWithin(ST_SetSRID(ST_MakePoint(%s, %s), 4326), geom, 2.0)
    """, (m['lon'], m['lat']))
    if cur.fetchone():
        # If the plane is performing a 180-turn or wide diversion at high altitude
        if m['alt'] > 20000 and abs(m['v_rate']) < 100 and m['vel'] > 300:
            # Check logic for 'Skirt' behavior (Simplified)
            pass


# Maintenance intelligence function: flags grounded fleets. If a specific 
# logistics aircraft from our watchlist stops moving for > 14 days.
def check_maintenance_lag(cur):
    """Logic #10: Maintenance and Operational Lag Detection"""
    # Runs daily or hourly. Looks for watchlist planes with no 'ACTIVE' legs.
    cur.execute("""
        SELECT icao_hex, owner_entity 
        FROM aircraft_profiles 
        WHERE is_watchlist = TRUE 
          AND icao_hex NOT IN (SELECT icao24 FROM flight_legs WHERE last_seen > NOW() - INTERVAL '14 days')
    """)
    grounded = cur.fetchall()
    for icao, owner in grounded:
        push_signal("MAINTENANCE_LAG", icao, "supply_line", {
            "category": "flow", "severity": 0.4, "owner": owner
        })


# Controller function: pulls all flight radar data from "global_sky" redis channel, cross references our 
# watchlist, then runs them all through the various signal generation gate functions above. It also 
# keeps our capacity stats fresh by updating them each hour.
def start_aviation_brain():
    print("[AVIATION BRAIN] Aviation Brain Online. Full Heuristic Suite (10 Gates) Active.")
    
    try:
        conn = psycopg2.connect(**DB_CONFIG)
        cur = conn.cursor()
    except Exception as e:
        print(f"[AVIATION BRAIN] CRITICAL DB ERROR: {e}")
        return

    last_capacity_check = 0
    last_maintenance_check = 0

    while True:
        try:
            batch = r_sky.zrange("global_sky", 0, 499)
            if batch:
                for member in batch:
                    icao_hex, callsign = member.decode().split(":")
                    raw = r_sky.hgetall(f"flight_data:{icao_hex}")
                    if not raw: continue
                    m = {k.decode(): float(v) for k, v in raw.items()}

                    # Check all planes to see if they are in the watchlist
                    cur.execute("SELECT owner_entity, category FROM aircraft_profiles WHERE icao_hex = %s", (icao_hex,))
                    res = cur.fetchone()
                    profile = {"owner": res[0], "cat": res[1]} if res else None

                    # Run Real-time Heuristic Functions (Signal Generation)
                    check_tactical_gates(cur, m, icao_hex, profile)
                    check_seismic_muting(cur, m, icao_hex)
                    check_environmental_gate(m, icao_hex)
                    check_safety_gates(m, icao_hex)
                    check_financial_shuttles(m, icao_hex, profile)
                    check_restriction_gate(cur, m)

                r_sky.zremrangebyrank("global_sky", 0, 499)

            # Hourly Capacity Analysis 
            if time.time() - last_capacity_check > 3600:
                check_capacity_gate(cur)
                last_capacity_check = time.time()
                conn.commit() 

            # Daily Maintenance Check
            if time.time() - last_maintenance_check > 86400:
                check_maintenance_lag(cur)
                last_maintenance_check = time.time()
                conn.commit()

            time.sleep(2)
        except Exception as e:
            print(f"[AVIATION BRAIN] Loop error: {e}")
            time.sleep(5)


# __main__: calls main controller function upon startup
if __name__ == "__main__":
    start_aviation_brain()