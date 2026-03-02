# VVV FILE: ./frontal_lobe/src/data_analysis/financial_parser.py VVV
import time
import psycopg2
from psycopg2.extras import execute_batch
import redis
import json
import requests
import re
import unicodedata
import warnings
from threading import Thread
from geopy.geocoders import Nominatim
from geopy.exc import GeocoderTimedOut, GeocoderServiceError

# --- NEW SEC STACK ---
from unstructured.partition.html import partition_html
from openai import OpenAI
from pydantic import BaseModel, Field
from typing import List, Optional
from edgar import set_identity, Company
from bs4 import BeautifulSoup

# =====================================================================
# SYSTEM CONFIGURATION & GLOBAL VARIABLES
# =====================================================================
DB_CONFIG = { "dbname": "rocco_commodities", "user": "rocco_admin", "password": "REMOVED", "host": "hippocampus", "port": "5432" }
geolocator = Nominatim(user_agent="RoccoMaggiore_Geolocator_v30")
HTTP_HEADERS_SEC = {"User-Agent": "RoccoCapital admin@roccocapital.com"}
TARGET_SIC_PREFIXES = ('10', '281', '33')

# --- OPENAI CONFIGURATION ---
OPENAI_API_KEY = "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"  # <--- REPLACE THIS
client = OpenAI(api_key=OPENAI_API_KEY)

def get_db_connection():
    return psycopg2.connect(**DB_CONFIG)

def heal_corrupted_database():
    try:
        conn = get_db_connection()
        cur = conn.cursor()
        cur.execute("UPDATE assets SET name = metadata->>'original_name' WHERE metadata ? 'original_name' AND metadata->>'original_name' != name;")
        
        cur.execute("""
            CREATE TABLE IF NOT EXISTS corporate_financials (
                ticker VARCHAR(10) NOT NULL,
                period VARCHAR(50) NOT NULL,
                revenue_usd NUMERIC,
                ebitda_usd NUMERIC,
                capex_usd NUMERIC,
                debt_usd NUMERIC,
                source_doc TEXT,
                last_updated TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                UNIQUE(ticker, period)
            );
        """)

        cur.execute("""
            ALTER TABLE quarterly_financials
            DROP COLUMN IF EXISTS revenue, DROP COLUMN IF EXISTS ebitda, DROP COLUMN IF EXISTS production_vol,
            DROP COLUMN IF EXISTS revenue_usd, DROP COLUMN IF EXISTS debt_usd, DROP COLUMN IF EXISTS reported_freight_expense_usd,
            DROP COLUMN IF EXISTS reported_realized_price, DROP COLUMN IF EXISTS reported_tcrc_expense,
            DROP COLUMN IF EXISTS capacity_tonnes, DROP COLUMN IF EXISTS throughput_tonnes;
        """)
        conn.commit()
        conn.close()

        r = redis.Redis(host='corpus_callosum', port=6379, db=0)
        r.delete("filing_processing_queue")
    except Exception: 
        pass

# =====================================================================
# DETERMINISTIC SEC XBRL FETCHER (Phase 1)
# =====================================================================
def fetch_corporate_financials_xbrl(cik):
    """
    Fetches exact GAAP/IFRS metrics directly from the SEC's iXBRL parser.
    Zero hallucination risk. Supports Foreign Private Issuers and CAD/AUD/GBP.
    """
    url = f"https://data.sec.gov/api/xbrl/companyfacts/CIK{str(cik).zfill(10)}.json"
    
    # Hardened 3-attempt retry loop for SEC API
    r = None
    for attempt in range(3):
        try:
            r = requests.get(url, headers=HTTP_HEADERS_SEC, timeout=20)
            if r.status_code == 200: break
            if r.status_code == 404: 
                print(f"      [-] No XBRL company facts found for CIK {cik} (404).", flush=True)
                return None
            time.sleep(2)
        except:
            time.sleep(2)
            
    if not r or r.status_code != 200:
        print(f"      [-] XBRL Fetch Failed. HTTP Status: {r.status_code if r else 'Timeout'}", flush=True)
        return None
        
    facts = r.json().get("facts", {})
    gaap = facts.get("us-gaap", {})
    ifrs = facts.get("ifrs-full", {}) 
    
    def _get_latest_fact(possible_tags):
        for tag in possible_tags:
            tag_data = gaap.get(tag) or ifrs.get(tag)
            if tag_data:
                try:
                    units_dict = tag_data.get("units", {})
                    if not units_dict: continue
                    
                    # Support foreign reporting currencies (CAD, AUD, EUR, GBP)
                    valid_currencies = [k for k in units_dict.keys() if k in ["USD", "CAD", "AUD", "GBP", "EUR"]]
                    unit_key = valid_currencies[0] if valid_currencies else list(units_dict.keys())[0]
                    
                    measurements = units_dict[unit_key]
                    
                    # Ensure we are only pulling from primary forms
                    valid_measurements = [m for m in measurements if m.get("form") in ["10-K", "10-Q", "20-F", "40-F"]]
                    if not valid_measurements:
                        valid_measurements = measurements
                        
                    latest = sorted(valid_measurements, key=lambda x: x.get("end", ""), reverse=True)[0]
                    return float(latest.get("val"))
                except:
                    continue
        return None

    extracted = {
        "revenue_usd": _get_latest_fact(["Revenues", "SalesRevenueNet", "RevenueFromContractWithCustomerExcludingAssessedTax", "Revenue", "RevenuesFromSaleOfGoods"]),
        "capex_usd": _get_latest_fact(["PaymentsToAcquirePropertyPlantAndEquipment", "PurchaseOfPropertyPlantAndEquipment", "PurchaseOfPropertyPlantAndEquipmentClassifiedAsInvestingActivities"]),
        "debt_usd": _get_latest_fact(["LongTermDebt", "DebtInstrumentCarryingAmount", "LongTermDebtAndCapitalLeaseObligations", "Borrowings", "NoncurrentBorrowings"]),
        "ebitda_usd": _get_latest_fact(["OperatingIncomeLoss", "ProfitLossFromOperatingActivities"]) 
    }
    return extracted

