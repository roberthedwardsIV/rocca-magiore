import time
import psycopg2
import redis
import json
import requests
import re
from bs4 import BeautifulSoup
from threading import Thread
from geopy.geocoders import Nominatim
from geopy.exc import GeocoderTimedOut, GeocoderServiceError

# --- CONFIG ---
DB_CONFIG = { "dbname": "rocco_commodities", "user": "rocco_admin", "password": "REMOVED", "host": "hippocampus", "port": "5432" }
geolocator = Nominatim(user_agent="RoccoMaggiore_Geolocator_v2")

# SEC requires a declared User-Agent
HTTP_HEADERS_SEC = {"User-Agent": "RoccoCapital admin@roccocapital.com"}
# Yahoo Finance requires a browser-like User-Agent
HTTP_HEADERS_YF = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"}

# --- PERMUTATION TEMPLATES (For Geocoding) ---
PERMUTATIONS = {
    "mine": ["{name} mine", "{name} operations", "{name} mining complex", "{name} open pit", "{name} project", "{name} site"],
    "refinery": ["{name} refinery", "{name} oil refinery", "{name} chemical plant", "{name} industrial complex", "{name} facility"],
    "smelter": ["{name} smelter", "{name} smelting complex", "{name} metal works", "{name} processing plant", "{name} foundry"]
}

def get_db_connection():
    return psycopg2.connect(**DB_CONFIG)

# =====================================================================
# PHASE 5: SPATIAL RESOLUTION (Geocoding Ghost Assets)
# =====================================================================
def iterative_search(asset_name, asset_type):
    candidates = [asset_name]
    if asset_type in PERMUTATIONS:
        candidates.extend([p.format(name=asset_name) for p in PERMUTATIONS[asset_type]])
    
    for query in candidates:
        try:
            location = geolocator.geocode(query, timeout=10)
            if location: return location.latitude, location.longitude
        except (GeocoderTimedOut, GeocoderServiceError):
            time.sleep(2) 
            continue
        time.sleep(1.5) # Respect rate limits strictly
    return None, None

def resolve_spatial_ghosts():
    conn = get_db_connection()
    cur = conn.cursor()
    cur.execute("SELECT id, name, type FROM assets WHERE geom IS NULL LIMIT 20")
    ghosts = cur.fetchall()

    if not ghosts:
        conn.close()
        return

    for aid, name, a_type in ghosts:
        lat, lon = iterative_search(name, a_type)
        if lat and lon:
            cur.execute("""
                UPDATE assets 
                SET geom = ST_SetSRID(ST_MakePoint(%s, %s), 4326), latitude = %s, longitude = %s, source = source || '_GEOLOCATED'
                WHERE id = %s
            """, (lon, lat, lat, lon, aid))
            print(f"      [GEO] Resolved {name} at {lat}, {lon}")
            
    conn.commit()
    conn.close()

def run_ghost_resolver_loop():
    print("[GEOLOCATION] Background Service Started.", flush=True)
    while True:
        try:
            resolve_spatial_ghosts()
            time.sleep(600)
        except Exception:
            time.sleep(60)

