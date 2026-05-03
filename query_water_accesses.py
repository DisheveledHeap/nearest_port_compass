#!/usr/bin/env python3
"""
query_water_access.py

Queries the Overpass API for water access points and port infrastructure
within a bounding box, then outputs:
  - water_access.h   : C header for direct #include in ESP32 firmware
  - water_access.bin : Compact binary file for SPIFFS/LittleFS on ESP32
  - water_access.csv : Human-readable for inspection/debugging

Access types covered:
  - Boat ramps / slipways      (leisure=slipway)
  - Marinas                    (leisure=marina)
  - Ferry terminals            (amenity=ferry_terminal)
  - Piers / jetties            (man_made=pier, man_made=jetty)
  - Harbours                   (harbour=yes / landuse=harbour)
  - Seamarks / port markers    (seamark:type=harbour|anchorage|berth)
  - Docks                      (waterway=dock)
  - Fishing access             (leisure=fishing)
  - Water sport centres        (leisure=water_sports)
  - Canoe/kayak launches       (canoe=put_in / leisure=slipway + boat=yes)

Usage:
    pip install requests
    python query_water_access.py

Edit the CONFIG section below before running.
"""

import requests
import json
import struct
import csv
import math
import time
import os
from dataclasses import dataclass


# ─────────────────────────────────────────────────────────────────────────────
# CONFIG — Edit these values before running
# ─────────────────────────────────────────────────────────────────────────────

# Bounding box for your operational area (south, west, north, east)
# Example below covers Oregon, USA — replace with your region
BBOX = {
    "south":  41.9,
    "west":  -124.7,
    "north":  46.3,
    "east":  -116.4,
}

# Maximum number of entries to write (ESP32 budget — 4000 is very safe)
MAX_ENTRIES = 4000

# Decimal places to keep in lat/lon (4 = ~11m precision)
COORD_PRECISION = 4

# Scale factor for integer encoding (must match COORD_PRECISION)
SCALE = 10 ** COORD_PRECISION

# Minimum distance in metres between two kept entries (deduplication)
DEDUP_DIST_M = 50.0

# Overpass API endpoint
OVERPASS_URL = "https://overpass-api.de/api/interpreter"
# Fallbacks:
# "https://lz4.overpass-api.de/api/interpreter"
# "https://overpass.kumi.systems/api/interpreter"

# Output directory
OUTPUT_DIR = "."

# ─────────────────────────────────────────────────────────────────────────────


@dataclass
class AccessPoint:
    lat: float
    lon: float
    name: str
    access_type: str   # slipway | marina | ferry | pier | jetty | harbour |
                       # dock | fishing | water_sports | anchorage | berth | other
    fee: bool          # True if OSM tags suggest a fee is charged


def build_overpass_query(bbox: dict) -> str:
    """Build Overpass QL query for water access/port features in bbox."""
    s, w, n, e = bbox["south"], bbox["west"], bbox["north"], bbox["east"]
    bb = f"{s},{w},{n},{e}"
    return f"""
[out:json][timeout:120];
(
  node["leisure"="slipway"]({bb});
  way["leisure"="slipway"]({bb});
  node["leisure"="marina"]({bb});
  way["leisure"="marina"]({bb});
  relation["leisure"="marina"]({bb});
  node["amenity"="ferry_terminal"]({bb});
  way["amenity"="ferry_terminal"]({bb});
  node["man_made"="pier"]({bb});
  way["man_made"="pier"]({bb});
  node["man_made"="jetty"]({bb});
  way["man_made"="jetty"]({bb});
  node["harbour"="yes"]({bb});
  way["harbour"="yes"]({bb});
  relation["harbour"="yes"]({bb});
  way["landuse"="harbour"]({bb});
  relation["landuse"="harbour"]({bb});
  node["waterway"="dock"]({bb});
  way["waterway"="dock"]({bb});
  node["leisure"="fishing"]({bb});
  node["leisure"="water_sports"]({bb});
  way["leisure"="water_sports"]({bb});
  node["seamark:type"~"^(harbour|anchorage|berth|small_craft_facility)$"]({bb});
  node["canoe"="put_in"]({bb});
  node["canoe"="portage"]({bb});
  node["boat"="yes"]["man_made"="jetty"]({bb});
);
out center tags;
""".strip()