# =====================================================================
# BACKGROUND SERVICES (DASHBOARD & GEOLOCATION)
# =====================================================================
def run_progress_dashboard():
    print("[financial_parser.py] [DASHBOARD] Progress Tracker Online.", flush=True)
    r = redis.Redis(host='corpus_callosum', port=6379, db=0)
    while True:
        time.sleep(60) 
        conn = None
        try:
            conn = get_db_connection()
            cur = conn.cursor()
            cur.execute("SELECT count(*) FROM assets WHERE type IN ('mine', 'refinery', 'smelter')")
            total_assets = cur.fetchone()[0]
            cur.execute("SELECT count(*) FROM assets WHERE metadata->>'mapped' = 'true'")
            mapped_assets = cur.fetchone()[0]
            cur.execute("SELECT count(DISTINCT asset_id) FROM quarterly_financials")
            parsed_assets = cur.fetchone()[0]
            cur.execute("SELECT count(*) FROM edgar_universe")
            universe_size = cur.fetchone()[0]
            queue_len = r.scard("filing_processing_queue")
            coverage_pct = (parsed_assets / total_assets * 100) if total_assets > 0 else 0
            
            print("\n" + "="*80, flush=True)
            print("[financial_parser.py] [FORENSIC PAGER PIPELINE (GPT-4o)]", flush=True)
            print(f" -> EDGAR Universe:     {universe_size} verified producers", flush=True)
            print(f" -> Live SEC Queue:     {queue_len} pending filings", flush=True)
            print(f" -> Deep Extraction:    {parsed_assets} / {total_assets} ({coverage_pct:.1f}%)", flush=True)
            print("="*80 + "\n", flush=True)
        except Exception: pass
        finally:
            if conn: conn.close()

def iterative_search(asset_name, asset_type):
    PERMUTATIONS = {
        "mine": ["{name} mine", "{name} operations", "{name} mining complex", "{name} open pit"],
        "refinery": ["{name} refinery", "{name} chemical plant", "{name} industrial complex"],
        "smelter": ["{name} smelter", "{name} smelting complex", "{name} metal works"]
    }
    candidates = [asset_name]
    if asset_type in PERMUTATIONS:
        candidates.extend([p.format(name=asset_name) for p in PERMUTATIONS[asset_type]])
    for query in candidates:
        try:
            loc = geolocator.geocode(query, timeout=10)
            if loc: return loc.latitude, loc.longitude
        except (GeocoderTimedOut, GeocoderServiceError):
            time.sleep(2) 
            continue
        time.sleep(1.5) 
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
            cur.execute("UPDATE assets SET geom = ST_SetSRID(ST_MakePoint(%s, %s), 4326), latitude = %s, longitude = %s, source = source || '_GEOLOCATED' WHERE id = %s", (lon, lat, lat, lon, aid))
    conn.commit()
    conn.close()

def run_ghost_resolver_loop():
    while True:
        try:
            resolve_spatial_ghosts()
            time.sleep(600)
        except Exception: time.sleep(60)

# =====================================================================
# UNIVERSE & OWNERSHIP RESOLUTION
# =====================================================================
class EdgarUniverseGenerator:
    def build_universe(self):
        conn = get_db_connection()
        cur = conn.cursor()
        try:
            r = requests.get("https://www.sec.gov/files/company_tickers.json", headers=HTTP_HEADERS_SEC, timeout=10)
            if r.status_code != 200: return
            data = r.json()
            verified_batch = []
            for item in list(data.values()):
                cik = str(item['cik_str']).zfill(10)
                ticker = item['ticker']
                name = item['title']
                try:
                    meta_r = requests.get(f"https://data.sec.gov/submissions/CIK{cik}.json", headers=HTTP_HEADERS_SEC, timeout=5)
                    if meta_r.status_code == 200:
                        sic = str(meta_r.json().get('sic', ''))
                        if sic.startswith(TARGET_SIC_PREFIXES):
                            verified_batch.append((cik, ticker, name, sic))
                except: pass
                time.sleep(0.15) 
                if len(verified_batch) >= 50:
                    execute_batch(cur, "INSERT INTO edgar_universe (cik, ticker, company_name, sic_code) VALUES (%s, %s, %s, %s) ON CONFLICT (cik) DO UPDATE SET last_audited = NOW();", verified_batch)
                    conn.commit()
                    verified_batch = []
            if verified_batch:
                execute_batch(cur, "INSERT INTO edgar_universe (cik, ticker, company_name, sic_code) VALUES (%s, %s, %s, %s) ON CONFLICT (cik) DO UPDATE SET last_audited = NOW();", verified_batch)
                conn.commit()
        except Exception: conn.rollback()
        finally: conn.close()

