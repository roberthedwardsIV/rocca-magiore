import psycopg2
import csv
import os
import sys

# --- CONFIG ---
DB_CONFIG = {
    "dbname": "rocco_commodities",
    "user": "rocco_admin",
    "password": "REMOVED",
    "host": "hippocampus",
    "port": "5432"
}

CSV_FILE_PATH = "/app/data/aircraftDatabase.csv"

def get_db_connection():
    return psycopg2.connect(**DB_CONFIG)

def seed_registry():
    if not os.path.exists(CSV_FILE_PATH):
        print(f"[ERROR] CSV file not found at {CSV_FILE_PATH}", flush=True)
        return

    print(f"[REGISTRY] Starting robust load from {CSV_FILE_PATH}...", flush=True)
    
    try:
        conn = get_db_connection()
        cur = conn.cursor()

        # SQL for Upsert
        # We handle duplicates by updating the existing record with new info
        insert_sql = """
            INSERT INTO aircraft_registry (
                icao24, registration, manufacturer, model, typecode, operator, owner
            )
            VALUES (%s, %s, %s, %s, %s, %s, %s)
            ON CONFLICT (icao24) 
            DO UPDATE SET 
                registration = EXCLUDED.registration,
                manufacturer = EXCLUDED.manufacturer,
                model = EXCLUDED.model,
                owner = EXCLUDED.owner,
                operator = EXCLUDED.operator;
        """

        batch = []
        batch_size = 5000
        count = 0

        # Open file with DictReader (Smart Column Selection)
        with open(CSV_FILE_PATH, 'r', encoding='utf-8', errors='replace') as f:
            # Check if the first line contains quotes around headers like 'icao24'
            # If so, we might need to handle them, but DictReader is usually smart.
            
            # Use 'quotechar' to handle the single quotes if your headers are literally 'icao24'
            # Based on your prompt, it looks like standard CSV formatting.
            reader = csv.DictReader(f, delimiter=',', quotechar="'")
            
            # Clean headers (strip whitespace and potential quotes just in case)
            headers = [h.strip().strip("'") for h in reader.fieldnames]
            reader.fieldnames = headers # Re-assign cleaned headers
            
            print(f"[REGISTRY] Detected Columns: {headers[:5]} ...", flush=True)

            if 'icao24' not in headers:
                print("[CRITICAL] 'icao24' column missing. Check CSV format.", flush=True)
                return

            print("[REGISTRY] Processing rows (this takes a moment)...", flush=True)
            
            for row in reader:
                # Extract ONLY what we need
                r_icao = row.get('icao24')
                if not r_icao: continue # Skip empty rows

                # Map CSV columns to DB Schema
                data_tuple = (
                    r_icao,
                    row.get('registration'),
                    row.get('manufacturerName'), # Mapped from your provided header
                    row.get('model'),
                    row.get('typecode'),
                    row.get('operator'),
                    row.get('owner')
                )
                
                batch.append(data_tuple)

                # Execute Batch
                if len(batch) >= batch_size:
                    cur.executemany(insert_sql, batch)
                    conn.commit()
                    count += len(batch)
                    print(f"   Processed {count} aircraft...", end='\r', flush=True)
                    batch = []

            # Insert remaining rows
            if batch:
                cur.executemany(insert_sql, batch)
                conn.commit()
                count += len(batch)

        print(f"\n[REGISTRY] Success! Loaded {count} aircraft records.", flush=True)

    except Exception as e:
        print(f"\n[CRITICAL ERROR] {e}", flush=True)
        import traceback
        traceback.print_exc()
    finally:
        if 'conn' in locals() and conn:
            conn.close()

if __name__ == "__main__":
    seed_registry()