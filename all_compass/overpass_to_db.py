import json
import math
import sqlite3
import time
import urllib.request
import urllib.parse
import urllib.error

# ── Configuration ────────────────────────────────────────────────────────────

BOUNDING_BOX     = (41.99, -124.57, 46.24, -116.46)  # (south, west, north, east) — Oregon
MAX_ENTRIES      = 100_000
MAX_CELL_SQ_MILES = 10_000   # split the bbox into cells no larger than this

OVERPASS_URL     = "https://overpass-api.de/api/interpreter"
REQUEST_DELAY    = 3         # seconds to wait between cells (be a good API citizen)
MAX_RETRIES      = 3         # per-cell retry attempts on failure
RETRY_BACKOFF    = 10        # seconds added per retry (10, 20, 30 …)

# OSM tag keys to collect, checked in priority order.
# The first matching key wins for (type, subtype).
TAG_KEYS = ["amenity", "shop", "tourism", "leisure", "historic", "natural"]

OUTPUT_FILE = "pois.db"

# ── Bounding-box splitting ────────────────────────────────────────────────────

def cell_size_degrees(lat_mid: float, target_sq_miles: float) -> float:
    """
    Return the side length (in degrees) of a square cell whose area is
    approximately `target_sq_miles` at the given mid-latitude.

    At latitude φ:
        1° lat  ≈ 69.17 miles
        1° lon  ≈ 69.17 × cos(φ) miles
        cell area (sq mi) = Δlat × Δlon × 69.17 × 69.17 × cos(φ)

    For a square cell (Δlat = Δlon = d):
        d = sqrt(target / (69.17² × cos(φ)))
    """
    miles_per_deg = 69.17
    cos_lat       = math.cos(math.radians(lat_mid))
    return math.sqrt(target_sq_miles / (miles_per_deg ** 2 * cos_lat))


def split_bbox(
    bbox: tuple[float, float, float, float],
    max_cell_sq_miles: float,
) -> list[tuple[float, float, float, float]]:
    """
    Divide `bbox` into a uniform grid of cells each ≤ `max_cell_sq_miles`.
    Returns a flat list of (south, west, north, east) tuples.
    """
    s, w, n, e = bbox
    lat_mid    = (s + n) / 2
    cell_deg   = cell_size_degrees(lat_mid, max_cell_sq_miles)

    lat_span = n - s
    lon_span = e - w
    rows     = math.ceil(lat_span / cell_deg)
    cols     = math.ceil(lon_span / cell_deg)

    # Recompute exact step so cells tile perfectly without overlap or gap
    lat_step = lat_span / rows
    lon_step = lon_span / cols

    cells = []
    for row in range(rows):
        for col in range(cols):
            cell_s = round(s + row * lat_step,       6)
            cell_n = round(s + (row + 1) * lat_step, 6)
            cell_w = round(w + col * lon_step,       6)
            cell_e = round(w + (col + 1) * lon_step, 6)
            cells.append((cell_s, cell_w, cell_n, cell_e))

    approx_area = lat_span * lon_span * (69.17 ** 2) * math.cos(math.radians(lat_mid))
    print(f"Grid          : {rows} rows × {cols} cols = {len(cells)} cells")
    print(f"Approx area   : {approx_area:,.0f} sq miles  →  ≤{max_cell_sq_miles:,} sq mi per cell")
    return cells


# ── Overpass query ────────────────────────────────────────────────────────────

def build_query(bbox: tuple[float, float, float, float]) -> str:
    s, w, n, e = bbox
    bbox_str   = f"{s},{w},{n},{e}"
    unions     = "\n  ".join(f'node["{key}"]({bbox_str});' for key in TAG_KEYS)
    return f"""[out:json][timeout:60];
(
  {unions}
);
out body;"""


def fetch_overpass(query: str, cell_label: str) -> list[dict]:
    """POST a query to Overpass and return the elements list, with retries."""
    data = urllib.parse.urlencode({"data": query}).encode()

    for attempt in range(1, MAX_RETRIES + 1):
        try:
            req = urllib.request.Request(OVERPASS_URL, data=data)
            with urllib.request.urlopen(req, timeout=90) as resp:
                raw      = json.load(resp)
                elements = raw.get("elements", [])
                print(f"  {cell_label}  →  {len(elements):,} raw elements")
                return elements

        except urllib.error.HTTPError as exc:
            wait = RETRY_BACKOFF * attempt
            print(f"  {cell_label}  HTTP {exc.code} on attempt {attempt}/{MAX_RETRIES} — retrying in {wait}s …")
            time.sleep(wait)

        except Exception as exc:
            wait = RETRY_BACKOFF * attempt
            print(f"  {cell_label}  Error on attempt {attempt}/{MAX_RETRIES}: {exc} — retrying in {wait}s …")
            time.sleep(wait)

    print(f"  {cell_label}  FAILED after {MAX_RETRIES} attempts — skipping cell")
    return []


# ── Filtering & normalisation ─────────────────────────────────────────────────

def element_to_poi(el: dict) -> dict | None:
    """Convert a raw Overpass element to a POI dict, or None if it should be skipped."""
    tags = el.get("tags", {})
    name = tags.get("name", "").strip()
    if not name:
        return None

    lat = el.get("lat")
    lon = el.get("lon")
    if lat is None or lon is None:
        return None

    for key in TAG_KEYS:
        if key in tags:
            return {
                "id":      el["id"],
                "lat":     round(lat, 6),
                "lon":     round(lon, 6),
                "name":    name,
                "type":    key,
                "subtype": tags[key],
            }

    return None  # has none of our desired tag keys