class OwnershipResolver:
    def __init__(self):
        self.r = redis.Redis(host='corpus_callosum', port=6379, db=0)
    
    def get_universe_map(self):
        conn = get_db_connection()
        cur = conn.cursor()
        cur.execute("SELECT lower(company_name), ticker FROM edgar_universe WHERE is_active = TRUE")
        univ_map = {row[0]: row[1] for row in cur.fetchall()}
        conn.close()
        return univ_map

    def run_resolution_loop(self):
        GLOBAL_OPERATORS = {
            "bhp": "BHP", "rio tinto": "RIO", "vale": "VALE", "glencore": "GLNCY", "freeport": "FCX", 
            "southern copper": "SCCO", "teck": "TECK", "anglo american": "NGLOY", "first quantum": "FQVLF",
            "kghm": "KGQYF", "ivanhoe": "IVPAF", "lundin": "LUNMF", "barrick": "GOLD", "newmont": "NEM", 
            "agnico": "AEM", "kinross": "KGC", "gold fields": "GFI", "anglogold": "AU", "alcoa": "AA", 
            "albemarle": "ALB", "sqm": "SQM", "pilbara": "PLL", "century aluminum": "CENX", "norsk hydro": "NHYDY"
        }
        while True:
            try:
                univ_map = self.get_universe_map()
                conn = get_db_connection()
                cur = conn.cursor()
                cur.execute("SELECT id, name, metadata FROM assets WHERE (metadata IS NULL OR (metadata->>'mapped') IS NULL) AND is_private = FALSE LIMIT 500")
                orphans = cur.fetchall()
                for aid, name, meta in orphans:
                    name_str = str(name).strip()
                    if name_str.count('.') >= 2 or name_str.count('-') >= 2:
                        cur.execute("UPDATE assets SET metadata = COALESCE(metadata, '{}'::jsonb) || %s::jsonb WHERE id = %s", (json.dumps({"mapped": True}), aid))
                        conn.commit()
                        continue
                    
                    raw_ticker = None
                    search_term = (meta or {}).get('operator') or (meta or {}).get('company') or name_str
                    if search_term:
                        search_lower = search_term.lower()
                        for key, ticker in GLOBAL_OPERATORS.items():
                            if key in search_lower:
                                raw_ticker = ticker
                                break
                        if not raw_ticker:
                            clean_search = re.sub(r'(?i)\b(inc|corp|ltd|llc|plc|company|co)\b|\.', '', search_lower).strip()
                            for sec_name, ticker in univ_map.items():
                                if len(sec_name) > 4 and (clean_search == sec_name or clean_search in sec_name):
                                    raw_ticker = ticker
                                    break
                    if raw_ticker:
                        meta_payload = json.dumps({"company": search_term, "asset_id": aid, "owner_ticker": raw_ticker})
                        cur.execute("INSERT INTO ticker_registry (symbol, instrument_type, exchange, multiplier, active, metadata, last_updated) VALUES (%s, 'stock', 'SMART', 1.0, TRUE, %s::jsonb, NOW()) ON CONFLICT DO NOTHING", (raw_ticker, meta_payload))
                        cur.execute("INSERT INTO ticker_sensitivity (ticker_symbol, entity_id, weight) VALUES (%s, %s, 1.0) ON CONFLICT DO NOTHING", (raw_ticker, f"ASSET_{aid}"))
                        cur.execute("UPDATE assets SET metadata = COALESCE(metadata, '{}'::jsonb) || %s::jsonb WHERE id = %s", (json.dumps({"mapped": True, "owner_ticker": raw_ticker}), aid))
                        self.r.sadd("filing_processing_queue", raw_ticker)
                    else:
                        priv_meta = json.dumps({"mapped": True, "owner_name": search_term}) if (search_term and search_term != name_str) else json.dumps({"mapped": True})
                        cur.execute("UPDATE assets SET metadata = COALESCE(metadata, '{}'::jsonb) || %s::jsonb, is_private = TRUE WHERE id = %s", (priv_meta, aid))
                conn.commit()
                conn.close()
                if orphans: time.sleep(5) 
                else: time.sleep(3600) 
            except Exception: time.sleep(60)

class TextNormalizer:
    @staticmethod
    def clean_unicode(text):
        text = str(text).lower().strip()
        text = unicodedata.normalize('NFKD', text).encode('ASCII', 'ignore').decode('utf-8')
        return re.sub(r'[^a-z0-9\s]', ' ', text)

    @staticmethod
    def strip_generics(raw_name, ticker=""):
        clean = TextNormalizer.clean_unicode(raw_name)
        generics = r'\b(mine|refinery|smelter|complex|project|plant|operations|salar de|salar|mina de|mina|open pit|underground|cava da|cava|pedreira|antiga|do|da|de|ferro|ouro|cobre|s a|sa|ltda|llc|inc|corp|pty|ltd|minerao|mineradora|quarry|sand|gravel)\b'
        clean = re.sub(generics, ' ', clean)
        
        corp_list = [ticker.lower(), 'vale', 'fcx', 'freeport', 'kinross', 'bhp', 'rio tinto', 'glencore', 'anglo', 'newmont', 'barrick']
        for corp in corp_list:
            if corp: clean = re.sub(rf'\b{corp}\b', ' ', clean)
        words = [w for w in clean.split() if len(w) > 2]
        return " ".join(words).strip()


# =====================================================================
# RIGOROUS PYDANTIC SCHEMAS FOR STRUCTURED OUTPUTS
# =====================================================================
class FutureProject(BaseModel):
    project_name: str = Field(description="Name of the future development project or expansion.")
    expected_capex_millions_usd: Optional[float] = Field(description="Expected capital expenditures in millions of USD.")
    expected_completion_year: Optional[int] = Field(description="The year the project is expected to be completed or commissioned.")

class TaxAndFiscal(BaseModel):
    fiscal_updates: Optional[str] = Field(description="Summary of any changes to local royalties, corporate tax rates, or fiscal regimes mentioned in the document.")

class SegmentExtraction(BaseModel):
    segment_name: str = Field(description="Name of the reporting segment, region, or division.")
    production_raw_value: Optional[float] = Field(description="Raw numerical production volume for this entire segment.")
    production_unit: Optional[str] = Field(description="Unit of production (e.g., 'millions of tons', 'koz').")
    revenue_millions_usd: Optional[float] = Field(description="Segment revenue in millions USD.")
    ebitda_millions_usd: Optional[float] = Field(description="Segment EBITDA in millions USD.")

