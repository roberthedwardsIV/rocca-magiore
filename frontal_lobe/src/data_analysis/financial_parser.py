import time
import psycopg2
import redis
import json
import requests
import re
import pandas as pd
from bs4 import BeautifulSoup
import warnings
from bs4 import XMLParsedAsHTMLWarning
from threading import Thread
from geopy.geocoders import Nominatim
from geopy.exc import GeocoderTimedOut, GeocoderServiceError

warnings.filterwarnings("ignore", category=XMLParsedAsHTMLWarning)

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
# PROGRESS DASHBOARD THREAD
# =====================================================================
def run_progress_dashboard():
    """Prints a real-time status report of the NLP pipeline every 60 seconds."""
    print("[DASHBOARD] Progress Tracker Online.", flush=True)
    r = redis.Redis(host='corpus_callosum', port=6379, db=0)
    
    while True:
        time.sleep(60) 
        conn = None
        try:
            conn = get_db_connection()
            cur = conn.cursor()
            
            # 1. Total valid assets
            cur.execute("SELECT count(*) FROM assets WHERE type IN ('mine', 'refinery', 'smelter')")
            total_assets = cur.fetchone()[0]
            
            # 2. Assets mapped to an owner
            cur.execute("SELECT count(*) FROM assets WHERE metadata->>'mapped' = 'true'")
            mapped_assets = cur.fetchone()[0]
            
            # 3. Assets with successfully parsed tonnages
            cur.execute("SELECT count(DISTINCT asset_id) FROM quarterly_financials")
            parsed_assets = cur.fetchone()[0]
            
            # 4. Redis Queue Depth
            queue_len = r.llen("filing_processing_queue")
            
            coverage_pct = (parsed_assets / total_assets * 100) if total_assets > 0 else 0
            mapped_pct = (mapped_assets / total_assets * 100) if total_assets > 0 else 0
            
            print("\n" + "="*60, flush=True)
            print("[FINANCIAL NLP PARSER - PROGRESS DASHBOARD]", flush=True)
            print(f" -> Ownership Mapping:  {mapped_assets} / {total_assets} ({mapped_pct:.1f}%)", flush=True)
            print(f" -> Ticker Scrape Queue:{queue_len} pending SEC 10-K lookups", flush=True)
            print(f" -> NLP Extraction:     {parsed_assets} / {total_assets} ({coverage_pct:.1f}%)", flush=True)
            
            # Solver status indicator
            if coverage_pct >= 20.0:
                print(f" -> Solver Status:      🟢 UNLOCKED (Threshold Met)", flush=True)
            else:
                print(f" -> Solver Status:      🔴 LOCKED (Target: 20.0%)", flush=True)
            print("="*60 + "\n", flush=True)
            
        except Exception as e:
            print(f"[DASHBOARD ERR] {e}", flush=True)
        finally:
            if conn: conn.close()

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
        self.sec_company_map = self._load_sec_crosswalk()
    
    def _load_sec_crosswalk(self):
        print("[OSINT] Downloading SEC Company Crosswalk...", flush=True)
        company_map = {}
        try:
            r = requests.get("https://www.sec.gov/files/company_tickers.json", headers=HTTP_HEADERS_SEC, timeout=10)
            if r.status_code == 200:
                for val in r.json().values():
                    clean_title = re.sub(r'(?i)\b(INC|CORP|LTD|LLC|PLC|COMPANY|CO)\b|\.', '', val['title']).strip().lower()
                    company_map[clean_title] = val['ticker']
        except Exception as e:
            print(f"[OSINT ERR] Failed to load SEC crosswalk: {e}", flush=True)
        return company_map
    
    def validate_ticker(self, ticker):
        if not ticker: return None
        ticker = str(ticker).split('/')[-1].upper() 
        
        url = f"https://query2.finance.yahoo.com/v1/finance/search?q={ticker}&quotesCount=1&newsCount=0"
        try:
            r = requests.get(url, headers=HTTP_HEADERS_YF, timeout=5)
            if r.status_code == 200:
                quotes = r.json().get('quotes', [])
                if quotes:
                    q = quotes[0]
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
        GLOBAL_OPERATORS = {
            "bhp": "BHP", "rio tinto": "RIO", "vale": "VALE", 
            "glencore": "GLNCY", "freeport": "FCX", "southern copper": "SCCO", 
            "teck": "TECK", "anglo american": "NGLOY", "first quantum": "FQVLF",
            "kghm": "KGQYF", "ivanhoe": "IVPAF", "lundin": "LUNMF",
            "barrick": "GOLD", "newmont": "NEM", "agnico": "AEM", 
            "kinross": "KGC", "gold fields": "GFI", "anglogold": "AU",
            "alcoa": "AA", "albemarle": "ALB", "sqm": "SQM", "pilbara": "PLL",
            "century aluminum": "CENX", "norsk hydro": "NHYDY"
        }

        while True:
            try:
                conn = get_db_connection()
                cur = conn.cursor()
                
                cur.execute("""
                    SELECT id, name, metadata 
                    FROM assets 
                    WHERE metadata IS NULL OR (metadata->>'mapped') IS NULL
                    LIMIT 500
                """)
                orphans = cur.fetchall()
                
                for aid, name, meta in orphans:
                    name_str = str(name).strip()
                    if name_str.count('.') >= 2 or name_str.count('-') >= 2:
                        cur.execute("UPDATE assets SET metadata = jsonb_set(metadata, '{mapped}', 'true') WHERE id = %s", (aid,))
                        conn.commit()
                        continue

                    raw_ticker = None
                    search_term = meta.get('operator') or meta.get('company') or name_str
                    
                    if search_term:
                        search_lower = search_term.lower()
                        for key, ticker in GLOBAL_OPERATORS.items():
                            if key in search_lower:
                                raw_ticker = ticker
                                break

                    if not raw_ticker and 'wikidata' in meta:
                        raw_ticker = self.resolve_via_wikidata(meta['wikidata'])
                    
                    if not raw_ticker and search_term:
                        clean_search = re.sub(r'(?i)\b(colliery|quarry|mine|project|operations|inc|corp|ltd|llc|plc|company|co)\b|\.', '', search_term).strip().lower()
                        for sec_name, ticker in self.sec_company_map.items():
                            if len(sec_name) > 4:
                                if clean_search == sec_name or re.search(rf'\b{re.escape(sec_name)}\b', clean_search):
                                    raw_ticker = ticker
                                    break

                    if not raw_ticker and search_term:
                        clean_term = re.sub(r'(?i)\b(colliery|quarry|mine|project|operations)\b', '', search_term).strip()
                        raw_ticker = self.resolve_via_sec_api(clean_term)
                        time.sleep(0.15)
                    
                    valid_ticker = self.validate_ticker(raw_ticker)

                    if valid_ticker:
                        print(f"   [PIVOT SUCCESS] Mapped '{name_str}' to Public Ticker: {valid_ticker}")
                        
                        meta_payload = json.dumps({"company": search_term, "asset_id": aid})
                        cur.execute("""
                            INSERT INTO ticker_registry 
                            (symbol, instrument_type, exchange, multiplier, active, metadata, last_updated) 
                            VALUES (%s, 'stock', 'SMART', 1.0, TRUE, %s::jsonb, NOW()) 
                            ON CONFLICT DO NOTHING
                        """, (valid_ticker, meta_payload))
                        
                        entity_key = f"ASSET_{aid}"
                        cur.execute("""
                            INSERT INTO ticker_sensitivity 
                            (ticker_symbol, entity_id, weight) 
                            VALUES (%s, %s, 1.0) 
                            ON CONFLICT DO NOTHING
                        """, (valid_ticker, entity_key))

                        task = {
                            "source": "OWNERSHIP_PIVOT",
                            "entity": valid_ticker,
                            "origin_asset_id": aid,
                            "action": "DEEP_DISCOVERY",
                            "timestamp": int(time.time()*1000)
                        }
                        self.r.lpush("filing_processing_queue", json.dumps(task))
                    
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
# PHASE 3 & 4: NLP FILING SCRAPER & OPERATIONAL EXTRACTION
# =====================================================================
class FilingProcessor:
    def __init__(self):
        self.r = redis.Redis(host='corpus_callosum', port=6379, db=0)
        self.cik_map = {}
        self.load_cik_map()
        self._init_financial_table()

    def _init_financial_table(self):
        """Ensures the quarterly_financials table exists for the solver constraints."""
        conn = get_db_connection()
        cur = conn.cursor()
        cur.execute("""
            CREATE TABLE IF NOT EXISTS quarterly_financials (
                asset_id INT,
                quarter VARCHAR(20),
                reported_production_tonnes NUMERIC,
                reported_freight_expense_usd NUMERIC,
                PRIMARY KEY (asset_id, quarter)
            );
        """)
        conn.commit()
        conn.close()

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
            time.sleep(0.2) 
            r = requests.get(url, headers=HTTP_HEADERS_SEC, timeout=10)
            if r.status_code != 200: return None
                
            filings = r.json().get('filings', {}).get('recent', {})
            for i, form in enumerate(filings.get('form', [])):
                if form == "10-K":
                    acc_clean = filings['accessionNumber'][i].replace("-", "")
                    doc = filings['primaryDocument'][i]
                    return f"https://www.sec.gov/Archives/edgar/data/{int(cik)}/{acc_clean}/{doc}"
        except Exception as e:
            pass
        return None

    def fetch_html(self, url):
        """Downloads the raw HTML content once to be used by both extractors."""
        try:
            time.sleep(0.2)
            r = requests.get(url, headers=HTTP_HEADERS_SEC, timeout=20)
            if r.status_code == 200:
                return r.content
        except Exception as e:
            print(f"      [SEC API ERR] HTML Fetch failed: {e}")
        return None

    def extract_assets_from_html(self, html_content):
        """Finds new 'ghost' assets in the text."""
        try:
            soup = BeautifulSoup(html_content, 'lxml')
            text = soup.get_text(separator=' ', strip=True)
            
            pattern = r'\b([A-Z][A-Za-z0-9\-]+(?:\s+[A-Z][A-Za-z0-9\-]+){0,3})\s+(Mine|Refinery|Smelter|Complex|Processing Plant|Concentrator)\b'
            matches = set(re.findall(pattern, text))
            
            generic_terms = {
                "bauxite", "alumina", "copper", "gold", "silver", "iron", "coal", 
                "lithium", "nickel", "zinc", "lead", "metal", "the", "this", "our", 
                "any", "new", "a", "an", "operations", "project"
            }

            discovered = []
            for match in matches:
                prefix = match[0].strip()
                if len(prefix.split()) == 1 and prefix.lower() in generic_terms:
                    continue
                name = f"{match[0]} {match[1]}"
                a_type = "mine"
                if "Refinery" in match[1]: a_type = "refinery"
                elif "Smelter" in match[1]: a_type = "smelter"
                discovered.append({"name": name, "type": a_type})
            return discovered
        except Exception as e:
            return []

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

    # --- THE HARDCORE EXTRACTION SUITE ---
    def _clean_number(self, val_str, unit_str=''):
        """Converts extracted text into raw metric tonnes, handling hardcore mining units."""
        try:
            # Strip everything except digits and decimal points (handle commas)
            clean_str = re.sub(r'[^\d\.]', '', str(val_str).replace(',', ''))
            if not clean_str: return None
            val = float(clean_str)
            unit = str(unit_str).lower()
            
            # Multipliers for millions/thousands
            if any(x in unit for x in ['m ', 'million', 'mt', 'mtpa']): val *= 1000000
            elif any(x in unit for x in ['k ', 'thousand', 'kt', 'ktpa', 'koz']): val *= 1000
            
            # Exotic Conversions (LCE is already in tonnes usually, just needs scale)
            if 'lb' in unit or 'pound' in unit: val *= 0.000453592  # Copper/Uranium
            elif 'oz' in unit or 'ounce' in unit: val *= 0.0000283495 # Gold/Silver
            
            return val
        except Exception:
            return None

    def parse_custom_tables(self, html_content, search_name):
        """Manually hacks through HTML tables to find asset rows and capacity numbers."""
        soup = BeautifulSoup(html_content, 'lxml')
        tables = soup.find_all('table')
        
        for table in tables:
            text_content = table.get_text(separator=' ', strip=True).lower()
            if search_name.lower() not in text_content: continue
            
            # Only process tables that look like production/capacity summaries
            if not any(kw in text_content for kw in ['capacity', 'production', 'output', 'tonnes', 'lce', 'tpa']):
                continue
                
            rows = table.find_all('tr')
            for row in rows:
                row_text = row.get_text(separator=' ', strip=True)
                if search_name.lower() in row_text.lower():
                    # Look for a number and unit in this specific table row
                    match = re.search(r'([\d,\.]+)\s*(million|thousand|kilo)?\s*(Mt|kt|tons|tonnes|tpa|mtpa|ktpa|lce|oz|ounces|lbs|pounds|metric)', row_text, re.IGNORECASE)
                    if match:
                        return self._clean_number(match.group(1), match.group(0))
        return None

    def parse_text_proximity(self, html_content, search_name):
        """Scans raw text paragraphs for production context."""
        soup = BeautifulSoup(html_content, 'lxml')
        clean_text = soup.get_text(separator=' ')
        clean_text = re.sub(r'\s+', ' ', clean_text) 
        
        try:
            # Grab 200 chars around the asset name
            pattern = re.compile(rf"(.{{0,200}}?\b{re.escape(search_name)}\b.{{0,200}}?)", re.IGNORECASE)
            matches = pattern.findall(clean_text)
            
            for context in matches:
                # Look for capacity/production + number + mining unit
                num_pattern = re.compile(r"(?:production|capacity|output|guidance).{0,30}?([\d,\.]+)\s*(million|thousand|kilo)?\s*(Mt|kt|tons|tonnes|tpa|mtpa|ktpa|lce|oz|ounces|lbs|pounds|metric)", re.IGNORECASE)
                num_match = num_pattern.search(context)
                if num_match:
                    return self._clean_number(num_match.group(1), num_match.group(0))
                    
                # Desperation Pass: Just look for a number and unit near the asset name
                desp_pattern = re.compile(r"([\d,\.]+)\s*(million|thousand|kilo)?\s*(Mt|kt|tons|tonnes|tpa|mtpa|ktpa|lce|oz|ounces|lbs|pounds|metric)", re.IGNORECASE)
                desp_match = desp_pattern.search(context)
                if desp_match:
                    return self._clean_number(desp_match.group(1), desp_match.group(0))
        except Exception: pass
        return None

    def extract_operational_data(self, ticker, html_content):
        """Finds production constraints for all assets owned by this ticker."""
        conn = get_db_connection()
        try:
            cur = conn.cursor()
            # Fetch all known assets linked to this parent company
            cur.execute("""
                SELECT DISTINCT a.id, a.name 
                FROM assets a
                LEFT JOIN ticker_sensitivity ts ON ts.entity_id = 'ASSET_' || a.id
                WHERE ts.ticker_symbol = %s OR a.metadata->>'owner_ticker' = %s
            """, (ticker, ticker))
            assets = cur.fetchall()

            print(f"      [DEBUG] Hunting capacity data for {len(assets)} assets under {ticker}...", flush=True)

            success_count = 0
            for aid, name in assets:
                # Core Fix: Strip generic words for matching
                search_name = re.sub(r'(?i)\b(mine|refinery|smelter|complex|project|plant|operations|salar de)\b', '', name).strip()
                if len(search_name) < 4: search_name = name # Fallback if name is tiny
                
                # Try Tables first, then Text
                prod_tonnes = self.parse_custom_tables(html_content, search_name)
                if not prod_tonnes:
                    prod_tonnes = self.parse_text_proximity(html_content, search_name)
                
                if prod_tonnes:
                    success_count += 1
                    print(f"        -> [SUCCESS] Found {name}: {prod_tonnes:,.0f} MT", flush=True)
                    cur.execute("""
                        INSERT INTO quarterly_financials (asset_id, quarter, reported_production_tonnes, reported_freight_expense_usd)
                        VALUES (%s, '2025-CURRENT', %s, 0)
                        ON CONFLICT (asset_id, quarter) DO UPDATE 
                        SET reported_production_tonnes = EXCLUDED.reported_production_tonnes;
                    """, (aid, prod_tonnes))
            
            print(f"      [DEBUG] Extracted {success_count}/{len(assets)} assets for {ticker}.", flush=True)
            conn.commit()
        except Exception as e:
            print(f"      [DEBUG ERR] {e}", flush=True)
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
                    html_content = self.fetch_html(doc_url)
                    if not html_content: continue
                    
                    # 1. Discover and save new ghost assets
                    discovered_assets = self.extract_assets_from_html(html_content)
                    for asset in discovered_assets:
                        bad_words = ["The", "This", "Our", "Any", "New", "A"]
                        if asset['name'].split()[0] in bad_words: continue
                        self.create_ghost_asset(asset['name'], asset['type'], entity_name)
                        
                    # 2. Rip operational data for all assets tied to this company
                    self.extract_operational_data(entity_name, html_content)

                    time.sleep(2) 
            except Exception as e:
                time.sleep(5)

if __name__ == "__main__":
    # Start the new Progress Dashboard
    Thread(target=run_progress_dashboard, daemon=True).start()
    
    Thread(target=run_ghost_resolver_loop, daemon=True).start()
    pivot_engine = OwnershipResolver()
    Thread(target=pivot_engine.run_resolution_loop, daemon=True).start()
    processor = FilingProcessor()
    processor.process_queue()