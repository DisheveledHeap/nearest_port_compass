#include "water_accesses.h"
#include <stdlib.h>
#include <limits.h>

// Helper Functions

static struct Quad* create_node(
    struct WA_Point tl,
    struct WA_Point br
) {
    struct Quad* node = (struct Quad*)malloc(sizeof(struct Quad));

    node->top_left = tl;
    node->bottom_right = br;

    node->cur.lat = INT32_MIN; // mark empty
    node->cur.lon = INT32_MIN;

    node->top_left_tree = NULL;
    node->top_right_tree = NULL;
    node->bottom_left_tree = NULL;
    node->bottom_right_tree = NULL;

    return node;
}

static int is_empty(struct Quad* q) {
    return q->cur.lat == INT32_MIN;
}

static int in_bounds(struct Quad* q, struct WA_Point p) {
    return (p.lat <= q->top_left.lat &&
            p.lat >= q->bottom_right.lat &&
            p.lon >= q->top_left.lon &&
            p.lon <= q->bottom_right.lon);
}

// initialization

void quad_initialize(struct Quad* q) {
    // Entire world bounds (scaled)
    q->top_left.lat =  900000;   // 90.0000
    q->top_left.lon = -1800000;  // -180.0000

    q->bottom_right.lat = -900000;
    q->bottom_right.lon =  1800000;

    q->cur.lat = INT32_MIN;
    q->cur.lon = INT32_MIN;

    q->top_left_tree = NULL;
    q->top_right_tree = NULL;
    q->bottom_left_tree = NULL;
    q->bottom_right_tree = NULL;

    for (uint16_t i = 0; i < WATER_ACCESS_COUNT; i++) {
         quad_insert(q, WATER_ACCESS_POINTS[i]);
      }
      return;
}

// insertion

void quad_insert(struct Quad* q, struct WA_Point p) {
    if (!q) return;
    if (!in_bounds(q, p)) return;

    // If this node has no point yet, store it
    if (is_empty(q)) {
        q->cur = p;
        return;
    }

    // If leaf node with one point, subdivide
    int32_t mid_lat = (q->top_left.lat + q->bottom_right.lat) / 2;
    int32_t mid_lon = (q->top_left.lon + q->bottom_right.lon) / 2;

    // Create children if needed
    if (!q->top_left_tree) {
        q->top_left_tree = create_node(
            (struct WA_Point){q->top_left.lat, q->top_left.lon},
            (struct WA_Point){mid_lat, mid_lon}
        );

        q->top_right_tree = create_node(
            (struct WA_Point){q->top_left.lat, mid_lon},
            (struct WA_Point){mid_lat, q->bottom_right.lon}
        );

        q->bottom_left_tree = create_node(
            (struct WA_Point){mid_lat, q->top_left.lon},
            (struct WA_Point){q->bottom_right.lat, mid_lon}
        );

        q->bottom_right_tree = create_node(
            (struct WA_Point){mid_lat, mid_lon},
            (struct WA_Point){q->bottom_right.lat, q->bottom_right.lon}
        );

        // Reinsert current point
        struct WA_Point old = q->cur;
        q->cur.lat = INT32_MIN;
        q->cur.lon = INT32_MIN;

        quad_insert(q, old);
    }

    // Insert into correct quadrant
    if (p.lat >= mid_lat) {
        if (p.lon <= mid_lon)
            quad_insert(q->top_left_tree, p);
        else
            quad_insert(q->top_right_tree, p);
    } else {
        if (p.lon <= mid_lon)
            quad_insert(q->bottom_left_tree, p);
        else
            quad_insert(q->bottom_right_tree, p);
    }
}

// nearest neighbor

static int64_t dist_sq(int32_t lat1, int32_t lon1,
                       int32_t lat2, int32_t lon2) {
    int64_t dlat = (int64_t)lat1 - lat2;
    int64_t dlon = (int64_t)lon1 - lon2;
    return dlat * dlat + dlon * dlon;
}

static void nearest_rec(struct Quad* q,
                        int32_t lat, int32_t lon,
                        struct WA_Point* best,
                        int64_t* best_dist) {
    if (!q) return;

    if (!is_empty(q)) {
        int64_t d = dist_sq(lat, lon, q->cur.lat, q->cur.lon);
        if (d < *best_dist) {
            *best_dist = d;
            *best = q->cur;
        }
    }

    nearest_rec(q->top_left_tree, lat, lon, best, best_dist);
    nearest_rec(q->top_right_tree, lat, lon, best, best_dist);
    nearest_rec(q->bottom_left_tree, lat, lon, best, best_dist);
    nearest_rec(q->bottom_right_tree, lat, lon, best, best_dist);
}

struct WA_Point get_nearest(struct Quad* q,
                                    int32_t lat,
                                    int32_t lon) {
    struct WA_Point best = {0, 0};
    int64_t best_dist = LLONG_MAX;

    nearest_rec(q, lat, lon, &best, &best_dist);

    return best;
}