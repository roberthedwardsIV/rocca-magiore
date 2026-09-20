"""Shared Postgres connection settings from environment."""
import os


def db_config(*, asyncpg=False):
    """Return a psycopg2-style dict, or asyncpg-style if asyncpg=True."""
    cfg = {
        "user": os.getenv("POSTGRES_USER") or os.getenv("DB_USER") or "rocco_admin",
        "password": os.getenv("POSTGRES_PASSWORD") or os.getenv("DB_PASS") or "",
        "host": os.getenv("POSTGRES_HOST") or os.getenv("DB_HOST") or "hippocampus",
        "port": os.getenv("POSTGRES_PORT") or os.getenv("DB_PORT") or "5432",
    }
    name = os.getenv("POSTGRES_DB") or os.getenv("DB_NAME") or "rocco_commodities"
    if asyncpg:
        cfg["database"] = name
        cfg["port"] = int(cfg["port"])
    else:
        cfg["dbname"] = name
    return cfg