class AssetExtraction(BaseModel):
    asset_name: str = Field(description="The exact name of the individual mine, refinery, or smelter.")
    sub_sites: Optional[List[str]] = Field(default=None, description="If the asset has sub-sites (pits, underground), list them here.")
    ownership_percentage: Optional[float] = Field(description="The company's ownership percentage of this asset (e.g., 38.5 for a Joint Venture). Use null if fully owned or unstated.")
    
    throughput_printed_number: Optional[float] = Field(description="Amount of ore milled/processed/throughput. DO NOT add zeros.")
    throughput_scale: Optional[str] = Field(description="'None', 'Thousands', 'Millions', 'Billions'")
    throughput_base_unit: Optional[str] = Field(description="'tonnes', 'tons'")

    production_printed_number: Optional[float] = Field(description="The exact printed production number. Do not add zeros.")
    production_scale: Optional[str] = Field(description="Must be one of: 'None', 'Thousands', 'Millions', 'Billions'.")
    production_base_unit: Optional[str] = Field(description="Must be one of: 'pounds', 'ounces', 'tonnes', 'tons', 'LCE', 'grams'.")
    
    cash_cost_per_unit_usd: Optional[float] = Field(description="AISC or cash cost per unit in raw USD (e.g. 1.50). DO NOT scale.")
    cogs_printed_number: Optional[float] = Field(description="Cost of Goods Sold (COGS) exact printed number.")
    cogs_scale: Optional[str] = Field(description="Must be one of: 'None', 'Thousands', 'Millions', 'Billions'.")
    capex_printed_number: Optional[float] = Field(description="Capital expenditures exact printed number.")
    capex_scale: Optional[str] = Field(description="Must be one of: 'None', 'Thousands', 'Millions', 'Billions'.")
    
    head_grade_raw: Optional[float] = Field(description="Numeric head grade (e.g. 0.45 or 2.5).")
    head_grade_unit: Optional[str] = Field(description="Unit for head grade (e.g. 'g/t', '%', 'oz/t').")
    recovery_rate_pct: Optional[float] = Field(description="Recovery rate percentage (usually 50.0 to 95.0).")

class SECForensicReport(BaseModel):
    reasoning_chain: str = Field(description="STEP 1: Entity Resolution. STEP 2: Filter out customers/segments. STEP 3: Ensure JV mines are listed separately.")
    assets: List[AssetExtraction] = Field(description="List of physical mining assets and their mass-balance metrics.")
    segments: Optional[List[SegmentExtraction]] = Field(default=None, description="List of reporting segments or regional divisions.") 
    future_pipeline: List[FutureProject] = Field(description="List of future capital projects and expansions.")
    tax_and_fiscal: Optional[TaxAndFiscal] = Field(description="Information regarding changes to tax laws, royalties, or fiscal regimes.")

