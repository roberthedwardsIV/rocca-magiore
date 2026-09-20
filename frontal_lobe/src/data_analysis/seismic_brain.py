import socket
import os
from utils.db_config import db_config
import numpy as np
import tensorflow as tf
import psycopg2
import time
import sys
import json
import redis
import math

"""
Purpose: The seismic_brain listens to the socket posts made by seismic_ingest.cpp when the STA / LTA ratio spikes.
         Once data is received, we run this through our model, locate the signal and publish any confidently
         detected event triggers/signals to the nexus_core.cpp module through redis.
"""

# ---------------------------------------------------------------------------------------
# Configuration variables
SOCKET_PATH = "/shared/seismic_brain.sock" 
MODEL_PATH = "/app/rocco_hybrid_v2.h5"
SAMPLE_COUNT = 1000 
THRESHOLD = 0.85

# Redis Bus Connection (to enable us to write signals back to nexus_core.cpp)
r_bus = redis.Redis(host='corpus_callosum', port=6379)

# Neural Network Import for Inferences
model = tf.keras.models.load_model(MODEL_PATH)

# Database credentials + connection
DB_CONFIG = db_config()
try:
    db_conn = psycopg2.connect(**DB_CONFIG)
    db_conn.autocommit = True
    #print("[DEBUG] Database persistent connection established.")
except Exception as e:
    print(f"[!] Seismic Monitor Stream DB Connection failed: {e}")
    db_conn = None


# ---------------------------------------------------------------------------------------
"""
Name: get_station_coords
Params: station_id -> string of NET_STA (eg. IU_ANMO)
Outputs:
    - latitude (float) of station
    - longitude (float) of station
Purpose: using the station_id's published to the socket, this function looks up the lat +
         long from our db.
Called by: process_inference
Calls: none
"""
def get_station_coords(station_id):
    if not db_conn: return None, None
    try:
        if "_" in station_id:
            net, sta = station_id.split("_", 1)
        else:
            net, sta = "IU", station_id 

        with db_conn.cursor() as cur:
            cur.execute(
                "SELECT latitude, longitude, sensitivity, units FROM earthquake_stations WHERE network = %s AND station = %s", 
                (net.strip(), sta.strip()))
            res = cur.fetchone()
            return (float(res[0]), float(res[1]), float(res[2]), str(res[3])) if res else (None, None, None, None)
    except Exception as e:
        print(f"[ERROR] DB Lookup Error for {station_id}: {e}")
        return None, None, None, None



"""
Name: process_inference
Params: 
    - station_id -> string of NET_STA (eg. IU_ANMO)
    - data_array -> waveform data streamed by seismic_ingest
Outputs: none
Purpose: Takes the raw waveform data and scales it, determines if sustained_noise is
         present, runs data through model to detect what type of seismic event is 
         causing vibrations, captures all hits above our threshold (0.85), then 
         publishes this to redis as a .json package for nexus_core.cpp.
Called by: start_brain_listener
Calls: get_station_coords
"""
def process_inference(station_id, data_array):
    detrended_data = data_array - np.mean(data_array)
    max_amp = np.max(np.abs(detrended_data))
    scaled_data = data_array / (max_amp + 1e-7) if max_amp > 0 else data_array 

    input_data = scaled_data.reshape(1, SAMPLE_COUNT, 1)
    prediction = model.predict(input_data, verbose=0)[0]
    classes = ['Earthquake', 'Logistics_Air', 'Logistics_Rail', 'Logistics_Truck', 'Logistics_Mine', 'Logistics_Gen', 'Military/Explosion']
    
    detections = []
    for i, score in enumerate(prediction):
        if score >= THRESHOLD:
            detections.append({
                "class": classes[i],
                "conf": round(float(score), 4)
            })

    if detections:
        lat, lon, sensitivity, units = get_station_coords(station_id)
        timestamp = int(round(time.time() * 1000))
        entity_id = f"{station_id}_{timestamp}"
        if lat and lon:
            for detection in detections: 
                if detection["class"] == "Earthquake":
                    #Intensity Calculation (Counts/Sensitivity - m/s -> *100 = cm/s)
                    if not sensitivity or sensitivity == 0:
                        print(f"[ERROR] {station_id} has invalid sensitivity: {sensitivity}")
                        continue
                    if "m/s" not in units.lower():
                        print(f"[DEBUG] Skipping {station_id}: Units are {units}, not m/s")
                        continue
                    pgv = (max_amp / sensitivity) * 100
                    mmi = 3.47 * math.log10(max(pgv, 1e-9)) + 2.35
                    if mmi == 0.0:
                        print("[SEISMIC MONITORS] Intensity is ")
                        continue
                    mmi = max(1.0, min(12.0, round(float(mmi), 1)))
                    if mmi == 1.0 or mmi == 12.0:
                        detection["conf"] = 0.00
                    signal_packet = {
                        "entity_id": entity_id,
                        "entity_type": "earthquake",
                        "timestamp": timestamp,
                        "reliability_noise": 2 - (detection["conf"]),
                        "data": {
                            "lat": lat,
                            "lon": lon,
                            "mmi": mmi
                        }
                    }
                    r_bus.lpush("raw_signals", json.dumps(signal_packet))
                    r_bus.publish("raw_signals", json.dumps(signal_packet))
                    print(f"[Seismic Monitor Stream] Earthquake {entity_id} data sent to Redis.")

                elif detection["class"].startswith("Logistics"): 
                    #supply-line geometries (line_id, line_name, line_type, geom)
                    continue
                    #see if this logistics blip is on one of our supply line assets

                    #if so -> we send a raw_signal to the corresponding class with the correct data fields as needed
                else:
                    #add additional class processing here once ready, right now will just report earthquake data/signals
                    continue
        else:
            print(f"[Seismic Monitor Stream] Dropped {entity_id}: No GPS coordinates.")



"""
Name: start_brain_listener
Params: none
Outputs: none
Purpose: Connects to the socket containing seismic_ingest.cpp posts, listens for new spikes,
         parses the raw data from the socket, then runs process_inference to analyze. 
Called by: main()
Calls: process_inference
"""
def start_brain_listener():
    if os.path.exists(SOCKET_PATH): os.remove(SOCKET_PATH)
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(SOCKET_PATH)
    os.chmod(SOCKET_PATH, 0o777) 
    server.listen(10)
    
    print(f"Brain listening on {SOCKET_PATH}...")

    while True:
        conn, _ = server.accept()
        try:
            header_buf = b""
            while True:
                chunk = conn.recv(1)
                if not chunk: break
                header_buf += chunk
                if b"|" in header_buf: break
            
            if not header_buf: continue
            header_str = header_buf.decode('utf-8', errors='ignore').strip('\x00')
            
            try:
                station_id = header_str.split(":")[1].replace("|", "").strip()
            except IndexError: continue
            
            bytes_to_read = SAMPLE_COUNT * 4
            raw_data = b""
            while len(raw_data) < bytes_to_read:
                packet = conn.recv(bytes_to_read - len(raw_data))
                if not packet: break
                raw_data += packet

            if len(raw_data) == bytes_to_read:
                samples = np.frombuffer(raw_data, dtype=np.float32).copy()
                process_inference(station_id, samples)
                
        except Exception as e:
            print(f"Runtime Error: {e}")
        finally:
            conn.close()

if __name__ == "__main__":
    start_brain_listener()