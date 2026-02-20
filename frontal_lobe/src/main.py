if __name__ == "__main__":
    logger.info("Frontal Lobe Ingestion Service Starting...")
    
    # 1. DB Integrity Check (Ensures lat/lon columns are relaxed)
    ensure_schema_integrity()

    # 2. FORCE RUN DISCOVERY IMMEDIATELY
    # We do not wait for the scheduler. We hit the APIs now.
    logger.info("[STARTUP] Triggering High-Intensity Global Ingestion...")
    job() 

    # 3. Increase frequency to every 6 hours during initial build
    schedule.every(6).hours.do(job)

    while True:
        schedule.run_pending()
        time.sleep(1)