# ── Geohash encoder ───────────────────────────────────────────────────────────

# Standard base32 alphabet used by the geohash spec
_BASE32 = "0123456789bcdefghjkmnpqrstuvwxyz"

def geohash(lat: float, lon: float, precision: int = 5) -> str:
    """
    Encode (lat, lon) as a geohash string of `precision` characters.

    Each character adds ~2.5 bits of precision per axis:
        5 chars  ≈ ±2.4 km  (good enough for city-scale neighbour queries)
        6 chars  ≈ ±0.6 km
        7 chars  ≈ ±76  m

    The algorithm interleaves bits from the longitude range (even positions)
    and latitude range (odd positions), then encodes every 5-bit group as a
    base32 character.
    """
    lat_lo, lat_hi = -90.0,  90.0
    lon_lo, lon_hi = -180.0, 180.0

    bits      = 0   # accumulator for the current 5-bit group
    bit_count = 0   # how many bits are loaded into `bits`
    is_lon    = True  # alternate: longitude bit first
    result    = []

    while len(result) < precision:
        if is_lon:
            mid = (lon_lo + lon_hi) / 2
            if lon >= mid:
                bits = (bits << 1) | 1
                lon_lo = mid
            else:
                bits = bits << 1
                lon_hi = mid
        else:
            mid = (lat_lo + lat_hi) / 2
            if lat >= mid:
                bits = (bits << 1) | 1
                lat_lo = mid
            else:
                bits = bits << 1
                lat_hi = mid

        is_lon    = not is_lon
        bit_count += 1

        if bit_count == 5:
            result.append(_BASE32[bits])
            bits      = 0
            bit_count = 0

    return "".join(result)


# ── SQLite output ─────────────────────────────────────────────────────────────

def open_db(path: str) -> sqlite3.Connection:
    con = sqlite3.connect(path)
    con.execute("PRAGMA journal_mode = WAL")   # safe for incremental writes
    con.execute("PRAGMA synchronous  = NORMAL")
    con.execute("""
        CREATE TABLE IF NOT EXISTS pois (
            id      INTEGER PRIMARY KEY,  -- OSM node ID
            lat     REAL    NOT NULL,
            lon     REAL    NOT NULL,
            name    TEXT    NOT NULL,
            type    TEXT    NOT NULL,     -- amenity / shop / tourism / …
            subtype TEXT    NOT NULL,     -- fuel / hospital / cafe / …
            geohash TEXT    NOT NULL      -- 5-char ≈ ±2.4 km cell
        )
    """)
    con.execute("CREATE INDEX IF NOT EXISTS idx_geohash ON pois (geohash)")
    con.execute("CREATE INDEX IF NOT EXISTS idx_type    ON pois (type, subtype)")
    con.execute("CREATE INDEX IF NOT EXISTS idx_name    ON pois (name COLLATE NOCASE)")
    con.commit()
    return con


def insert_batch(con: sqlite3.Connection, pois: list[dict]) -> None:
    con.executemany(
        "INSERT OR IGNORE INTO pois VALUES (?,?,?,?,?,?,?)",
        [
            (
                p["id"], p["lat"], p["lon"],
                p["name"], p["type"], p["subtype"],
                geohash(p["lat"], p["lon"], precision=5),
            )
            for p in pois
        ],
    )
    con.commit()


def finalise_db(con: sqlite3.Connection, path: str) -> None:
    count = con.execute("SELECT COUNT(*) FROM pois").fetchone()[0]
    con.execute("ANALYZE")   # update query-planner statistics
    con.close()
    print(f"  Wrote {count:,} rows → {path}")


# ── Entry point ───────────────────────────────────────────────────────────────

def main(
    bbox:              tuple[float, float, float, float] = BOUNDING_BOX,
    max_entries:       int                               = MAX_ENTRIES,
    max_cell_sq_miles: float                             = MAX_CELL_SQ_MILES,
    output:            str                               = OUTPUT_FILE,
) -> None:
    print(f"Bounding box  : {bbox}")
    print(f"Max entries   : {max_entries:,}")
    print(f"Max cell size : {max_cell_sq_miles:,} sq miles")
    print()

    cells = split_bbox(bbox, max_cell_sq_miles)
    total = len(cells)
    print()

    con = open_db(output)

    # Track OSM IDs in memory to deduplicate across cell boundaries.
    # Each ID is a 64-bit int; 100k entries ≈ 800 KB — fine for the desktop.
    seen_ids:   set[int] = set()
    total_kept: int      = 0

    for i, cell in enumerate(cells, start=1):
        label    = f"[{i:>2}/{total}] bbox={cell}"
        query    = build_query(cell)
        elements = fetch_overpass(query, label)

        batch: list[dict] = []
        for el in elements:
            if el["id"] in seen_ids:
                continue
            poi = element_to_poi(el)
            if poi is None:
                continue
            seen_ids.add(el["id"])
            batch.append(poi)
            if total_kept + len(batch) >= max_entries:
                break

        insert_batch(con, batch)
        total_kept += len(batch)
        print(f"    +{len(batch):,} new  |  running total: {total_kept:,}")

        if total_kept >= max_entries:
            print(f"\n  Hit the {max_entries:,}-entry cap after cell {i}/{total} — stopping early.")
            print(f"  Increase MAX_ENTRIES or reduce MAX_CELL_SQ_MILES to get more granular batches.")
            break

        if i < total:
            time.sleep(REQUEST_DELAY)

    print(f"\nTotal unique POIs : {total_kept:,}")
    finalise_db(con, output)
    print("Done.")


if __name__ == "__main__":
    main()
