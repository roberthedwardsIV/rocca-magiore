-- Run: docker cp /home/twitchy477/ROCCO-MAGGIORE/frontal_lobe/data/BasicMaterialsCompanyList.csv hippocampus:/tmp/BasicMaterialsCompanyList.csv 
--      docker cp /home/twitchy477/ROCCO-MAGGIORE/frontal_lobe/data/EnergyCompanyList.csv hippocampus:/tmp/EnergyCompanyList.csv 
--      docker cp /home/twitchy477/ROCCO-MAGGIORE/frontal_lobe/data/IndustrialCompanyList.csv hippocampus:/tmp/IndustrialCompanyList.csv 
--      docker cp /home/twitchy477/ROCCO-MAGGIORE/frontal_lobe/data/UtilityCompanyList.csv hippocampus:/tmp/UtilityCompanyList.csv 
--      docker exec -i hippocampus psql -U rocco_admin -d rocco_commodities < 003_company_list_injections.sql

CREATE TABLE IF NOT EXISTS global_company_universe (
    ticker VARCHAR(20) NOT NULL,
    company_name VARCHAR(255) NOT NULL,
    home_exchange VARCHAR(50),
    ibkr_exchange VARCHAR(20),
    sector VARCHAR(50),
    industry VARCHAR(50),
    is_active BOOLEAN DEFAULT TRUE,
    
    PRIMARY KEY (ticker, ibkr_exchange)
);

CREATE TEMP TABLE staging_universe (
    ticker VARCHAR(20),
    company_name VARCHAR(255),
    sector VARCHAR(50),
    industry VARCHAR(50),
    extra_col TEXT  -- Catches the ghost column (IBKR includes a comma for some reason after the last column title)
);

COPY staging_universe(ticker, company_name, sector, industry, extra_col) FROM '/tmp/BasicMaterialsCompanyList.csv' DELIMITER ',' CSV HEADER;
COPY staging_universe(ticker, company_name, sector, industry, extra_col) FROM '/tmp/EnergyCompanyList.csv' DELIMITER ',' CSV HEADER;
COPY staging_universe(ticker, company_name, sector, industry, extra_col) FROM '/tmp/IndustrialCompanyList.csv' DELIMITER ',' CSV HEADER;
COPY staging_universe(ticker, company_name, sector, industry, extra_col) FROM '/tmp/UtilityCompanyList.csv' DELIMITER ',' CSV HEADER;

INSERT INTO global_company_universe (ticker, company_name, home_exchange, ibkr_exchange, sector, industry, is_active)
SELECT DISTINCT 
    TRIM(ticker), 
    TRIM(company_name), 
    'NYSE',   
    'SMART',  
    TRIM(sector), 
    TRIM(industry), 
    TRUE
FROM staging_universe
WHERE ticker IS NOT NULL AND ticker != ''
ON CONFLICT (ticker, ibkr_exchange) DO NOTHING;

DROP TABLE staging_universe;