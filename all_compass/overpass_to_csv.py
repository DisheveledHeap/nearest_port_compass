import json
import csv
import math
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

OUTPUT_FILE = "pois.csv"

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


# ── CSV output ────────────────────────────────────────────────────────────────

def write_csv(pois: list[dict], path: str) -> None:
    fieldnames = ["id", "lat", "lon", "name", "type", "subtype"]
    with open(path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(pois)
    print(f"  Wrote {len(pois):,} rows → {path}")


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

    # Query each cell and accumulate unique POIs (deduplicated by OSM node ID)
    seen_ids: set[int]   = set()
    all_pois: list[dict] = []

    for i, cell in enumerate(cells, start=1):
        label = f"[{i:>2}/{total}] bbox={cell}"
        query = build_query(cell)
        elements = fetch_overpass(query, label)

        new_this_cell = 0
        for el in elements:
            if el["id"] in seen_ids:
                continue                      # duplicate from an adjacent cell
            poi = element_to_poi(el)
            if poi is None:
                continue
            seen_ids.add(el["id"])
            all_pois.append(poi)
            new_this_cell += 1

        print(f"    +{new_this_cell:,} new  |  running total: {len(all_pois):,}")

        if len(all_pois) >= max_entries:
            print(f"\n  Hit the {max_entries:,}-entry cap after cell {i}/{total} — stopping early.")
            print(f"  Increase MAX_ENTRIES or reduce MAX_CELL_SQ_MILES to get more granular batches.")
            break

        if i < total:
            time.sleep(REQUEST_DELAY)

    # Trim to cap, then write
    if len(all_pois) > max_entries:
        all_pois = all_pois[:max_entries]

    print(f"\nTotal unique POIs : {len(all_pois):,}")
    write_csv(all_pois, output)
    print("Done.")


if __name__ == "__main__":
    main()