def query_overpass(query: str, retries: int = 3) -> dict:
    """POST query to Overpass API with retry/backoff logic."""
    for attempt in range(1, retries + 1):
        try:
            print(f"  Querying Overpass API (attempt {attempt}/{retries})...")
            response = requests.post(
                OVERPASS_URL,
                data={"data": query},
                timeout=150,
                headers={"User-Agent": "ESP32-WaterAccessFinder/1.0"}
            )
            response.raise_for_status()
            return response.json()
        except requests.exceptions.Timeout:
            print(f"  Timeout on attempt {attempt}.")
        except requests.exceptions.HTTPError as e:
            print(f"  HTTP error: {e}")
            if response.status_code == 429:
                wait = 30 * attempt
                print(f"  Rate limited — waiting {wait}s...")
                time.sleep(wait)
        except Exception as e:
            print(f"  Unexpected error: {e}")
        if attempt < retries:
            time.sleep(5 * attempt)
    raise RuntimeError("Overpass API query failed after all retries.")


def classify_access_type(tags: dict) -> str:
    """Map OSM tags to a simplified access type string."""
    leisure   = tags.get("leisure", "")
    amenity   = tags.get("amenity", "")
    man_made  = tags.get("man_made", "")
    waterway  = tags.get("waterway", "")
    harbour   = tags.get("harbour", "")
    landuse   = tags.get("landuse", "")
    seamark   = tags.get("seamark:type", "")
    canoe     = tags.get("canoe", "")

    if leisure == "slipway" or canoe in ("put_in", "portage"): return "slipway"
    if leisure == "marina":                                     return "marina"
    if amenity == "ferry_terminal":                             return "ferry"
    if man_made == "pier":                                      return "pier"
    if man_made == "jetty":                                     return "jetty"
    if harbour == "yes" or landuse == "harbour":                return "harbour"
    if waterway == "dock":                                      return "dock"
    if leisure == "fishing":                                    return "fishing"
    if leisure == "water_sports":                               return "water_sports"
    if seamark == "anchorage":                                  return "anchorage"
    if seamark == "berth":                                      return "berth"
    if seamark:                                                 return "seamark"
    return "other"


def has_fee(tags: dict) -> bool:
    """Return True if OSM tags suggest this access point charges a fee."""
    fee_tag = tags.get("fee", "").lower()
    return fee_tag in ("yes", "true", "1")


def haversine_m(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    """Return distance in metres between two WGS84 points."""
    R = 6_371_000
    phi1, phi2 = math.radians(lat1), math.radians(lat2)
    dphi  = math.radians(lat2 - lat1)
    dlam  = math.radians(lon2 - lon1)
    a = math.sin(dphi/2)**2 + math.cos(phi1)*math.cos(phi2)*math.sin(dlam/2)**2
    return 2 * R * math.asin(math.sqrt(a))


def parse_elements(raw: dict) -> list[AccessPoint]:
    """Extract AccessPoint objects from raw Overpass JSON."""
    results = []
    seen_ids = set()

    for el in raw.get("elements", []):
        el_id = (el.get("type"), el.get("id"))
        if el_id in seen_ids:
            continue
        seen_ids.add(el_id)

        # Resolve centroid
        center = el.get("center")
        if not center:
            if el.get("type") == "node":
                center = {"lat": el["lat"], "lon": el["lon"]}
            else:
                continue

        tags = el.get("tags", {})
        name = tags.get("name", tags.get("name:en", tags.get("seamark:name", "")))[:32]

        ap = AccessPoint(
            lat         = round(center["lat"], COORD_PRECISION),
            lon         = round(center["lon"], COORD_PRECISION),
            name        = name,
            access_type = classify_access_type(tags),
            fee         = has_fee(tags),
        )
        results.append(ap)

    return results


def deduplicate(entries: list[AccessPoint]) -> list[AccessPoint]:
    """Remove access points within DEDUP_DIST_M of an already-kept point."""
    kept = []
    for candidate in entries:
        too_close = any(
            haversine_m(candidate.lat, candidate.lon, e.lat, e.lon) < DEDUP_DIST_M
            for e in kept
        )
        if not too_close:
            kept.append(candidate)
    return kept


def filter_entries(entries: list[AccessPoint]) -> list[AccessPoint]:
    """Deduplicate and cap entries."""
    print(f"  Raw parsed entries : {len(entries):>6}")
    kept = deduplicate(entries)
    print(f"  After deduplication: {len(kept):>6} entries  (radius {DEDUP_DIST_M:.0f}m)")
    if len(kept) > MAX_ENTRIES:
        print(f"  Capping to {MAX_ENTRIES} entries (increase MAX_ENTRIES if needed)")
        kept = kept[:MAX_ENTRIES]
    return kept


# ─────────────────────────────────────────────────────────────────────────────
# Type map — must match esp32_water_access_loader.h
# ─────────────────────────────────────────────────────────────────────────────

TYPE_MAP = {
    "slipway":      0,
    "marina":       1,
    "ferry":        2,
    "pier":         3,
    "jetty":        4,
    "harbour":      5,
    "dock":         6,
    "fishing":      7,
    "water_sports": 8,
    "anchorage":    9,
    "berth":        10,
    "seamark":      11,
    "other":        12,
}


# ─────────────────────────────────────────────────────────────────────────────
# Output writers
# ─────────────────────────────────────────────────────────────────────────────

def write_header(entries: list[AccessPoint], path: str):
    """Write a C header for direct #include in ESP32 firmware."""
    lines = [
        "// Auto-generated by query_water_access.py — do not edit manually",
        "//",
        "// Format: { lat_scaled, lon_scaled, type, flags }",
        "//   lat/lon : int32_t  scaled by 10000 (4 decimal places = ~11m)",
        "#pragma once",
        "#include <stdint.h>",
        "",
        "struct WaterAccessPoint {",
        "    int32_t lat;    // degrees * 10000",
        "    int32_t lon;    // degrees * 10000",
        "};",
        "",
        f"const uint16_t WATER_ACCESS_COUNT = {len(entries)};",
        "",
        "const WaterAccessPoint WATER_ACCESS_POINTS[] = {",
    ]

    for ap in entries:
        lat_i   = int(round(ap.lat * SCALE))
        lon_i   = int(round(ap.lon * SCALE))
        comment = f"  // {ap.name}" if ap.name else ""
        lines.append(f"    {{{lat_i:>10}, {lon_i:>11}}},{comment}")

    lines += ["};", ""]

    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"  Written: {path}  ({len(entries)} entries)")