# =====================================================================
# PHASE 2: THE PIVOT (Ownership Resolution)
# =====================================================================
class OwnershipResolver:
    def __init__(self):
        self.r = redis.Redis(host='corpus_callosum', port=6379, db=0)

    def validate_ticker(self, ticker):
        """Tier 1.5: Validates if a ticker is an actively traded public equity."""
        if not ticker: return None
        ticker = str(ticker).split('/')[-1].upper() # Clean Wikidata URLs
        
        url = f"https://query2.finance.yahoo.com/v1/finance/search?q={ticker}&quotesCount=1&newsCount=0"
        try:
            r = requests.get(url, headers=HTTP_HEADERS_YF, timeout=5)
            if r.status_code == 200:
                quotes = r.json().get('quotes', [])
                if quotes:
                    q = quotes[0]
                    # Ensure it is a tradable stock, not a private company holding string
                    if q.get('quoteType') in ['EQUITY', 'ETF']:
                        return q.get('symbol')
        except Exception:
            pass
        return None

    def resolve_via_wikidata(self, wd_id):
        query = f"""
        SELECT ?ticker WHERE {{
          wd:{wd_id} wdt:P127|wdt:P137 ?owner .
          ?owner wdt:P249 ?ticker .
        }} LIMIT 1
        """
        url = 'https://query.wikidata.org/sparql'
        try:
            r = requests.get(url, params={'format': 'json', 'query': query}, headers=HTTP_HEADERS_SEC, timeout=10)
            results = r.json().get('results', {}).get('bindings', [])
            if results: return results[0]['ticker']['value']
        except Exception: pass
        return None

    def resolve_via_sec_api(self, search_term):
        url = "https://efts.sec.gov/LATEST/search-index"
        payload = {"keysTyped": search_term, "narrow": True}
        try:
            r = requests.post(url, json=payload, headers=HTTP_HEADERS_SEC, timeout=10)
            hits = r.json().get('hits', {}).get('hits', [])
            if hits:
                top_hit = hits[0].get('_id', '')
                parts = top_hit.split(':')
                if len(parts) >= 3 and parts[2]:
                    return parts[2].split(',')[0] 
        except Exception: pass
        return None

    def run_resolution_loop(self):
        print("[OSINT] Ownership Resolution Engine Online.", flush=True)
        while True:
            try:
                conn = get_db_connection()
                cur = conn.cursor()
                
                # Fetch assets with metadata that haven't been processed yet
                cur.execute("""
                    SELECT id, name, metadata 
                    FROM assets 
                    WHERE NOT (metadata ? 'mapped')
                    LIMIT 500
                """)
                orphans = cur.fetchall()
                
                for aid, name, meta in orphans:
                    # Skip alphanumeric engineering codes (e.g., TA.CMA-9.HNI-4)
                    name_str = str(name).strip()
                    if name_str.count('.') >= 2 or name_str.count('-') >= 2:
                        cur.execute("UPDATE assets SET metadata = jsonb_set(metadata, '{mapped}', 'true') WHERE id = %s", (aid,))
                        conn.commit()
                        continue

                    raw_ticker = None
                    if 'wikidata' in meta:
                        raw_ticker = self.resolve_via_wikidata(meta['wikidata'])
                    if not raw_ticker:
                        search_term = meta.get('operator') or meta.get('company') or name_str
                        if search_term:
                            # Clean generic terms to improve SEC API hit rate
                            clean_term = re.sub(r'(?i)\b(colliery|quarry|mine|project|operations)\b', '', search_term).strip()
                            raw_ticker = self.resolve_via_sec_api(clean_term)
                            time.sleep(0.15)

                    # --- PUBLIC MARKET GATE ---
                    valid_ticker = self.validate_ticker(raw_ticker)

                    if valid_ticker:
                        print(f"   [PIVOT SUCCESS] Mapped '{name_str}' to Public Ticker: {valid_ticker}")
                        
                        # 1. Register Ticker
                        cur.execute("INSERT INTO ticker_registry (symbol, instrument_type, exchange, active, last_updated) VALUES (%s, 'stock', 'SMART', TRUE, NOW()) ON CONFLICT DO NOTHING", (valid_ticker,))
                        
                        # 2. Map Asset to Ticker for the Brainstem/Signal Engine
                        entity_key = f"ASSET_{aid}"
                        cur.execute("INSERT INTO ticker_sensitivity (ticker_symbol, entity_id, weight) VALUES (%s, %s, 1.0) ON CONFLICT DO NOTHING", (valid_ticker, entity_key))

                        # 3. Trigger 10-K Scraping
                        task = {
                            "source": "OWNERSHIP_PIVOT",
                            "entity": valid_ticker,
                            "origin_asset_id": aid,
                            "action": "DEEP_DISCOVERY",
                            "timestamp": int(time.time()*1000)
                        }
                        self.r.lpush("filing_processing_queue", json.dumps(task))
                    
                    # NOTE: We silently ignore failures here. State-owned and private assets are expected.
                    
                    # Mark as processed in DB
                    cur.execute("UPDATE assets SET metadata = jsonb_set(metadata, '{mapped}', 'true') WHERE id = %s", (aid,))
                    conn.commit()

                conn.close()
                
                if orphans:
                    time.sleep(5) 
                else:
                    print("[OSINT] Backlog cleared. Sleeping for 1 hour.")
                    time.sleep(3600) 
            except Exception as e:
                print(f"[OSINT ERR] {e}")
                time.sleep(60)

