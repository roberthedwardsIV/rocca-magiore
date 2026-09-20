#!/usr/bin/env python3
"""Demo Redis publisher for the Visual Cortex UI.

Publishes synthetic messages on the pub/sub channels that visual_cortex
bridges to the frontend over WebSocket, so the UI can be exercised without
running the full ingest / thalamus / brainstem stack.

Channels:
  raw_signals, system_logs, global_sky, maritime_ais,
  state_vectors, execution_signals

Usage (from inside the Docker network, e.g. the backend container):
  python demo_redis_feed.py burst|stream|quake|fire|exec|signal|plane|ship

Env: REDIS_HOST (default corpus_callosum), REDIS_PORT (default 6379)
"""

import argparse
import json
import os
import random
import sys
import time

import redis

REDIS_HOST = os.getenv("REDIS_HOST", "corpus_callosum")
REDIS_PORT = int(os.getenv("REDIS_PORT", "6379"))

TICKERS = ["WTI_SPOT", "CL", "BRENT", "NG", "HO", "RB"]
EVENT_TYPES = ["SEISMIC", "WILDFIRE", "MARITIME", "AVIATION"]


def now_ms() -> int:
    return int(time.time() * 1000)


def connect() -> redis.Redis:
    r = redis.Redis(host=REDIS_HOST, port=REDIS_PORT, decode_responses=True)
    r.ping()
    return r


def pub(r: redis.Redis, channel: str, payload: dict) -> None:
    n = r.publish(channel, json.dumps(payload))
    print(f"  -> {channel:<18} subs={n}  {json.dumps(payload)[:90]}")


def market_signal(r: redis.Redis) -> None:
    sym = random.choice(TICKERS)
    pub(r, "raw_signals", {
        "entity_id": sym,
        "entity_type": "MARKET",
        "data": {
            "price": round(random.uniform(60, 95), 2),
            "volume": random.randint(1_000, 50_000),
            "delta": round(random.uniform(-1.5, 1.5), 3),
        },
        "timestamp": now_ms(),
    })


def earthquake(r: redis.Redis) -> None:
    eid = f"EQ_{random.randint(100000, 999999)}"
    lat = round(random.uniform(-55, 60), 4)
    lon = round(random.uniform(-170, 170), 4)
    mag = round(random.uniform(4.0, 7.6), 1)
    pub(r, "raw_signals", {
        "entity_id": eid,
        "entity_type": "earthquake",
        "data": {"lat": lat, "lon": lon, "mag": mag},
        "timestamp": now_ms(),
    })
    pub(r, "system_logs", {
        "container": "thalamus",
        "log": f"ROUTING HIT :: SEISMIC m{mag} @ {lat},{lon} -> WTI_SPOT",
    })


def wildfire(r: redis.Redis) -> None:
    fid = f"FIRE_{random.randint(1000, 9999)}"
    lat = round(random.uniform(-40, 65), 4)
    lon = round(random.uniform(-160, 160), 4)
    frp = round(random.uniform(20, 450), 1)
    pub(r, "raw_signals", {
        "entity_id": fid,
        "entity_type": "wildfire",
        "data": {"lat": lat, "lon": lon, "frp": frp},
        "timestamp": now_ms(),
    })
    pub(r, "system_logs", {
        "container": "thalamus",
        "log": f"ROUTING HIT :: WILDFIRE {frp}MW @ {lat},{lon} -> NG",
    })


def aircraft(r: redis.Redis) -> None:
    pub(r, "global_sky", {
        "icao": f"{random.randint(0, 0xFFFFFF):06X}",
        "callsign": random.choice(["UAL", "DAL", "BAW", "AFR", "UAE"]) + str(random.randint(100, 999)),
        "lat": round(random.uniform(-60, 70), 4),
        "lon": round(random.uniform(-170, 170), 4),
        "alt": random.randint(28000, 41000),
    })


def ship(r: redis.Redis) -> None:
    pub(r, "maritime_ais", {
        "mmsi": str(random.randint(200_000_000, 799_999_999)),
        "name": random.choice(["FRONT ALTAIR", "SEAVIGOUR", "MAERSK NEXOE", "PACIFIC GAS"]),
        "lat": round(random.uniform(-50, 60), 4),
        "lon": round(random.uniform(-170, 170), 4),
        "sog": round(random.uniform(0, 22), 1),
    })


def alpha_and_fill(r: redis.Redis) -> None:
    sym = random.choice(TICKERS)
    beta = round(random.uniform(-0.6, 0.6), 3)
    conf = round(random.uniform(0.7, 0.95), 2)
    side = "LONG" if beta >= 0 else "SHORT"
    px = round(random.uniform(60, 95), 2)
    qty = random.randint(1, 12)
    # ALPHA FOUND on thalamus is routed to the BRAINSTEM_EXECUTION panel by the UI
    pub(r, "system_logs", {
        "container": "thalamus",
        "log": f"ALPHA FOUND :: {side} {sym} beta={beta:+.2f} conf={conf:.2f}",
    })
    pub(r, "system_logs", {
        "container": "brainstem",
        "log": f"EXEC :: {'BUY' if side == 'LONG' else 'SELL'} {qty}x {sym} @ {px} [FILLED]",
    })
    pub(r, "state_vectors", {
        "type": "portfolio_update",
        "symbol": sym,
        "qty": qty,
        "avg_px": px,
        "pnl": round(random.uniform(-5000, 9000), 2),
    })
    # Raw execution packet (UI currently ignores this channel on purpose)
    pub(r, "execution_signals", {
        "sym": sym, "action": "BUY" if side == "LONG" else "SELL",
        "mkt": px, "conf": conf, "ts": now_ms(),
    })


def burst(r: redis.Redis) -> None:
    print("[demo] firing a curated wave of events...")
    for _ in range(3):
        market_signal(r)
        time.sleep(0.3)
    aircraft(r); aircraft(r); ship(r); ship(r)
    time.sleep(0.4)
    earthquake(r)
    time.sleep(0.6)
    wildfire(r)
    time.sleep(0.6)
    alpha_and_fill(r)
    print("[demo] burst complete.")


def stream(r: redis.Redis) -> None:
    print("[demo] streaming randomized feed. Ctrl-C to stop.")
    choices = [market_signal, market_signal, aircraft, aircraft, ship,
               earthquake, wildfire, alpha_and_fill]
    try:
        while True:
            random.choice(choices)(r)
            time.sleep(random.uniform(0.4, 1.4))
    except KeyboardInterrupt:
        print("\n[demo] stopped.")


MODES = {
    "burst": burst,
    "stream": stream,
    "quake": earthquake,
    "fire": wildfire,
    "exec": alpha_and_fill,
    "signal": market_signal,
    "plane": aircraft,
    "ship": ship,
}


def main() -> int:
    parser = argparse.ArgumentParser(description="Visual Cortex Redis demo feed")
    parser.add_argument("mode", nargs="?", default="burst", choices=sorted(MODES))
    args = parser.parse_args()

    try:
        r = connect()
    except Exception as e:
        print(f"[demo] cannot reach redis at {REDIS_HOST}:{REDIS_PORT}: {e}", file=sys.stderr)
        return 1

    print(f"[demo] connected to redis {REDIS_HOST}:{REDIS_PORT}, mode={args.mode}")
    MODES[args.mode](r)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
