import psycopg2
import json
import time
import sys
import redis

# --- CONFIG ---
DB_CONFIG = {
    "dbname": "rocco_commodities",
    "user": "rocco_admin",
    "password": "REMOVED",
    "host": "hippocampus",
    "port": "5432"
}

# The base commodities we care about. The system maps all physical assets to these first.
COMMODITY_TEMPLATES = {
    "copper": {"spot": "LME_CU", "fut": "HG", "exch": "COMEX", "sector": "METALS", "mult": 25000},
    "gold":   {"spot": "XAU_USD", "fut": "GC", "exch": "COMEX", "sector": "METALS", "mult": 100},
    "oil":    {"spot": "WTI_SPOT", "fut": "CL", "exch": "NYMEX", "sector": "ENERGY", "mult": 1000},
    "lithium": {"spot": "LITH_CARB", "fut": "LJK", "exch": "CME", "sector": "METALS", "mult": 1000}
}

r_bus = redis.Redis(host='corpus_callosum', port=6379, db=0)

def get_raw_connection():
    return psycopg2.connect(**DB_CONFIG)

def ensure_schema_compatibility(cur):
    print("[SYNAPSE] Running Database Janitor & Schema Integrity Check...", flush=True)
    try:
        # 1. Ensure columns exist
        cur.execute("ALTER TABLE assets ADD COLUMN IF NOT EXISTS last_update BIGINT DEFAULT 0;")
        cur.execute("ALTER TABLE assets ADD COLUMN IF NOT EXISTS op_health FLOAT DEFAULT 1.0;")
        cur.execute("ALTER TABLE assets ADD COLUMN IF NOT EXISTS metadata JSONB DEFAULT '{}'::jsonb;")
        
        # 2. Scrub Garbage Data
        cur.execute("""
            DELETE FROM assets 
            WHERE name IN ('Unknown', 'H', 'UNKN', '') 
               OR name IS NULL 
               OR length(name) < 3;
        """)

        # 3. Deduplicate Assets (Keep the most recent)
        cur.execute("""
            DELETE FROM assets
            WHERE id NOT IN (
                SELECT id FROM (
                    SELECT id, ROW_NUMBER() OVER (PARTITION BY name ORDER BY last_update DESC, id DESC) as rnum
                    FROM assets
                ) t
                WHERE t.rnum = 1
            );
        """)

        # 4. Lock the Schema (SILENTLY check if constraint exists first to avoid terminal spam)
        cur.execute("""
            DO $$
            BEGIN
                IF NOT EXISTS (
                    SELECT 1 FROM pg_constraint WHERE conname = 'unique_asset_name'
                ) THEN
                    ALTER TABLE assets ADD CONSTRAINT unique_asset_name UNIQUE (name);
                END IF;
            END
            $$;
        """)
        
    except Exception as e:
        print(f"[SCHEMA ERR] {e}", flush=True)

def wait_for_reality(cur):
    print("[SYNAPSE] Waiting for physical assets...", flush=True)
    while True:
        try:
            cur.execute("SELECT COUNT(*) FROM assets")
            count = cur.fetchone()[0]
            if count > 0:
                print(f"[SYNAPSE] Physical world detected. Mapping {count} assets to base commodities...", flush=True)
                break
        except Exception: pass
        time.sleep(10)

def build_synapses():
    print("[SYNAPSE] Booting Associative Memory...", flush=True)
    conn = get_raw_connection()
    conn.autocommit = True
    cur = conn.cursor()

    ensure_schema_compatibility(cur)
    wait_for_reality(cur)

    # Fetch all physical assets to link them to their base commodity futures
    cur.execute("SELECT id, name, commodity_types FROM assets")
    assets = cur.fetchall()

    for asset_id, name, comm_list in assets:
        entity_key = f"ASSET_{asset_id}"
        
        if comm_list:
            for comm in comm_list:
                template_key = next((k for k in COMMODITY_TEMPLATES if k in comm.lower()), None)
                if template_key:
                    tmpl = COMMODITY_TEMPLATES[template_key]
                    # Link Asset to Commodity Spot
                    cur.execute("INSERT INTO ticker_registry (symbol, instrument_type, exchange, active, last_updated) VALUES (%s, 'commodity_spot', 'GLOBAL', TRUE, NOW()) ON CONFLICT DO NOTHING", (tmpl["spot"],))
                    cur.execute("INSERT INTO ticker_sensitivity (ticker_symbol, entity_id, weight) VALUES (%s, %s, 1.0) ON CONFLICT DO NOTHING", (tmpl["spot"], entity_key))
                    
                    # Link Asset to Commodity Future
                    cur.execute("INSERT INTO ticker_registry (symbol, instrument_type, exchange, multiplier, active, last_updated) VALUES (%s, 'future', %s, %s, TRUE, NOW()) ON CONFLICT DO NOTHING", (tmpl["fut"], tmpl["exch"], tmpl["mult"]))
                    cur.execute("INSERT INTO ticker_sensitivity (ticker_symbol, entity_id, weight) VALUES (%s, %s, 0.8) ON CONFLICT DO NOTHING", (tmpl["fut"], entity_key))

    cur.close()
    conn.close()
    print("[SYNAPSE] Base commodity pathways defined. Ownership resolution deferred to Financial Parser.")

if __name__ == "__main__":
    build_synapses()