# =====================================================================
# PHASE 3 & 4: NLP FILING SCRAPER
# =====================================================================
class FilingProcessor:
    def __init__(self):
        self.r = redis.Redis(host='corpus_callosum', port=6379, db=0)
        self.cik_map = {}
        self.load_cik_map()

    def load_cik_map(self):
        try:
            print("[FINANCE] Downloading SEC CIK Map...")
            r = requests.get("https://www.sec.gov/files/company_tickers.json", headers=HTTP_HEADERS_SEC, timeout=10)
            for key, val in r.json().items():
                self.cik_map[val['ticker']] = str(val['cik_str']).zfill(10)
        except Exception as e:
            print(f"[FINANCE ERR] Failed to load CIK map: {e}")

    def fetch_latest_10k_url(self, ticker):
        cik = self.cik_map.get(ticker)
        if not cik: return None
        url = f"https://data.sec.gov/submissions/CIK{cik}.json"
        try:
            r = requests.get(url, headers=HTTP_HEADERS_SEC, timeout=10)
            filings = r.json().get('filings', {}).get('recent', {})
            for i, form in enumerate(filings.get('form', [])):
                if form == "10-K":
                    acc_clean = filings['accessionNumber'][i].replace("-", "")
                    doc = filings['primaryDocument'][i]
                    return f"https://www.sec.gov/Archives/edgar/data/{int(cik)}/{acc_clean}/{doc}"
        except Exception: pass
        return None

    def extract_assets_from_html(self, url):
        try:
            r = requests.get(url, headers=HTTP_HEADERS_SEC, timeout=20)
            soup = BeautifulSoup(r.content, 'lxml')
            text = soup.get_text(separator=' ', strip=True)
            
            pattern = r'\b([A-Z][A-Za-z0-9\-]+(?:\s+[A-Z][A-Za-z0-9\-]+){0,3})\s+(Mine|Refinery|Smelter|Complex|Processing Plant|Concentrator)\b'
            matches = set(re.findall(pattern, text))
            
            discovered = []
            for match in matches:
                name = f"{match[0]} {match[1]}"
                a_type = "mine"
                if "Refinery" in match[1]: a_type = "refinery"
                elif "Smelter" in match[1]: a_type = "smelter"
                discovered.append({"name": name, "type": a_type})
            return discovered
        except Exception: return []

    def create_ghost_asset(self, name, a_type, owner_ticker):
        conn = get_db_connection()
        try:
            with conn.cursor() as cur:
                cur.execute("SELECT 1 FROM assets WHERE name = %s", (name,))
                if cur.fetchone(): return

                print(f"      [NLP DISCOVERY] Found new asset: {name} (Owned by {owner_ticker})")
                meta = json.dumps({"owner_ticker": owner_ticker, "mapped": True})
                cur.execute("""
                    INSERT INTO assets (name, type, commodity_types, source, last_update, op_health, metadata)
                    VALUES (%s, %s, '{"Unknown"}', 'SEC_NLP', %s, 1.0, %s)
                    ON CONFLICT DO NOTHING
                """, (name, a_type, int(time.time()), meta))
                conn.commit()
        except Exception as e:
            conn.rollback()
        finally:
            conn.close()

    def process_queue(self):
        print("[FINANCE] Filing Extraction Queue Online...", flush=True)
        while True:
            try:
                _, data = self.r.brpop("filing_processing_queue")
                task = json.loads(data)
                
                entity_name = task.get('entity')
                action = task.get('action')
                
                if action == "DEEP_DISCOVERY" and entity_name:
                    print(f"[FINANCE] Executing Deep Discovery for {entity_name}...")
                    doc_url = self.fetch_latest_10k_url(entity_name)
                    if not doc_url: continue
                    
                    print(f"   -> Scraping 10-K: {doc_url}")
                    discovered_assets = self.extract_assets_from_html(doc_url)
                    
                    for asset in discovered_assets:
                        bad_words = ["The", "This", "Our", "Any", "New", "A"]
                        if asset['name'].split()[0] in bad_words: continue
                        self.create_ghost_asset(asset['name'], asset['type'], entity_name)
                        
                    time.sleep(2) 
            except Exception as e:
                time.sleep(5)

if __name__ == "__main__":
    Thread(target=run_ghost_resolver_loop, daemon=True).start()
    pivot_engine = OwnershipResolver()
    Thread(target=pivot_engine.run_resolution_loop, daemon=True).start()
    processor = FilingProcessor()
    processor.process_queue()