# =====================================================================
# THE FORENSIC LLM ENGINE
# =====================================================================
class ForensicLLMExtractor:
    def __init__(self):
        self.system_prompt = """You are an elite Wall Street quantitative forensic auditor.
        
CHAIN OF THOUGHT (MANDATORY):
1. Entity Resolution: Identify all named entities.
2. JV Routing: List physical mines inside JVs, apply ownership percentages.
3. Semantic Guardrails: Filter out non-physical assets (customers, generic segments, corporate entities).

INPUT FORMAT:
You will receive massive raw text chunks extracted from a complete SEC EDGAR filing. Scan the text for the most recent periods available (e.g., Q3 2025 vs Q3 2024). Do not miss data hidden in narrative paragraphs.

EXTRACTION RULES:
1. NO MATH: Extract exact printed numbers. Do not add zeros.
2. SCALES & UNITS: Separate the number from its scale (Millions/Thousands) and base unit.
3. ASSET NAMING (STRICT): Treat distinct mines as separate assets. ONLY extract physical MINES, REFINERIES, or SMELTERS. DO NOT extract corporate entities, companies, joint ventures, gas plants, hydrogen plants, or generic 'Facilities'/'Projects' (e.g. 'NEOM Green Hydrogen Project', 'Jazan Company', 'Pace Facility'). If it is not a physical mining or metals asset, SKIP IT.
"""

    def extract_full_document_chunks(self, cik, ticker):
        from edgar import set_identity, Company
        from bs4 import BeautifulSoup
        import requests
        import time
        
        set_identity("RoccoCapital admin@roccocapital.com")
        
        html_content = ""
        doc_url = ""
        cik_str = str(cik).zfill(10)
        sub_url = f"https://data.sec.gov/submissions/CIK{cik_str}.json"
        
        is_foreign = False
        target_forms = ["10-K", "10-Q"]
        
        # 1. PROFILE ENTITY (Identify FPI vs Domestic)
        print(f"   -> Profiling entity via SEC Submissions API...", flush=True)
        for attempt in range(3):
            try:
                r = requests.get(sub_url, headers=HTTP_HEADERS_SEC, timeout=20)
                if r.status_code == 200:
                    data = r.json()
                    state_inc = data.get("stateOfIncorporation", "")
                    
                    if len(state_inc) == 2 and not state_inc.isalpha():
                        is_foreign = True
                    elif len(state_inc) != 2:
                        is_foreign = True
                        
                    if is_foreign:
                        target_forms = ["40-F", "20-F"] # Strictly restricted forms
                    break
            except Exception:
                time.sleep(2)

        # =================================================================
        # ATTEMPT 1: EDGARTOOLS (Primary Tool as Instructed)
        # =================================================================
        if not is_foreign:
            print(f"   -> [ATTEMPT 1] Using edgartools to fetch FULL document for {ticker}...", flush=True)
            try:
                company = Company(ticker)
                filings = company.get_filings(form=target_forms)
                if filings:
                    filing = filings.latest(1)
                    if filing:
                        try:
                            doc_url = filing.document.url if hasattr(filing, 'document') else ""
                            html_content = filing.html()
                            if html_content:
                                print("      [+] edgartools successfully downloaded full HTML.", flush=True)
                        except Exception as e:
                            print(f"      [-] edgartools extraction failed ({e}).", flush=True)
                else:
                    print(f"      [-] edgartools found no {target_forms} filings for {ticker}.", flush=True)
            except Exception as e:
                print(f"      [-] edgartools connection failed ({e}).", flush=True)
        else:
            print(f"   -> [ATTEMPT 1] Entity is FPI. Bypassing edgartools to prevent SGML crash.", flush=True)

        # =================================================================
        # ATTEMPT 2: BARE-METAL SEC API (Fallback if edgartools failed or skipped)
        # =================================================================
        if not html_content:
            print(f"   -> [ATTEMPT 2] Cascading to Bare-Metal SEC API for FULL document...", flush=True)
            for attempt in range(3):
                try:
                    r = requests.get(sub_url, headers=HTTP_HEADERS_SEC, timeout=20)
                    if r.status_code == 200:
                        recent = r.json().get("filings", {}).get("recent", {})
                        forms = recent.get("form", [])
                        acc_nums = recent.get("accessionNumber", [])
                        docs = recent.get("primaryDocument", [])
                        
                        for i, f in enumerate(forms):
                            if f in target_forms:
                                acc = acc_nums[i].replace("-", "")
                                doc = docs[i]
                                doc_url = f"https://www.sec.gov/Archives/edgar/data/{int(cik)}/{acc}/{doc}"
                                break
                        break
                except Exception:
                    time.sleep(2)
                    
            if not doc_url:
                print(f"      [-] Could not resolve document URL via fallback. No recent {target_forms} found.", flush=True)
                return [], ""

            print(f"   -> Downloading FULL SEC HTML via Requests: {doc_url}", flush=True)
            for attempt in range(4):
                try:
                    r = requests.get(doc_url, headers=HTTP_HEADERS_SEC, timeout=120)
                    if r.status_code == 200:
                        html_content = r.text
                        break
                    elif r.status_code == 429:
                        print("      [SEC RATE LIMIT] Throttled. Sleeping 10s...", flush=True)
                        time.sleep(10)
                except Exception as e:
                    print(f"      [DL RETRY {attempt+1}/4] Timeout/Error: {e}", flush=True)
                    time.sleep(5)
                    
        if not html_content:
            print("      [SEC DL ERR] All fetching methods failed.", flush=True)
            return [], doc_url

        # =================================================================
        # STRIP HTML AND CHUNK FULL TEXT (Zero Context Dropped)
        # =================================================================
        print("   -> Stripping HTML to extract full raw text...", flush=True)
        try:
            soup = BeautifulSoup(html_content, 'lxml')
            for element in soup(["script", "style", "meta", "noscript"]): 
                element.extract()
            full_text = soup.get_text(separator='\n', strip=True)
            
            # Chunk into 50,000 character blocks (~12,000 tokens)
            CHUNK_SIZE = 50000
            chunks = [full_text[i:i+CHUNK_SIZE] for i in range(0, len(full_text), CHUNK_SIZE)]
            
            print(f"      [+] Extracted {len(full_text)} characters. Split into {len(chunks)} chunks.", flush=True)
            return chunks, doc_url

        except Exception as e:
            print(f"      [PARSE ERR] Failed to extract text: {e}", flush=True)
            return [], doc_url
        
    def _query_openai(self, text_chunk, valid_anchors):
        try:
            anchor_str = ", ".join(valid_anchors)
            prompt = self.system_prompt + f"\nKNOWN SPECIFIC ASSET TARGETS TO HUNT FOR: [{anchor_str}]."
            retries = 0
            while retries < 3:
                try:
                    completion = client.beta.chat.completions.parse(
                        model="gpt-4o-mini",
                        messages=[
                            {"role": "system", "content": prompt},
                            {"role": "user", "content": f"--- SEC DOCUMENT BLOCK ---\n{text_chunk}"}
                        ],
                        response_format=SECForensicReport,
                        temperature=0.0
                    )
                    return completion.choices[0].message.parsed
                except Exception as e:
                    error_msg = str(e).lower()
                    if "429" in error_msg or "rate limit" in error_msg or "too large" in error_msg:
                        print(f"      [!] OpenAI Limit Hit: {e}. Pausing 60s...", flush=True)
                        time.sleep(60)
                        retries += 1
                    else:
                        print(f"      [-] OpenAI API Error: {e}", flush=True)
                        break
            return None
        except Exception as e:
            return None

    def match_to_db(self, db_raw_name, extracted_doc_assets_dict, ticker, claimed_keys):
        db_clean = TextNormalizer.strip_generics(db_raw_name, ticker)
        db_set = set(db_clean.split())
        
        best_match = None
        best_combined_score = 0.0
        best_raw_key = None

        for raw_doc_key, data in extracted_doc_assets_dict.items():
            if raw_doc_key in claimed_keys: continue
                
            doc_clean = TextNormalizer.strip_generics(raw_doc_key, ticker)
            doc_set = set(doc_clean.split())
            if not db_set or not doc_set: continue
            
            # --- IMPROVED JACCARD & CONTAINMENT ---
            intersection = len(db_set.intersection(doc_set))
            union = len(db_set.union(doc_set))
            jaccard_score = intersection / float(union) if union > 0 else 0
            
            # Catch sub-names (e.g., 'Pinjarra' vs 'Pinjarra Alumina Refinery')
            containment_score = 0.0
            if db_clean in doc_clean or doc_clean in db_clean:
                containment_score = 0.90 

            base_score = max(jaccard_score, containment_score)

            # --- INCREASED THRESHOLD: Must be 0.50 or Containment ---
            if base_score >= 0.50: 
                data_weight = sum(1 for k, v in data.items() if v is not None and k in ['production_tonnes', 'cash_cost_per_unit_usd'])
                combined_score = base_score + (data_weight * 0.15)
                
                if combined_score > best_combined_score:
                    best_combined_score = combined_score
                    best_match = data
                    best_raw_key = raw_doc_key
                    
        return best_match, best_raw_key

