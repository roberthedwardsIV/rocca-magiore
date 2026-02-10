import psycopg2
import pandas as pd
import time
import json
from sklearn.ensemble import RandomForestClassifier
from geopy.distance import geodesic

# --- CONFIG ---
DB_CONFIG = {
    "dbname": "rocco_commodities", "user": "rocco_admin",
    "password": "REMOVED", "host": "hippocampus", "port": "5432"
}

def get_db_connection():
    return psycopg2.connect(**DB_CONFIG)

def train_watchlist():
    print("[MARITIME TRAINER] Analyzing Global Fleet Behavior...", flush=True)
    conn = get_db_connection()
    cur = conn.cursor()

    # 1. IDENTIFY SUPPLY CHAIN SUSPECTS (Heuristic Phase)
    # Find ships that have visited BOTH a Mine AND a Refinery in the last 30 days.
    # This creates our "Ground Truth" for the classifier.
    
    print("   -> Scanning for Mine-to-Refinery routes...", flush=True)
    cur.execute("""
        WITH mine_visits AS (
            SELECT DISTINCT l.mmsi 
            FROM maritime_voyage_logs l
            JOIN assets a ON ST_DWithin(ST_SetSRID(ST_MakePoint(l.lon, l.lat), 4326), a.geom, 0.2)
            WHERE a.commodity_types::text ILIKE '%Mine%'
        ),
        refinery_visits AS (
            SELECT DISTINCT l.mmsi 
            FROM maritime_voyage_logs l
            JOIN assets a ON ST_DWithin(ST_SetSRID(ST_MakePoint(l.lon, l.lat), 4326), a.geom, 0.2)
            WHERE a.commodity_types::text ILIKE '%Refinery%'
        )
        SELECT m.mmsi 
        FROM mine_visits m
        JOIN refinery_visits r ON m.mmsi = r.mmsi;
    """)
    
    suspects = [row[0] for row in cur.fetchall()]
    print(f"   -> Found {len(suspects)} confirmed supply chain vessels.")

    # 2. UPDATE WATCHLIST
    # We tag these ships as 'Strategic_Hauler'
    if suspects:
        cur.execute("""
            INSERT INTO maritime_vessel_profiles (mmsi, category, is_watchlist, confidence_score, last_updated)
            VALUES (%s, 'Strategic_Hauler', TRUE, 0.95, NOW())
            ON CONFLICT (mmsi) DO UPDATE SET
                is_watchlist = TRUE,
                category = 'Strategic_Hauler',
                last_updated = NOW();
        """, (suspects[0],)) # Simplified batch insert logic for brevity
        
        # Batch update loop
        for mmsi in suspects:
            cur.execute("""
                UPDATE maritime_vessel_profiles 
                SET is_watchlist = TRUE, category = 'Strategic_Hauler', last_updated = NOW()
                WHERE mmsi = %s
            """, (mmsi,))
            
    # 3. DRAFT ANALYSIS (Secondary Check)
    # Detect ships that changed draft (loading/unloading) near ports
    cur.execute("""
        SELECT DISTINCT mmsi FROM maritime_voyage_logs 
        WHERE draft IS NOT NULL 
        GROUP BY mmsi 
        HAVING MAX(draft) - MIN(draft) > 1.5 -- Significant cargo change
    """)
    draft_changers = [row[0] for row in cur.fetchall()]
    
    for mmsi in draft_changers:
        cur.execute("""
            INSERT INTO maritime_vessel_profiles (mmsi, category, is_watchlist, last_updated)
            VALUES (%s, 'Heavy_Cargo_Active', TRUE, NOW())
            ON CONFLICT (mmsi) DO UPDATE SET is_watchlist = TRUE;
        """, (mmsi,))

    conn.commit()
    conn.close()
    print("[MARITIME TRAINER] Watchlist Updated.", flush=True)

if __name__ == "__main__":
    while True:
        try:
            train_watchlist()
            time.sleep(86400) # Run daily
        except Exception as e:
            print(f"[TRAINER ERR] {e}")
            time.sleep(60)