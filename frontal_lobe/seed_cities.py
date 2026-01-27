import psycopg2
import requests
import zipfile
import io
import os

# --- CONFIG ---
DB_CONFIG = {
    "dbname": "rocco_commodities",
    "user": "rocco_admin",
    "password": "REMOVED",
    "host": "hippocampus",
    "port": "5432"
}

GEONAMES_URL = "http://download.geonames.org/export/dump/cities15000.zip"
CSV_FILENAME = "cities15000.txt"

def get_db_connection():
    return psycopg2.connect(**DB_CONFIG)

def create_schema(cur):
    print("[SEED] Creating Schema 'spatial_ref'...")
    cur.execute("CREATE SCHEMA IF NOT EXISTS spatial_ref;")
    
    # Drop table if exists to ensure clean slate
    cur.execute("DROP TABLE IF EXISTS spatial_ref.world_cities;")
    
    # Create Raw Table (Matches GeoNames structure perfectly)
    cur.execute("""
        CREATE TABLE spatial_ref.world_cities (
            geonameid         int,
            name              text,
            asciiname         text,
            alternatenames    text,
            latitude          float,
            longitude         float,
            feature_class     char(1),
            feature_code      text,
            country_code      text,
            cc2               text,
            admin1_code       text,
            admin2_code       text,
            admin3_code       text,
            admin4_code       text,
            population        bigint,
            elevation         int,
            dem               int,
            timezone          text,
            modification_date date
        );
    """)

def download_and_extract():
    print(f"[SEED] Downloading city data from {GEONAMES_URL}...")
    r = requests.get(GEONAMES_URL)
    r.raise_for_status()
    
    print("[SEED] Extracting...")
    with zipfile.ZipFile(io.BytesIO(r.content)) as z:
        # Extract to local temp storage
        z.extract(CSV_FILENAME, "/tmp")
    
    return f"/tmp/{CSV_FILENAME}"

def populate_table(conn, file_path):
    cur = conn.cursor()
    print("[SEED] Bulk inserting data...")
    
    with open(file_path, 'r', encoding='utf-8') as f:
        # Use copy_expert for maximum speed with specific GeoNames CSV format (tab separated, no header)
        cur.copy_expert(
            "COPY spatial_ref.world_cities FROM STDIN WITH CSV DELIMITER E'\t' QUOTE E'\b' NULL ''", 
            f
        )
    
    print("[SEED] Converting coordinates to PostGIS Geometry...")
    # 1. Add Geometry Column
    cur.execute("ALTER TABLE spatial_ref.world_cities ADD COLUMN coords geometry(Point, 4326);")
    
    # 2. Populate Geometry from Lat/Lon columns
    cur.execute("UPDATE spatial_ref.world_cities SET coords = ST_SetSRID(ST_MakePoint(longitude, latitude), 4326);")
    
    # 3. Create Spatial Index (CRITICAL for the <-> operator in C++)
    print("[SEED] Building Spatial Index (GiST)...")
    cur.execute("CREATE INDEX idx_world_cities_coords ON spatial_ref.world_cities USING GIST (coords);")
    
    # 4. Create standard index on population for fast filtering
    cur.execute("CREATE INDEX idx_world_cities_pop ON spatial_ref.world_cities (population);")
    
    conn.commit()
    print("[SEED] Success! World Cities database is ready.")

if __name__ == "__main__":
    try:
        conn = get_db_connection()
        with conn.cursor() as cur:
            create_schema(cur)
        
        csv_path = download_and_extract()
        populate_table(conn, csv_path)
        
        conn.close()
        # Cleanup
        if os.path.exists(csv_path):
            os.remove(csv_path)
            
    except Exception as e:
        print(f"[ERROR] Seeding failed: {e}")