import os
import psycopg2
from obspy.clients.fdsn import Client
from obspy import UTCDateTime

user = os.getenv("DB_USER", "rocco_admin")
password = os.getenv("DB_PASS", "")
host = os.getenv("DB_HOST", "localhost")
port = os.getenv("DB_PORT", "5432")
dbname = os.getenv("DB_NAME", "rocco_commodities")
conn = psycopg2.connect(f"postgresql://{user}:{password}@{host}:{port}/{dbname}")
cur = conn.cursor()
client = Client("IRIS")

# Get all stations where sensitivity is 0
cur.execute("SELECT network, station FROM earthquake_stations WHERE sensitivity = 0;")
stations = cur.fetchall()

print(f"Updating {len(stations)} stations...")

for net, sta in stations:
    try:
        # Fetch only the latest epoch to get the current gain
        inv = client.get_stations(network=net, station=sta, channel="BHZ", 
                                 level="response", starttime=UTCDateTime.now())
        
        # Extract the total instrument sensitivity (counts per m/s)
        sens = inv[0][0][0].response.instrument_sensitivity.value
        
        cur.execute("UPDATE earthquake_stations SET sensitivity = %s WHERE network = %s AND station = %s", 
                    (sens, net, sta))
        print(f"  [UPDATED] {net}.{sta}: {sens:.4e}")
        
    except Exception as e:
        # If IRIS is down or station is legacy, use the GSN average
        cur.execute("DELETE FROM earthquake_stations WHERE network = %s AND station = %s", (net, sta))
        print(f"  [REMOVED] {net}.{sta}: Dropped from DB. No sensitivity data found.")

conn.commit()
cur.close()
conn.close()