import redis
import json
import time
import requests
import re
from bs4 import BeautifulSoup
import psycopg2
import sys

# --- CONFIG ---
REDIS_HOST = "corpus_callosum"
DB_CONFIG = {
    "dbname": "rocco_commodities",
    "user": "rocco_admin",
    "password": "REMOVED",
    "host": "hippocampus",
    "port": "5432"
}

# --- ASSET-SPECIFIC PATTERNS ---
ASSET_CONFIGS = {
    "mine": {
        "production": [
            r"(?:copper|gold|ore|metal)\s+production\s+[:\-]?\s*([\d,.]+)\s*(?:kt|tonnes|tons|koz)",
            r"mined\s+([\d,.]+)\s+(?:tonnes|tons)",
            r"head\s+grade\s+[:\-]?\s*([\d,.]+)\s*%" 
        ],
        "costs": [
            r"cash\s+costs?\s+(?:C1)?\s*[:\-]?\s*\$\s*([\d,.]+)", # C1 Cash Cost
            r"AISC\s+[:\-]?\s*\$\s*([\d,.]+)",                    # All-in Sustaining Cost
            r"unit\s+cost\s+[:\-]?\s*\$\s*([\d,.]+)"
        ],
        "reserves": [
            r"(?:proven|probable)\s+reserves\s+[:\-]?\s*([\d,.]+)\s*(?:Mt|million\s+tonnes)",
            r"life\s+of\s+mine\s+[:\-]?\s*(\d+)\s+years"
        ],
        "yields": [ # Mining Recovery
            r"metallurgical\s+recovery\s+[:\-]?\s*([\d,.]+)\s*%",
            r"concentrator\s+recovery\s+[:\-]?\s*([\d,.]+)\s*%",
            r"average\s+recovery\s+[:\-]?\s*([\d,.]+)\s*%"
        ]
    },
    "refinery": {
        "throughput": [ # The volume processed (Input)
            r"throughput\s+[:\-]?\s*([\d,.]+)\s*(?:kt|tonnes)",
            r"processed\s+([\d,.]+)\s+(?:tonnes|concentrate)",
            r"anode\s+production\s+[:\-]?\s*([\d,.]+)"
        ],
        "yields": [ # Processing Efficiency
            r"recovery\s+rate\s+[:\-]?\s*([\d,.]+)\s*%",
            r"copper\s+yield\s+[:\-]?\s*([\d,.]+)\s*%"
        ],
        "economics": [ # The "Price" of refining (TC/RCs)
            r"treatment\s+charges?\s+[:\-]?\s*\$\s*([\d,.]+)",
            r"TC/RCs?\s+[:\-]?\s*\$\s*([\d,.]+)",
            r"refining\s+charges?\s+[:\-]?\s*([\d,.]+)\s*(?:cents|c/lb)"
        ]
    }
}

# Common patterns (Freight/Revenue apply to everyone)
COMMON_PATTERNS = {
    "freight": [
        r"freight\s+expense\s+[:\-]?\s*\$\s*([\d,.]+)\s*(?:million)?",
        r"shipping\s+costs\s+[:\-]?\s*\$\s*([\d,.]+)\s*(?:million)?",
        r"transportation\s+costs\s+[:\-]?\s*\$\s*([\d,.]+)"
    ],
    "revenue": [
        r"segment\s+revenue\s+[:\-]?\s*\$\s*([\d,.]+)",
        r"realized\s+price\s+[:\-]?\s*\$\s*([\d,.]+)"
    ]
}

def get_db_connection():
    try:
        return psycopg2.connect(**DB_CONFIG)
    except Exception as e:
        print(f"[PARSER] DB Connection Failed: {e}", flush=True)
        return None

def download_filing(url):
    headers = {"User-Agent": "RoccoCapital admin@roccocapital.com"}
    try:
        r = requests.get(url, headers=headers, timeout=15)
        if r.status_code == 200:
            return r.text
        else:
            print(f"[PARSER] HTTP {r.status_code} for {url}", flush=True)
    except Exception as e:
        print(f"[PARSER] Download failed: {e}", flush=True)
    return None

def extract_section(text, entity_name, window_chars=4000):
    """
    Locates the specific section of the 10-K where the Asset is discussed.
    Returns a 'Context Window' string to prevent matching data from the wrong mine.
    Increased window size to catch tables that might be far below the header.
    """
    text_lower = text.lower()
    entity_lower = entity_name.lower()
    
    # Find all occurrences of the asset name
    indices = [m.start() for m in re.finditer(re.escape(entity_lower), text_lower)]
    
    if not indices:
        return "" 
    
    combined_text = ""
    for idx in indices:
        start = max(0, idx - 500) # Look back for headers
        end = min(len(text), idx + window_chars)
        combined_text += text[start:end] + "\n---SECTION BREAK---\n"
        
    return combined_text

