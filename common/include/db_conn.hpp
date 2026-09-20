#ifndef ROCCO_DB_CONN_HPP
#define ROCCO_DB_CONN_HPP

#include <cstdlib>
#include <string>

inline std::string env_or(const char* key, const char* fallback) {
    const char* v = std::getenv(key);
    return (v && *v) ? std::string(v) : std::string(fallback);
}

inline std::string env_first(const char* a, const char* b, const char* fallback) {
    const char* v = std::getenv(a);
    if (v && *v) return std::string(v);
    v = std::getenv(b);
    if (v && *v) return std::string(v);
    return std::string(fallback);
}

/** Hippocampus (PostGIS) connection string from DB_* / POSTGRES_* env vars. */
inline std::string hippocampus_conn() {
    return "dbname=" + env_first("DB_NAME", "POSTGRES_DB", "rocco_commodities")
         + " user=" + env_first("DB_USER", "POSTGRES_USER", "rocco_admin")
         + " password=" + env_first("DB_PASS", "POSTGRES_PASSWORD", "")
         + " host=" + env_first("DB_HOST", "POSTGRES_HOST", "hippocampus")
         + " port=" + env_first("DB_PORT", "POSTGRES_PORT", "5432");
}

/** External market TimescaleDB connection (brainstem). */
inline std::string market_tsdb_conn() {
    return "dbname=" + env_or("TSDB_NAME", "market_data")
         + " user=" + env_or("TSDB_USER", "quant_admin")
         + " password=" + env_or("TSDB_PASS", "")
         + " host=" + env_or("TSDB_HOST", "host.docker.internal")
         + " port=" + env_or("TSDB_PORT", "5433");
}

#endif