def write_binary(entries: list[AccessPoint], path: str):
    """
    Write compact binary for LittleFS/SPIFFS storage on ESP32.

    Layout:
        [4 bytes] magic  : 'WA_E'
        [2 bytes] count  : uint16_t
        [N * 8 bytes]   : entries
            [4] int32_t lat
            [4] int32_t lon

    Example: 1000 entries → 4 + 2 + (1000 * 8) = 10,006 bytes (~10 KB)
    """
    with open(path, "wb") as f:
        f.write(b'WA_E')
        f.write(struct.pack("<H", len(entries)))
        for ap in entries:
            lat_i = int(round(ap.lat * SCALE))
            lon_i = int(round(ap.lon * SCALE))
            f.write(struct.pack("<ii", lat_i, lon_i))

    size = os.path.getsize(path)
    print(f"  Written: {path}  ({size:,} bytes)")


def write_csv(entries: list[AccessPoint], path: str):
    """Write human-readable CSV for inspection."""
    with open(path, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["lat", "lon"])
        for ap in entries:
            writer.writerow([ap.lat, ap.lon])
    print(f"  Written: {path}")


def print_summary(entries: list[AccessPoint]):
    """Print a type breakdown of the final dataset."""
    from collections import Counter
    counts = Counter(ap.access_type for ap in entries)
    print("\n  Type breakdown:")
    for t, n in sorted(counts.items(), key=lambda x: -x[1]):
        print(f"    {t:<14} {n:>5}")
    fee_count = sum(1 for ap in entries if ap.fee)
    if fee_count:
        print(f"\n  {fee_count} entries marked as fee-charging.")


# ─────────────────────────────────────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────────────────────────────────────

def main():
    print("=" * 60)
    print("  ESP32 Water Access Point Pre-collector")
    print("=" * 60)
    print(f"\nBounding box  : {BBOX}")
    print(f"Max entries   : {MAX_ENTRIES}")
    print(f"Dedup radius  : {DEDUP_DIST_M}m")
    print(f"Precision     : {COORD_PRECISION} decimal places\n")

    print("[1/4] Fetching data from Overpass API...")
    query = build_overpass_query(BBOX)
    raw   = query_overpass(query)
    print(f"  Raw elements returned: {len(raw.get('elements', []))}")

    print("\n[2/4] Parsing elements...")
    entries = parse_elements(raw)

    print("\n[3/4] Filtering and deduplicating...")
    entries = filter_entries(entries)
    print_summary(entries)

    print("\n[4/4] Writing output files...")
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    write_header(entries, os.path.join(OUTPUT_DIR, "water_accesses.h"))
    write_binary(entries, os.path.join(OUTPUT_DIR, "water_accesses.bin"))
    write_csv   (entries, os.path.join(OUTPUT_DIR, "water_accesses.csv"))

    print(f"\n✓ Done. {len(entries)} water access points saved.")
    print("\nNext steps:")
    print("  • Open water_accesses.csv to verify the data looks correct")
    print("  • Copy water_accesses.h into your ESP32 project and #include it")
    print("  • OR upload water_accesses.bin to LittleFS and use the binary loader")


if __name__ == "__main__":
    main()