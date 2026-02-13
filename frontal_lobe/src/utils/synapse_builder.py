import psycopg2
import json
import re
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

# The "Logic Templates" for Market Instantiation
# If we find a physical asset with these keywords, we define the market in Thalamus.
COMMODITY_TEMPLATES = {
    "copper": {"spot": "LME_CU", "fut": "HG", "exch": "COMEX", "sector": "METALS", "mult": 25000},
    "gold":   {"spot": "XAU_USD", "fut": "GC", "exch": "COMEX", "sector": "METALS", "mult": 100},
    "oil":    {"spot": "WTI_SPOT", "fut": "CL", "exch": "NYMEX", "sector": "ENERGY", "mult": 1000},
    "lithium": {"spot": "LITH_CARB", "fut": "LJK", "exch": "CME", "sector": "METALS", "mult": 1000}
}

# Redis for inter-module signaling
r_bus = redis.Redis(host='corpus_callosum', port=6379, db=0)

def get_db_connection():
    return psycopg2.connect(**DB_CONFIG)

def wait_for_reality(cur):
    """
    Cognitive Gate: Ensures that osm_ingest has started populating the assets table.
    We don't form synapses in a void.
    """
    print("[SYNAPSE] Waiting for physical assets to appear in DB...", flush=True)
    while True:
        cur.execute("SELECT COUNT(*) FROM assets")
        count = cur.fetchone()[0]
        if count > 0:
            print(f"[SYNAPSE] Physical world detected. Mapping {count} assets...", flush=True)
            break
        time.sleep(10)

def discover_ticker_from_registry(cur, asset_name):
    """
    Heuristic: Search the aircraft_registry to link a physical asset name 
    to a corporate parent.
    """
    if not asset_name or len(asset_name) < 3:
        return None
    
    # Extract text in parentheses (e.g., 'Escondida (BHP)')
    match = re.search(r'\((.*?)\)', asset_name)
    search_term = match.group(1) if match else asset_name
    
    cur.execute("""
        SELECT owner FROM aircraft_registry 
        WHERE owner ILIKE %s LIMIT 1
    """, (f"%{search_term}%",))
    res = cur.fetchone()
    
    if res:
        # Initial Ticker Guess (Refined by financial_parser later)
        return search_term.split()[0].upper()[:4]
    return None

def trigger_portfolio_expansion(ticker, asset_id):
    """
    The Alpha Move: Tell the financial_parser to find ALL other assets 
    owned by this ticker in SEC filings.
    """
    task = {
        "source": "SYNAPSE_DISCOVERY",
        "entity": ticker,
        "origin_asset_id": asset_id,
        "action": "DEEP_DISCOVERY",
        "timestamp": int(time.time() * 1000)
    }
    r_bus.lpush("filing_processing_queue", json.dumps(task))

def build_synapses():
    print("[SYNAPSE] Booting Associative Memory...", flush=True)
    conn = get_db_connection()
    cur = conn.cursor()

    # 1. Block until OSM Ingest has provided seeds
    wait_for_reality(cur)

    # 2. Map Physical Assets to Tickers
    cur.execute("SELECT id, name, commodity_types FROM assets")
    assets = cur.fetchall()
    
    processed_tickers = set()

    for asset_id, name, comm_list in assets:
        entity_key = f"ASSET_{asset_id}"
        
        # --- A. COMMODITY LAYER (Product) ---
        if comm_list:
            for comm in comm_list:
                template_key = next((k for k in COMMODITY_TEMPLATES if k in comm.lower()), None)
                if template_key:
                    tmpl = COMMODITY_TEMPLATES[template_key]
                    
                    # Register Spot + Future if not present [cite: 581, 590]
                    cur.execute("INSERT INTO ticker_registry (symbol, instrument_type, exchange, active, last_updated) VALUES (%s, 'commodity_spot', 'GLOBAL', TRUE, NOW()) ON CONFLICT DO NOTHING", (tmpl["spot"],))
                    cur.execute("INSERT INTO ticker_registry (symbol, instrument_type, exchange, multiplier, active, last_updated) VALUES (%s, 'future', %s, %s, TRUE, NOW()) ON CONFLICT DO NOTHING", (tmpl["fut"], tmpl["exch"], tmpl["mult"]))

                    # Link Sensitivities [cite: 611, 612]
                    cur.execute("INSERT INTO ticker_sensitivity (ticker_symbol, entity_id, weight) VALUES (%s, %s, 1.0) ON CONFLICT DO NOTHING", (tmpl["spot"], entity_key))
                    cur.execute("INSERT INTO ticker_sensitivity (ticker_symbol, entity_id, weight) VALUES (%s, %s, 0.8) ON CONFLICT DO NOTHING", (tmpl["fut"], entity_key))

        # --- B. CORPORATE LAYER (Owner) ---
        ticker = discover_ticker_from_registry(cur, name)
        if ticker:
            # Register discovered Stock
            cur.execute("INSERT INTO ticker_registry (symbol, instrument_type, exchange, active, last_updated) VALUES (%s, 'stock', 'SMART', TRUE, NOW()) ON CONFLICT DO NOTHING", (ticker,))
            
            # Wire the "Doorbell" for the C++ Valuation Engine [cite: 479, 480]
            cur.execute("INSERT INTO ticker_sensitivity (ticker_symbol, entity_id, weight) VALUES (%s, %s, 0.5) ON CONFLICT DO NOTHING", (ticker, entity_key))
            
            # RECURSIVE TRIGGER: Find the rest of the sector
            if ticker not in processed_tickers:
                trigger_portfolio_expansion(ticker, asset_id)
                processed_tickers.add(ticker)
                print(f"   [RECURSion] Queued portfolio expansion for: {ticker}")

    conn.commit()
    conn.close()
    print("[SYNAPSE] Initial pathways defined. Handing off to Financial Parser for sector expansion.")

if __name__ == "__main__":
    build_synapses()