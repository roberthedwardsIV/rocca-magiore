ALTER TABLE assets ADD CONSTRAINT unique_mine_name UNIQUE (name);

-- 1. Insert Assets (Skips if name already exists)
INSERT INTO assets (name, commodity_types, latitude, longitude, sensitivity_radius_km) VALUES
('Cortez', '{Gold,Silver}', 40.250000, -116.630000, 15.0),
('Carlin Trend', '{Gold}', 40.900000, -116.300000, 15.0),
('Turquoise Ridge', '{Gold}', 41.200000, -117.200000, 15.0),
('Orapa', '{Diamonds}', -21.283300, 25.366700, 15.0),
('Jwaneng', '{Diamonds}', -24.523300, 24.711100, 15.0),
('Oyu Tolgoi', '{Copper,Gold}', 44.008000, 106.840000, 15.0),
('Antamina', '{Copper,Zinc,Silver,Molybdenum}', -9.545800, -77.051700, 15.0),
('Escondida', '{Copper,Gold,Silver}', -24.271000, -69.072000, 15.0),
('Collahuasi', '{Copper,Molybdenum}', -20.985800, -68.641700, 15.0),
('Cerro Verde', '{Copper,Molybdenum,Silver}', -16.540000, -71.590000, 15.0),
('Kamoa-Kakula', '{Copper}', -10.800000, 25.200000, 15.0),
('Tenke Fungurume', '{Copper,Cobalt}', -10.590000, 26.210000, 15.0),
('Grasberg', '{Copper,Gold}', -4.050000, 137.110000, 15.0),
('Chuquicamata', '{Copper}', -22.288300, -68.900000, 15.0),
('El Teniente', '{Copper,Molybdenum}', -34.085000, -70.450000, 15.0),
('Los Bronces', '{Copper,Molybdenum}', -33.150000, -70.280000, 15.0),
('Pueblo Viejo', '{Gold,Silver,Copper}', 18.937200, -70.170000, 15.0),
('Kibali', '{Gold}', 3.125000, 29.583000, 15.0),
('Olympic Dam', '{Copper,Gold,Uranium}', -30.430000, 136.880000, 15.0),
('Morenci', '{Copper,Molybdenum}', 33.080000, -109.360000, 15.0),
('Bingham Canyon', '{Copper,Gold,Silver,Molybdenum}', 40.522200, -112.151100, 15.0),
('Carajas', '{Iron Ore}', -6.060000, -50.180000, 15.0),
('S11D', '{Iron Ore}', -6.400000, -50.160000, 15.0),
('Mount Whaleback', '{Iron Ore}', -23.350000, 119.720000, 15.0),
('Greenbushes', '{Lithium,Tantalum}', -33.850000, 116.050000, 15.0),
('Salar de Atacama (SQM)', '{Lithium,Potassium}', -23.500000, -68.200000, 15.0),
('Salar de Atacama (ALB)', '{Lithium}', -23.600000, -68.300000, 15.0),
('Norilsk Nickel', '{Nickel,Palladium,Platinum,Copper}', 69.350000, 88.200000, 15.0),
('Mutanda', '{Copper,Cobalt}', -10.810000, 25.820000, 15.0),
('Sentinel', '{Copper}', -12.910000, 24.840000, 15.0),
('Las Bambas', '{Copper,Gold,Silver}', -14.100000, -72.310000, 15.0),
('Solomon Hub', '{Iron Ore}', -22.000000, 117.800000, 15.0),
('Sishen', '{Iron Ore}', -27.700000, 23.000000, 15.0),
('Kiruna', '{Iron Ore}', 67.850000, 20.220000, 15.0),
('Muruntau', '{Gold}', 41.500000, 63.580000, 15.0),
('Loulo-Gounkoto', '{Gold}', 12.600000, -11.400000, 15.0),
('Fekola', '{Gold}', 12.500000, -11.500000, 15.0),
('Canadian Malartic', '{Gold}', 48.130000, -78.130000, 15.0),
('Detour Lake', '{Gold}', 49.950000, -79.700000, 15.0),
('Cadia Valley', '{Gold,Copper}', -33.450000, 148.990000, 15.0),
('Boddington', '{Gold,Copper}', -32.740000, 116.340000, 15.0),
('Lihir', '{Gold}', -3.130000, 152.640000, 15.0),
('Ahafo', '{Gold}', 7.000000, -2.350000, 15.0),
('Tanami', '{Gold}', -19.950000, 129.700000, 15.0),
('Kumtor', '{Gold}', 41.860000, 78.200000, 15.0),
('Geita', '{Gold}', -2.880000, 32.180000, 15.0),
('Tarkwa', '{Gold}', 5.300000, -2.000000, 15.0),
('South Deep', '{Gold}', -26.400000, 27.700000, 15.0),
('Mponeng', '{Gold}', -26.420000, 27.430000, 15.0),
('Udachny', '{Diamonds}', 66.420000, 112.280000, 15.0),
('Mirny', '{Diamonds}', 62.530000, 113.980000, 15.0),
('Karowe', '{Diamonds}', -21.500000, 25.400000, 15.0),
('Letseng', '{Diamonds}', -29.000000, 28.700000, 15.0),
('Venetia', '{Diamonds}', -22.440000, 29.320000, 15.0),
('Cullinan', '{Diamonds}', -25.670000, 28.510000, 15.0),
('Diavik', '{Diamonds}', 64.490000, -110.270000, 15.0),
('Pilgangoora', '{Lithium,Tantalum}', -21.030000, 118.910000, 15.0),
('Wodgina', '{Lithium}', -21.180000, 118.660000, 15.0),
('Mt Marion', '{Lithium}', -31.110000, 121.460000, 15.0),
('Salar del Hombre Muerto', '{Lithium}', -25.400000, -67.100000, 15.0),
('Salar de Olaroz', '{Lithium}', -23.650000, -66.750000, 15.0),
('Thacker Pass', '{Lithium}', 41.700000, -118.060000, 15.0),
('Kathleen Valley', '{Lithium}', -27.420000, 120.470000, 15.0),
('Mt Holland', '{Lithium}', -32.100000, 119.750000, 15.0),
('Grota do Cirilo', '{Lithium}', -16.700000, -42.800000, 15.0),
('Voisey''s Bay', '{Nickel,Copper,Cobalt}', 56.310000, -62.090000, 15.0),
('Raglan', '{Nickel,Copper}', 61.690000, -73.680000, 15.0),
('Murrin Murrin', '{Nickel,Cobalt}', -28.850000, 121.850000, 15.0),
('Eagle Mine', '{Nickel,Copper}', 46.750000, -87.880000, 15.0),
('Kansanshi', '{Copper,Gold}', -12.100000, 26.300000, 15.0),
('Lumwana', '{Copper}', -12.200000, 25.400000, 15.0),
('Aktogay', '{Copper}', 46.750000, 79.500000, 15.0),
('Bozshakol', '{Copper,Gold}', 51.800000, 74.500000, 15.0),
('Quellaveco', '{Copper,Molybdenum}', -17.100000, -70.600000, 15.0),
('Los Pelambres', '{Copper,Molybdenum}', -31.710000, -70.500000, 15.0),
('Antucoya', '{Copper}', -22.850000, -69.500000, 15.0),
('Centinela', '{Copper,Gold,Silver}', -22.950000, -69.150000, 15.0),
('Zaldívar', '{Copper}', -24.220000, -69.100000, 15.0),
('Radomiro Tomic', '{Copper}', -22.200000, -68.900000, 15.0),
('Gabriela Mistral', '{Copper}', -24.250000, -69.050000, 15.0),
('Ministro Hales', '{Copper,Silver}', -22.380000, -68.920000, 15.0),
('Andina', '{Copper,Molybdenum}', -33.150000, -70.280000, 15.0),
('Peñasquito', '{Gold,Silver,Lead,Zinc}', 23.950000, -102.700000, 15.0),
('Cerro Negro', '{Gold,Silver}', -46.800000, -70.200000, 15.0),
('Yanacocha', '{Gold}', -6.980000, -78.500000, 15.0),
('Merian', '{Gold}', 4.750000, -54.500000, 15.0),
('Granny Smith', '{Gold}', -28.800000, 122.400000, 15.0),
('St Ives', '{Gold}', -31.400000, 121.750000, 15.0),
('Paracatu', '{Gold}', -17.200000, -46.850000, 15.0),
('Kisladag', '{Gold}', 38.500000, 29.100000, 15.0),
('Tasiast', '{Gold}', 20.600000, -15.500000, 15.0),
('Hope Bay', '{Gold}', 68.200000, -106.500000, 15.0),
('Fosterville', '{Gold}', -36.750000, 144.500000, 15.0),
('Brucejack', '{Gold,Silver}', 56.500000, -130.200000, 15.0),
('Cobre Panama', '{Copper}', 8.830000, -80.640000, 15.0),
('Langer Heinrich', '{Uranium}', -22.800000, 15.300000, 15.0),
('Husab', '{Uranium}', -22.600000, 15.100000, 15.0),
('Rossing', '{Uranium}', -22.450000, 15.050000, 15.0),
('Kevitsa', '{Nickel,Copper,Gold,PGM}', 67.680000, 26.650000, 15.0),
('Aitik', '{Copper,Gold,Silver}', 67.070000, 20.950000, 15.0)
ON CONFLICT (name) DO NOTHING;

