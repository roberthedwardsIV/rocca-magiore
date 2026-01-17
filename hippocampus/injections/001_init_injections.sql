INSERT INTO earthquake_stations (network, station, location, frequency, latitude, longitude, active)
VALUES 
    ('IU', 'ANMO', '00', 'BH?', 34.9459, -106.4572, TRUE),
    ('II', 'AAK', '00', 'BH?', 42.639, 74.494, TRUE);


INSERT INTO assets (name, commodity_types, latitude, longitude, sensitivity_radius_km)
VALUES 
('Escondida Mine', ARRAY['Copper'], -24.2685, -69.0685, 20.0);
('Chuquicamata Mine', ARRAY['Copper', 'Molybdenum'], -22.2908, -68.9033, 25.0),
('Collahuasi Mine', ARRAY['Copper'], -20.9700, -68.6500, 20.0),
('Los Pelambres', ARRAY['Copper'], -31.7100, -70.4800, 15.0),
('Pilbara Operations', ARRAY['Iron Ore'], -21.7500, 117.7900, 50.0),
('Greenbushes Mine', ARRAY['Lithium'], -33.8500, 116.0600, 10.0),
('Olympic Dam', ARRAY['Copper', 'Uranium', 'Gold'], -30.4300, 136.8800, 20.0),
('Grasberg Mine', ARRAY['Copper', 'Gold'], -4.0500, 137.1100, 15.0),
('Carajas Mine', ARRAY['Iron Ore'], -6.0600, -50.1800, 40.0),
('Morenci Mine', ARRAY['Copper'], 33.0800, -109.3600, 20.0),
('Bingham Canyon', ARRAY['Copper', 'Gold'], 40.5200, -112.1500, 15.0);

INSERT INTO asset_ownership (asset_id, ticker, stake_percentage)
VALUES 
    ((SELECT id FROM assets WHERE name = 'Escondida Mine'), 'BHP', 57.5),
    ((SELECT id FROM assets WHERE name = 'Escondida Mine'), 'RIO', 30.0);
    ((SELECT id FROM assets WHERE name = 'Chuquicamata Mine'), 'CODELCO', 100.00),
    ((SELECT id FROM assets WHERE name = 'Collahuasi Mine'), 'GLEN.L', 44.00),
    ((SELECT id FROM assets WHERE name = 'Collahuasi Mine'), 'AAL.L', 44.00),
    ((SELECT id FROM assets WHERE name = 'Pilbara Operations'), 'RIO', 100.00),
    ((SELECT id FROM assets WHERE name = 'Pilbara Operations'), 'BHP', 100.00),
    ((SELECT id FROM assets WHERE name = 'Grasberg Mine'), 'FCX', 48.76),
    ((SELECT id FROM assets WHERE name = 'Carajas Mine'), 'VALE', 100.00),
    ((SELECT id FROM assets WHERE name = 'Morenci Mine'), 'FCX', 72.00),
    ((SELECT id FROM assets WHERE name = 'Olympic Dam'), 'BHP', 100.00),
    ((SELECT id FROM assets WHERE name = 'Greenbushes Mine'), 'ALB', 49.00);

INSERT INTO military_zones (name, zone_type, latitude, longitude, radius_km)
VALUES 
('Antofagasta Training Range', 'base', -23.6500, -70.4000, 10.0);
('White Sands Missile Range', 'base', 32.3800, -106.4800, 100.0),
('Nevada Test Site', 'base', 37.1300, -116.0500, 50.0),
('Zaporizhzhia Region', 'conflict', 47.8300, 35.1300, 150.0),
('Red Sea Shipping Lane', 'conflict', 15.5000, 41.7500, 200.0);



INSERT INTO aircraft_profiles (icao_hex, tail_number, owner_entity, category, typical_route_start) VALUES
('a00001', 'N123BH', 'BHP', 'corporate', 'Melbourne HQ'),
('a00002', 'N456RT', 'Rio Tinto', 'corporate', 'London/Perth'),
('400a1b', 'G-FCX1', 'Freeport-McMoRan', 'corporate', 'Phoenix HQ'),
('adfc21', 'C-130-LOG', 'Antofagasta_Logistics', 'logistics_heavy', 'Los Pelambres'),
('3e42bb', 'AN-124-H', 'Vale_Logistics', 'logistics_heavy', 'Carajás Mine'),
('7105af', 'STA-SHUTL', 'Codelco_Workers', 'worker_transport', 'Chuquicamata');