# =====================================================================
# CONTINUOUS SEC PIPELINE AUDITOR 
# =====================================================================
class ContinuousAuditor:
    def __init__(self):
        self.r = redis.Redis(host='corpus_callosum', port=6379, db=0)
        self.llm_engine = ForensicLLMExtractor()
        self.cik_map = {}
        try:
            r = requests.get("https://www.sec.gov/files/company_tickers.json", headers=HTTP_HEADERS_SEC, timeout=10)
            if r.status_code == 200:
                for val in r.json().values():
                    self.cik_map[val['ticker']] = str(val['cik_str']).zfill(10)
        except: pass

    def listen_and_audit(self):
        if OPENAI_API_KEY == "" or OPENAI_API_KEY.startswith("sk-proj-YOUR_"):
            print("[financial_parser.py] ERROR: Please insert your OpenAI API Key.", flush=True)
            return

        def calc_usd(val, scale):
            if val is None: return None
            s = str(scale).lower() if scale else ""
            if "billion" in s: return float(val) * 1000000000.0
            if "million" in s: return float(val) * 1000000.0
            if "thousand" in s: return float(val) * 1000.0
            return float(val)

        def calc_tonnes(val, scale, base_unit):
            if val is None or base_unit is None: return None
            s = str(scale).lower() if scale else ""
            mult = 1.0
            if "billion" in s: mult = 1000000000.0
            elif "million" in s: mult = 1000000.0
            elif "thousand" in s: mult = 1000.0
            
            raw_qty = float(val) * mult
            u = str(base_unit).lower()
            
            if "pound" in u or "lb" in u: return raw_qty * 0.000453592
            if "ounc" in u or "oz" in u: return raw_qty * 0.0283495
            if "ton" in u or "lce" in u or "mt" in u: return raw_qty
            if "gram" in u: return raw_qty * 0.000001
            return raw_qty

        print("[financial_parser.py] [AUDITOR] Forensic Analyst Online. Listening to Queue...", flush=True)

        while True:
            try:
                # SPOP (Set Pop) guarantees uniqueness and drains the queue safely
                raw_data = self.r.spop("filing_processing_queue")
                if not raw_data:
                    time.sleep(5)
                    continue
               
                ticker = raw_data.decode('utf-8')

                # HARD STOP: Discard Non-Target / Aggregates / Chemical Companies completely
                if ticker in ['VMC', 'MLM', 'SUM', 'EXP', 'CX', 'WOR', 'KNF', 'MDU', 'CRH', 'NUE', 'APD', 'LIN', 'ECL', 'SHW', 'PPG']: 
                    print(f"\n[financial_parser.py] [HARD STOP] Ignoring non-metals/aggregates/chemicals ticker: {ticker}.", flush=True)
                    continue
                
                q_len = self.r.scard("filing_processing_queue")
                print(f"\n=======================================================================", flush=True)
                print(f"[financial_parser.py] [QUEUE] Target: {ticker} ({q_len} left in queue)", flush=True)

                conn = get_db_connection()
                cur = conn.cursor()
                
                cur.execute("SELECT cik FROM edgar_universe WHERE ticker = %s AND is_active = TRUE", (ticker,))
                universe_check = cur.fetchone()
                cik = universe_check[0] if universe_check else self.cik_map.get(ticker)
                
                if not cik:
                    print(f"      [-] No CIK mapping found for {ticker}. Skipping entity.", flush=True)
                    conn.close()
                    continue

                # =====================================================================
                # PHASE 1: DETERMINISTIC CORPORATE FINANCIALS (SEC XBRL API)
                # =====================================================================
                print(f"   -> Fetching GAAP Financials from SEC iXBRL Data...", flush=True)
                corp_stats = fetch_corporate_financials_xbrl(cik)
                
                if corp_stats:
                    try:
                        cur.execute("""
                            INSERT INTO corporate_financials (ticker, period, revenue_usd, ebitda_usd, capex_usd, debt_usd, source_doc)
                            VALUES (%s, '2025-CURRENT', %s, %s, %s, %s, 'SEC_API')
                            ON CONFLICT (ticker, period) DO UPDATE SET
                                revenue_usd = COALESCE(EXCLUDED.revenue_usd, corporate_financials.revenue_usd),
                                ebitda_usd = COALESCE(EXCLUDED.ebitda_usd, corporate_financials.ebitda_usd),
                                capex_usd = COALESCE(EXCLUDED.capex_usd, corporate_financials.capex_usd),
                                debt_usd = COALESCE(EXCLUDED.debt_usd, corporate_financials.debt_usd),
                                last_updated = NOW();
                        """, (ticker, corp_stats.get('revenue_usd'), corp_stats.get('ebitda_usd'), corp_stats.get('capex_usd'), corp_stats.get('debt_usd')))
                        conn.commit()
                        print(f"      [+] Corporate Baseline Synced to DB (Rev: {corp_stats.get('revenue_usd')})", flush=True)
                    except Exception as e:
                        print(f"      [-] Corporate Baseline DB Save FAILED: {e}", flush=True)
                        conn.rollback()
                else:
                    print(f"      [-] Could not retrieve GAAP/IFRS financials from SEC XBRL.", flush=True)

                # =====================================================================
                # PHASE 2: PHYSICAL ASSET EXTRACTION
                # =====================================================================
                cur.execute("""
                    SELECT id, name, metadata 
                    FROM assets 
                    WHERE (metadata->>'owner_ticker' = %s) 
                    AND is_private = FALSE
                    AND type IN ('mine', 'refinery', 'smelter')
                    AND COALESCE(commodity_types::text, '') NOT ILIKE '%%oil%%'
                    AND COALESCE(commodity_types::text, '') NOT ILIKE '%%gas%%'
                    AND COALESCE(commodity_types::text, '') NOT ILIKE '%%coal%%'
                    AND COALESCE(commodity_types::text, '') NOT ILIKE '%%aggregate%%'
                    AND COALESCE(commodity_types::text, '') NOT ILIKE '%%sand%%'
                    AND COALESCE(commodity_types::text, '') NOT ILIKE '%%gravel%%'
                    AND COALESCE(commodity_types::text, '') NOT ILIKE '%%stone%%'
                    AND COALESCE(commodity_types::text, '') NOT ILIKE '%%limestone%%'
                    AND COALESCE(commodity_types::text, '') NOT ILIKE '%%cement%%'
                    AND COALESCE(commodity_types::text, '') NOT ILIKE '%%hydrogen%%'
                    AND COALESCE(commodity_types::text, '') NOT ILIKE '%%chemical%%'
                """, (ticker,))
                assets = cur.fetchall()

                if not assets:
                    print(f"      [-] No eligible physical assets mapped to {ticker} in database (or filtered out). Skipping HTML parse.", flush=True)
                    conn.close()
                    continue
                
                stripped_names = [TextNormalizer.strip_generics(a[1], ticker) for a in assets]
                valid_anchors = [n for n in stripped_names if len(n) > 2]
                if not valid_anchors:
                    valid_anchors = [a[1].lower() for a in assets]
                
                print(f"      [DEBUG] Hunting for asset anchors: {valid_anchors}", flush=True)

                document_chunks, primary_doc_url = self.llm_engine.extract_full_document_chunks(cik, ticker)
                
                if not document_chunks:
                    print("      [-] No document chunks returned. Proceeding to next ticker in queue.", flush=True)
                    conn.close()
                    continue

                extracted_data_for_ticker = {}

                for i, chunk in enumerate(document_chunks):
                    print(f"         -> Sending Document Chunk {i+1}/{len(document_chunks)} to GPT-4o...", flush=True)
                    
                    parsed_report = self.llm_engine._query_openai(chunk, valid_anchors)
                    
                    if parsed_report:
                        if parsed_report.assets:
                            names = [a.asset_name for a in parsed_report.assets]
                            print(f"         [+] Extracted Assets from chunk: {names}", flush=True)
                            
                            for asset in parsed_report.assets:
                                raw_name = asset.asset_name
                                if not raw_name: continue
                                
                                prod_tonnes = calc_tonnes(asset.production_printed_number, asset.production_scale, asset.production_base_unit)
                                throughput_tonnes = calc_tonnes(asset.throughput_printed_number, asset.throughput_scale, asset.throughput_base_unit)
                                
                                # TIER 1 FALLBACK: ALGORITHMIC ESTIMATION
                                if prod_tonnes is None and throughput_tonnes and asset.head_grade_raw and asset.recovery_rate_pct and asset.head_grade_unit:
                                    u = asset.head_grade_unit.lower()
                                    if "%" in u:
                                        prod_tonnes = throughput_tonnes * (asset.head_grade_raw / 100.0) * (asset.recovery_rate_pct / 100.0)
                                    elif "g/t" in u or "gram" in u:
                                        prod_tonnes = throughput_tonnes * (asset.head_grade_raw / 1000000.0) * (asset.recovery_rate_pct / 100.0)
                                    elif "oz/t" in u or "ounce" in u:
                                        prod_tonnes = throughput_tonnes * (asset.head_grade_raw * 31.1034768 / 1000000.0) * (asset.recovery_rate_pct / 100.0)
                                    
                                    if prod_tonnes:
                                        print(f"         [!] TIER 1 ESTIMATION: Calculated implied production for {raw_name}: {prod_tonnes:.2f} tonnes.", flush=True)

                                cogs_usd = calc_usd(asset.cogs_printed_number, asset.cogs_scale)
                                capex_usd = calc_usd(asset.capex_printed_number, asset.capex_scale)

                                if raw_name not in extracted_data_for_ticker:
                                    extracted_data_for_ticker[raw_name] = {
                                        "production_tonnes": prod_tonnes,
                                        "throughput_tonnes": throughput_tonnes,
                                        "cash_cost_per_unit_usd": asset.cash_cost_per_unit_usd,
                                        "cogs_usd": cogs_usd,
                                        "capex_usd": capex_usd,
                                        "head_grade_pct": asset.head_grade_raw,
                                        "head_grade_unit": asset.head_grade_unit,
                                        "recovery_rate_pct": asset.recovery_rate_pct,
                                        "ownership_percentage": asset.ownership_percentage,
                                        "sub_sites": asset.sub_sites,
                                        "raw_doc_name": asset.asset_name
                                    }
                                else:
                                    if prod_tonnes is not None: extracted_data_for_ticker[raw_name]["production_tonnes"] = prod_tonnes
                                    if asset.cash_cost_per_unit_usd is not None: extracted_data_for_ticker[raw_name]["cash_cost_per_unit_usd"] = asset.cash_cost_per_unit_usd
                                    if cogs_usd is not None: extracted_data_for_ticker[raw_name]["cogs_usd"] = cogs_usd
                                    if capex_usd is not None: extracted_data_for_ticker[raw_name]["capex_usd"] = capex_usd
                                    if asset.head_grade_raw is not None: extracted_data_for_ticker[raw_name]["head_grade_pct"] = asset.head_grade_raw
                                    if asset.head_grade_unit is not None: extracted_data_for_ticker[raw_name]["head_grade_unit"] = asset.head_grade_unit
                                    if asset.recovery_rate_pct is not None: extracted_data_for_ticker[raw_name]["recovery_rate_pct"] = asset.recovery_rate_pct
                                    if asset.ownership_percentage is not None: extracted_data_for_ticker[raw_name]["ownership_percentage"] = asset.ownership_percentage
                                    if asset.sub_sites is not None: 
                                        existing_sites = extracted_data_for_ticker[raw_name].get("sub_sites") or []
                                        extracted_data_for_ticker[raw_name]["sub_sites"] = list(set(existing_sites + asset.sub_sites))

                # ... (previous logic inside listen_and_audit) ...

                claimed_keys = set()
                matched_llm_keys = set()
                
                print(f"   -> Reconciling {len(extracted_data_for_ticker)} LLM extractions against {len(assets)} DB anchors...", flush=True)
                
                # FIRST PASS: Update existing assets
                for aid, db_name, _ in assets:
                    stats, matched_key = self.llm_engine.match_to_db(db_name, extracted_data_for_ticker, ticker, claimed_keys)
                    
                    if stats:
                        claimed_keys.add(matched_key)
                        matched_llm_keys.add(matched_key)
                        # ... (existing INSERT INTO quarterly_financials logic) ...
                        print(f"      [+] MATCH & UPDATE: '{db_name}' matched with SEC entry '{matched_key}'", flush=True)
                    else:
                        print(f"      [-] No metrics found for existing DB Asset: '{db_name}'", flush=True)

                # SECOND PASS: DISCOVERY (Insert high-confidence physical assets not in DB)
                # ... [Existing Pass 1: Existing DB Match Logic] ...

                # SECOND PASS: SMART DISCOVERY
                for raw_key, stats in extracted_data_for_ticker.items():
                    if raw_key in matched_llm_keys: continue
                    
                    # --- STRICT SEMANTIC FILTERING ---
                    forbidden = ['agreement', 'contract', 'state', 'lease', 'deposit', 'joint venture', 'jv', 'resource', 'facility', 'company', 's.a.', 'inc.']
                    is_shit = any(x in raw_key.lower() for x in forbidden)
                    
                    # Only accept if it identifies as one of the 3 holy types
                    inferred_type = None
                    low_key = raw_key.lower()
                    if 'refinery' in low_key: inferred_type = 'refinery'
                    elif 'smelter' in low_key: inferred_type = 'smelter'
                    elif 'mine' in low_key or 'quarry' in low_key: inferred_type = 'mine'
                    
                    has_metrics = stats.get('production_tonnes') is not None or stats.get('throughput_tonnes') is not None
                    
                    if not is_shit and inferred_type and has_metrics:
                        # --- COLLISION CHECK: Does this asset already exist under a different name? ---
                        cur.execute("SELECT name FROM assets WHERE name ILIKE %s OR %s ILIKE '%%' || name || '%%'", (f'%{raw_key}%', raw_key))
                        if cur.fetchone():
                            print(f"      [-] Discovery Collision: Asset '{raw_key}' likely exists. Dropping discovery path.", flush=True)
                            continue

                        print(f"      [*] DISCOVERY: Valid {inferred_type} '{raw_key}' found. Ingesting...", flush=True)
                        try:
                            metadata = {"owner_ticker": ticker, "mapped": True, "source": "SEC_DISCOVERY", "original_name": raw_key}
                            cur.execute("""
                                INSERT INTO assets (name, type, commodity_types, source, op_health, metadata)
                                VALUES (%s, %s, ARRAY['{Unknown}'], 'SEC_DISCOVERY', 1.0, %s::jsonb)
                                ON CONFLICT (name) DO NOTHING RETURNING id;
                            """, (raw_key, inferred_type, json.dumps(metadata)))
                            
                            new_aid_row = cur.fetchone()
                            if new_aid_row:
                                new_aid = new_aid_row[0]
                                cur.execute("""
                                    INSERT INTO quarterly_financials (asset_id, quarter, reported_production_tonnes, cash_cost_per_unit_usd, source_doc)
                                    VALUES (%s, '2025-CURRENT', %s, %s, %s)
                                """, (new_aid, stats.get('production_tonnes'), stats.get('cash_cost_per_unit_usd'), primary_doc_url))
                                cur.execute("INSERT INTO ticker_sensitivity (ticker_symbol, entity_id, weight) VALUES (%s, %s, 1.0) ON CONFLICT DO NOTHING", (ticker, f"ASSET_{new_aid}"))
                                conn.commit()
                                print(f"      [+] DISCOVERY SUCCESS: ASSET_{new_aid} ({raw_key})", flush=True)
                        except Exception as e:
                            print(f"      [-] DISCOVERY FAILED: {e}", flush=True)
                            conn.rollback()
                    else:
                        skip_reason = "Forbidden String" if is_shit else ("Not a Mine/Ref/Smelt" if not inferred_type else "No Metrics")
                        print(f"      [-] Dropping Garbage Extraction '{raw_key}': {skip_reason}", flush=True)

                self.r.publish("raw_signals", json.dumps({"entity_type": "financial_filing", "category": "threat", "severity": 1.0, "symbol": ticker, "timestamp": int(time.time() * 1000)}))
                conn.close()

            except Exception as e:
                print(f"[financial_parser.py] [ERR] {e}", flush=True)
                time.sleep(2)

if __name__ == "__main__":
    heal_corrupted_database()
    Thread(target=run_progress_dashboard, daemon=True).start()
    Thread(target=run_ghost_resolver_loop, daemon=True).start()
    universe_gen = EdgarUniverseGenerator()
    Thread(target=universe_gen.build_universe, daemon=True).start()
    pivot_engine = OwnershipResolver()
    Thread(target=pivot_engine.run_resolution_loop, daemon=True).start()
    time.sleep(10)
    auditor = ContinuousAuditor()
    auditor.listen_and_audit()