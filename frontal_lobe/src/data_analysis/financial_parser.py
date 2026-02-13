import time
import psycopg2
from geopy.geocoders import Nominatim
from geopy.exc import GeocoderTimedOut, GeocoderServiceError

# --- CONFIG ---
DB_CONFIG = { "dbname": "rocco_commodities", "user": "rocco_admin", "password": "REMOVED", "host": "hippocampus", "port": "5432" }
geolocator = Nominatim(user_agent="RoccoMaggiore_Geolocator_v2")

# --- PERMUTATION TEMPLATES ---
# The brain will try these suffixes in order to force a match in OSM/Nominatim.
PERMUTATIONS = {
    "mine": [
        "{name} mine", "{name} operations", "{name} mining complex", 
        "{name} open pit", "{name} project", "{name} site"
    ],
    "refinery": [
        "{name} refinery", "{name} oil refinery", "{name} chemical plant", 
        "{name} industrial complex", "{name} facility"
    ],
    "smelter": [
        "{name} smelter", "{name} smelting complex", "{name} metal works", 
        "{name} processing plant", "{name} foundry"
    ]
}

def get_db_connection():
    return psycopg2.connect(**DB_CONFIG)

def iterative_search(asset_name, asset_type):
    """
    Cycles through name permutations until a coordinate is found.
    """
    # 1. Start with the raw name as provided by the Financial Parser
    candidates = [asset_name]
    
    # 2. Add type-specific variations
    if asset_type in PERMUTATIONS:
        candidates.extend([p.format(name=asset_name) for p in PERMUTATIONS[asset_type]])
    
    # 3. Execution Loop
    for query in candidates:
        try:
            print(f"      [SEARCH] Trying: '{query}'...")
            location = geolocator.geocode(query, timeout=10)
            if location:
                return location.latitude, location.longitude
        except (GeocoderTimedOut, GeocoderServiceError):
            time.sleep(2) # Back off if service is struggling
            continue
        
        # Respect Nominatim rate limit (1 req/sec)
        time.sleep(1.2)
        
    return None, None

def resolve_spatial_ghosts():
    print("[GEOLOCATION] Scanning for assets requiring coordinates...", flush=True)
    conn = get_db_connection()
    cur = conn.cursor()

    # Select assets with NULL geometry, prioritizing those found via Filings
    cur.execute("SELECT id, name, type FROM assets WHERE geom IS NULL LIMIT 20")
    ghosts = cur.fetchall()

    if not ghosts:
        conn.close()
        return

    for aid, name, a_type in ghosts:
        print(f"   -> Attempting discovery for: {name} ({a_type})")
        
        lat, lon = iterative_search(name, a_type)

        if lat and lon:
            # UPDATE DB with Geometry and mark as Resolved
            cur.execute("""
                UPDATE assets 
                SET geom = ST_SetSRID(ST_MakePoint(%s, %s), 4326),
                    latitude = %s, longitude = %s,
                    source = source || '_GEOLOCATED'
                WHERE id = %s
            """, (lon, lat, lat, lon, aid))
            print(f"      [RESOLVED] Found at {lat}, {lon}")
        else:
            print(f"      [FAILED] Exhausted all permutations for {name}.")

    conn.commit()
    conn.close()

if __name__ == "__main__":
    while True:
        try:
            resolve_spatial_ghosts()
            # Check for new ghost assets every 30 minutes
            time.sleep(1800) 
        except Exception as e:
            print(f"[GEOLOCATION ERR] {e}")
            time.sleep(60)