-- 2. Map Ownership (Uses Subqueries to find the IDs based on name)
-- This ensures that even if your current IDs are different, the ownership attaches to the correct mine name.
INSERT INTO asset_ownership (asset_id, ticker, stake_percentage)
SELECT id, 'GOLD', 61.50 FROM assets WHERE name = 'Cortez' UNION ALL
SELECT id, 'NEM', 38.50 FROM assets WHERE name = 'Cortez' UNION ALL
SELECT id, 'GOLD', 61.50 FROM assets WHERE name = 'Carlin Trend' UNION ALL
SELECT id, 'NEM', 38.50 FROM assets WHERE name = 'Carlin Trend' UNION ALL
SELECT id, 'GOLD', 61.50 FROM assets WHERE name = 'Turquoise Ridge' UNION ALL
SELECT id, 'NEM', 38.50 FROM assets WHERE name = 'Turquoise Ridge' UNION ALL
SELECT id, 'AAL', 50.00 FROM assets WHERE name = 'Orapa' UNION ALL
SELECT id, 'BOTSWANA_GOV', 50.00 FROM assets WHERE name = 'Orapa' UNION ALL
SELECT id, 'AAL', 50.00 FROM assets WHERE name = 'Jwaneng' UNION ALL
SELECT id, 'BOTSWANA_GOV', 50.00 FROM assets WHERE name = 'Jwaneng' UNION ALL
SELECT id, 'RIO', 66.00 FROM assets WHERE name = 'Oyu Tolgoi' UNION ALL
SELECT id, 'MONGOLIA_GOV', 34.00 FROM assets WHERE name = 'Oyu Tolgoi' UNION ALL
SELECT id, 'BHP', 33.75 FROM assets WHERE name = 'Antamina' UNION ALL
SELECT id, 'GLEN', 33.75 FROM assets WHERE name = 'Antamina' UNION ALL
SELECT id, 'TECK', 22.50 FROM assets WHERE name = 'Antamina' UNION ALL
SELECT id, 'MITSUBISHI', 10.00 FROM assets WHERE name = 'Antamina' UNION ALL
SELECT id, 'BHP', 57.50 FROM assets WHERE name = 'Escondida' UNION ALL
SELECT id, 'RIO', 30.00 FROM assets WHERE name = 'Escondida' UNION ALL
SELECT id, 'MITSUBISHI', 10.00 FROM assets WHERE name = 'Escondida' UNION ALL
SELECT id, 'JECO', 2.50 FROM assets WHERE name = 'Escondida' UNION ALL
SELECT id, 'AAL', 44.00 FROM assets WHERE name = 'Collahuasi' UNION ALL
SELECT id, 'GLEN', 44.00 FROM assets WHERE name = 'Collahuasi' UNION ALL
SELECT id, 'MITSUI', 12.00 FROM assets WHERE name = 'Collahuasi' UNION ALL
SELECT id, 'FCX', 53.56 FROM assets WHERE name = 'Cerro Verde' UNION ALL
SELECT id, 'SUMITOMO', 21.00 FROM assets WHERE name = 'Cerro Verde' UNION ALL
SELECT id, 'BUENAVENTURA', 19.58 FROM assets WHERE name = 'Cerro Verde' UNION ALL
SELECT id, 'IVN', 39.60 FROM assets WHERE name = 'Kamoa-Kakula' UNION ALL
SELECT id, 'ZIJMF', 39.60 FROM assets WHERE name = 'Kamoa-Kakula' UNION ALL
SELECT id, 'DRC_GOV', 20.00 FROM assets WHERE name = 'Kamoa-Kakula' UNION ALL
SELECT id, 'CMOC', 80.00 FROM assets WHERE name = 'Tenke Fungurume' UNION ALL
SELECT id, 'GECAMINES', 20.00 FROM assets WHERE name = 'Tenke Fungurume' UNION ALL
SELECT id, 'FCX', 48.76 FROM assets WHERE name = 'Grasberg' UNION ALL
SELECT id, 'MIND_ID', 51.24 FROM assets WHERE name = 'Grasberg' UNION ALL
SELECT id, 'CODELCO', 100.00 FROM assets WHERE name = 'Chuquicamata' UNION ALL
SELECT id, 'CODELCO', 100.00 FROM assets WHERE name = 'El Teniente' UNION ALL
SELECT id, 'AAL', 50.10 FROM assets WHERE name = 'Los Bronces' UNION ALL
SELECT id, 'MITSUBISHI', 20.40 FROM assets WHERE name = 'Los Bronces' UNION ALL
SELECT id, 'CODELCO', 29.50 FROM assets WHERE name = 'Los Bronces' UNION ALL
SELECT id, 'GOLD', 60.00 FROM assets WHERE name = 'Pueblo Viejo' UNION ALL
SELECT id, 'NEM', 40.00 FROM assets WHERE name = 'Pueblo Viejo' UNION ALL
SELECT id, 'GOLD', 45.00 FROM assets WHERE name = 'Kibali' UNION ALL
SELECT id, 'ANG', 45.00 FROM assets WHERE name = 'Kibali' UNION ALL
SELECT id, 'SOKIMO', 10.00 FROM assets WHERE name = 'Kibali' UNION ALL
SELECT id, 'BHP', 100.00 FROM assets WHERE name = 'Olympic Dam' UNION ALL
SELECT id, 'FCX', 72.00 FROM assets WHERE name = 'Morenci' UNION ALL
SELECT id, 'SUMITOMO', 28.00 FROM assets WHERE name = 'Morenci' UNION ALL
SELECT id, 'RIO', 100.00 FROM assets WHERE name = 'Bingham Canyon' UNION ALL
SELECT id, 'VALE', 100.00 FROM assets WHERE name = 'Carajas' UNION ALL
SELECT id, 'VALE', 100.00 FROM assets WHERE name = 'S11D' UNION ALL
SELECT id, 'BHP', 100.00 FROM assets WHERE name = 'Mount Whaleback' UNION ALL
SELECT id, 'ALB', 49.00 FROM assets WHERE name = 'Greenbushes' UNION ALL
SELECT id, 'TIANQI', 51.00 FROM assets WHERE name = 'Greenbushes' UNION ALL
SELECT id, 'SQM', 100.00 FROM assets WHERE name = 'Salar de Atacama (SQM)' UNION ALL
SELECT id, 'ALB', 100.00 FROM assets WHERE name = 'Salar de Atacama (ALB)' UNION ALL
SELECT id, 'NILSY', 100.00 FROM assets WHERE name = 'Norilsk Nickel' UNION ALL
SELECT id, 'GLEN', 100.00 FROM assets WHERE name = 'Mutanda' UNION ALL
SELECT id, 'FM', 100.00 FROM assets WHERE name = 'Sentinel' UNION ALL
SELECT id, 'MMG', 62.50 FROM assets WHERE name = 'Las Bambas' UNION ALL
SELECT id, 'FMG', 100.00 FROM assets WHERE name = 'Solomon Hub' UNION ALL
SELECT id, 'AAL', 76.30 FROM assets WHERE name = 'Sishen' UNION ALL
SELECT id, 'LKAB', 100.00 FROM assets WHERE name = 'Kiruna' UNION ALL
SELECT id, 'UZBEK_GOV', 100.00 FROM assets WHERE name = 'Muruntau' UNION ALL
SELECT id, 'GOLD', 80.00 FROM assets WHERE name = 'Loulo-Gounkoto' UNION ALL
SELECT id, 'BTG', 80.00 FROM assets WHERE name = 'Fekola' UNION ALL
SELECT id, 'AEM', 100.00 FROM assets WHERE name = 'Canadian Malartic' UNION ALL
SELECT id, 'AEM', 100.00 FROM assets WHERE name = 'Detour Lake' UNION ALL
SELECT id, 'NEM', 100.00 FROM assets WHERE name = 'Cadia Valley' UNION ALL
SELECT id, 'NEM', 100.00 FROM assets WHERE name = 'Boddington' UNION ALL
SELECT id, 'NEM', 100.00 FROM assets WHERE name = 'Lihir' UNION ALL
SELECT id, 'NEM', 100.00 FROM assets WHERE name = 'Ahafo' UNION ALL
SELECT id, 'NEM', 100.00 FROM assets WHERE name = 'Tanami' UNION ALL
SELECT id, 'KYRGYZ_GOV', 100.00 FROM assets WHERE name = 'Kumtor' UNION ALL
SELECT id, 'ANG', 100.00 FROM assets WHERE name = 'Geita' UNION ALL
SELECT id, 'GFI', 90.00 FROM assets WHERE name = 'Tarkwa' UNION ALL
SELECT id, 'GFI', 100.00 FROM assets WHERE name = 'South Deep' UNION ALL
SELECT id, 'HAR', 100.00 FROM assets WHERE name = 'Mponeng' UNION ALL
SELECT id, 'ALROSA', 100.00 FROM assets WHERE name = 'Udachny' UNION ALL
SELECT id, 'ALROSA', 100.00 FROM assets WHERE name = 'Mirny' UNION ALL
SELECT id, 'LUC', 100.00 FROM assets WHERE name = 'Karowe' UNION ALL
SELECT id, 'GEMD', 70.00 FROM assets WHERE name = 'Letseng' UNION ALL
SELECT id, 'AAL', 100.00 FROM assets WHERE name = 'Venetia' UNION ALL
SELECT id, 'PDL', 74.00 FROM assets WHERE name = 'Cullinan' UNION ALL
SELECT id, 'RIO', 100.00 FROM assets WHERE name = 'Diavik' UNION ALL
SELECT id, 'PLS', 100.00 FROM assets WHERE name = 'Pilgangoora' UNION ALL
SELECT id, 'MIN', 50.00 FROM assets WHERE name = 'Wodgina' UNION ALL
SELECT id, 'ALB', 50.00 FROM assets WHERE name = 'Wodgina' UNION ALL
SELECT id, 'MIN', 50.00 FROM assets WHERE name = 'Mt Marion' UNION ALL
SELECT id, 'GANFENG', 50.00 FROM assets WHERE name = 'Mt Marion' UNION ALL
SELECT id, 'RIO', 100.00 FROM assets WHERE name = 'Salar del Hombre Muerto' UNION ALL
SELECT id, 'RIO', 66.50 FROM assets WHERE name = 'Salar de Olaroz' UNION ALL
SELECT id, 'LAC', 100.00 FROM assets WHERE name = 'Thacker Pass' UNION ALL
SELECT id, 'LTR', 100.00 FROM assets WHERE name = 'Kathleen Valley' UNION ALL
SELECT id, 'WES', 50.00 FROM assets WHERE name = 'Mt Holland' UNION ALL
SELECT id, 'SQM', 50.00 FROM assets WHERE name = 'Mt Holland' UNION ALL
SELECT id, 'SGML', 100.00 FROM assets WHERE name = 'Grota do Cirilo' UNION ALL
SELECT id, 'VALE', 100.00 FROM assets WHERE name = 'Voisey''s Bay' UNION ALL
SELECT id, 'GLEN', 100.00 FROM assets WHERE name = 'Raglan' UNION ALL
SELECT id, 'GLEN', 100.00 FROM assets WHERE name = 'Murrin Murrin' UNION ALL
SELECT id, 'LUN', 100.00 FROM assets WHERE name = 'Eagle Mine' UNION ALL
SELECT id, 'FM', 80.00 FROM assets WHERE name = 'Kansanshi' UNION ALL
SELECT id, 'GOLD', 100.00 FROM assets WHERE name = 'Lumwana' UNION ALL
SELECT id, 'KAZ', 100.00 FROM assets WHERE name = 'Aktogay' UNION ALL
SELECT id, 'KAZ', 100.00 FROM assets WHERE name = 'Bozshakol' UNION ALL
SELECT id, 'AAL', 60.00 FROM assets WHERE name = 'Quellaveco' UNION ALL
SELECT id, 'MITSUBISHI', 40.00 FROM assets WHERE name = 'Quellaveco' UNION ALL
SELECT id, 'ANTO', 60.00 FROM assets WHERE name = 'Los Pelambres' UNION ALL
SELECT id, 'ANTO', 70.00 FROM assets WHERE name = 'Antucoya' UNION ALL
SELECT id, 'ANTO', 70.00 FROM assets WHERE name = 'Centinela' UNION ALL
SELECT id, 'ANTO', 50.00 FROM assets WHERE name = 'Zaldívar' UNION ALL
SELECT id, 'BHP', 50.00 FROM assets WHERE name = 'Zaldívar' UNION ALL
SELECT id, 'CODELCO', 100.00 FROM assets WHERE name = 'Radomiro Tomic' UNION ALL
SELECT id, 'CODELCO', 100.00 FROM assets WHERE name = 'Gabriela Mistral' UNION ALL
SELECT id, 'CODELCO', 100.00 FROM assets WHERE name = 'Ministro Hales' UNION ALL
SELECT id, 'CODELCO', 100.00 FROM assets WHERE name = 'Andina' UNION ALL
SELECT id, 'NEM', 100.00 FROM assets WHERE name = 'Peñasquito' UNION ALL
SELECT id, 'NEM', 100.00 FROM assets WHERE name = 'Cerro Negro' UNION ALL
SELECT id, 'NEM', 100.00 FROM assets WHERE name = 'Yanacocha' UNION ALL
SELECT id, 'NEM', 75.00 FROM assets WHERE name = 'Merian' UNION ALL
SELECT id, 'GFI', 100.00 FROM assets WHERE name = 'Granny Smith' UNION ALL
SELECT id, 'GFI', 100.00 FROM assets WHERE name = 'St Ives' UNION ALL
SELECT id, 'KGC', 100.00 FROM assets WHERE name = 'Paracatu' UNION ALL
SELECT id, 'ELD', 100.00 FROM assets WHERE name = 'Kisladag' UNION ALL
SELECT id, 'KGC', 100.00 FROM assets WHERE name = 'Tasiast' UNION ALL
SELECT id, 'AEM', 100.00 FROM assets WHERE name = 'Hope Bay' UNION ALL
SELECT id, 'AEM', 100.00 FROM assets WHERE name = 'Fosterville' UNION ALL
SELECT id, 'NEM', 100.00 FROM assets WHERE name = 'Brucejack' UNION ALL
SELECT id, 'FM', 90.00 FROM assets WHERE name = 'Cobre Panama' UNION ALL
SELECT id, 'PDN', 75.00 FROM assets WHERE name = 'Langer Heinrich' UNION ALL
SELECT id, 'CGN', 90.00 FROM assets WHERE name = 'Husab' UNION ALL
SELECT id, 'CNNC', 68.60 FROM assets WHERE name = 'Rossing' UNION ALL
SELECT id, 'BOLIDEN', 100.00 FROM assets WHERE name = 'Kevitsa' UNION ALL
SELECT id, 'BOLIDEN', 100.00 FROM assets WHERE name = 'Aitik'
ON CONFLICT DO NOTHING;