def extract_metric(text, patterns):
    for p in patterns:
        match = re.search(p, text, re.IGNORECASE)
        if match:
            raw_num = match.group(1).replace(",", "")
            try:
                val = float(raw_num)
                # Heuristic: Detect "Million" scaling within the capture group context
                if "million" in match.group(0).lower(): val *= 1_000_000
                return val
            except: continue
    return 0.0

def process_task(r_client, task):
    print(f"[PARSER] Processing {task['entity']} ({task['form_type']})...", flush=True)
    
    # 1. Identify Asset Type from DB
    conn = get_db_connection()
    if not conn: return
    cur = conn.cursor()
    
    cur.execute("SELECT id, commodity_types FROM assets WHERE name ILIKE %s LIMIT 1", (f"%{task['entity']}%",))
    res = cur.fetchone()
    
    if not res:
        print(f"[PARSER] Warning: Asset '{task['entity']}' not found in DB.", flush=True)
        conn.close()
        return

    asset_id, comm_types = res
    # Heuristic: If it produces 'Cathode' or 'Metal', it's a Refinery. If 'Ore', it's a Mine.
    is_refinery = any("cathode" in t.lower() or "metal" in t.lower() for t in comm_types)
    asset_type = "refinery" if is_refinery else "mine"
    
    print(f"   -> Identified as {asset_type.upper()} (ID: {asset_id})", flush=True)

    # 2. Download & Clean
    raw_content = download_filing(task['url'])
    if not raw_content: 
        conn.close()
        return

    soup = BeautifulSoup(raw_content, 'lxml')
    full_text = soup.get_text(" ", strip=True)

    # 3. Context Isolation
    context_text = extract_section(full_text, task['entity'])
    
    if not context_text:
        print(f"   -> Asset name '{task['entity']}' not found in document text.", flush=True)
        conn.close()
        return

    # 4. Forensic Extraction
    data = {}
    
    # Common Fields
    data['freight'] = extract_metric(context_text, COMMON_PATTERNS['freight'])
    data['revenue'] = extract_metric(context_text, COMMON_PATTERNS['revenue'])

    # Specific Fields
    if asset_type == "mine":
        data['production'] = extract_metric(context_text, ASSET_CONFIGS['mine']['production'])
        data['cost_unit']  = extract_metric(context_text, ASSET_CONFIGS['mine']['costs'])
        data['reserves']   = extract_metric(context_text, ASSET_CONFIGS['mine']['reserves'])
        data['recovery']   = extract_metric(context_text, ASSET_CONFIGS['mine']['yields'])
        
        print(f"   -> MINE DATA: Prod={data['production']}, Rec={data['recovery']}%, Cost=${data['cost_unit']}")
        
    elif asset_type == "refinery":
        data['throughput'] = extract_metric(context_text, ASSET_CONFIGS['refinery']['throughput'])
        data['recovery']   = extract_metric(context_text, ASSET_CONFIGS['refinery']['yields'])
        data['tcrc']       = extract_metric(context_text, ASSET_CONFIGS['refinery']['economics'])
        
        print(f"   -> REF DATA: Thru={data['throughput']}, Rec={data['recovery']}%, TC/RC=${data['tcrc']}")

    if data['freight'] > 0:
        print(f"   -> FREIGHT FOUND: ${data['freight']}")

    # 5. Save & Signal
    # Update Quarterly Financials (Ground Truth)
    # We map 'production' to whatever the primary output metric was (production OR throughput)
    primary_vol = data.get('production', data.get('throughput', 0))
    
    cur.execute("""
        INSERT INTO quarterly_financials 
        (asset_id, quarter, reported_production_tonnes, reported_freight_expense_usd, 
         reported_realized_price, reported_tcrc_expense, source_doc)
        VALUES (%s, '2025-CURRENT', %s, %s, %s, %s, %s)
    """, (asset_id, primary_vol, data['freight'], data['revenue'], data.get('tcrc', 0), task['url']))
    
    conn.commit()
    
    # Broadcast Signal to Thalamus
    # We send the full 'data' dict so MineAsset.cpp can pick what it needs (e.g. reserves, recovery)
    signal = {
        "entity_type": "filing_update",
        "asset_id": asset_id,
        "asset_type": asset_type,
        "data": data,
        "timestamp": int(time.time() * 1000)
    }
    r_client.lpush("raw_signals", json.dumps(signal))
    
    conn.close()

def run_worker():
    print("[PARSER] Financial Parsing Engine Online (Asset-Aware v2).", flush=True)
    
    while True:
        try:
            r = redis.Redis(host=REDIS_HOST, port=6379)
            if r.ping(): break
        except:
            print("[PARSER] Waiting for Redis...", flush=True)
            time.sleep(2)

    while True:
        try:
            _, data = r.brpop("filing_processing_queue", timeout=0)
            task = json.loads(data)
            process_task(r, task)
        except Exception as e:
            print(f"[PARSER] Processing Loop Error: {e}", flush=True)
            time.sleep(1)

if __name__ == "__main__":
    run_worker()