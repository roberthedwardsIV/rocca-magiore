SELECT icao24, ST_NPoints(trajectory) as point_count, last_seen 
FROM flight_legs 
WHERE status = 'ACTIVE' 
ORDER BY point_count DESC 
LIMIT 10;

SELECT COUNT(*) FROM flight_legs;
