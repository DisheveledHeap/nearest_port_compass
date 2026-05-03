#pragma once
#include <stdint.h>
#ifndef WATER_ACCESSES_H
#define WATER_ACCESSES_H

// ─────────────────────────────────────────────────────────────────────────────
// Constants & types
// ─────────────────────────────────────────────────────────────────────────────

#define WA_SCALE        10000
#define WA_MAX_ENTRIES  4000

struct WA_Point {
    int32_t lat;    // degrees * 10000
    int32_t lon;    // degrees * 10000
};

struct Quad {
    struct WA_Point top_left;
    struct WA_Point bottom_right;


    struct WA_Point cur;


    struct Quad* top_left_tree;
    struct Quad* top_right_tree;
    struct Quad* bottom_left_tree;
    struct Quad* bottom_right_tree;
};

void quad_insert(struct Quad* q, struct WA_Point p);

void quad_initialize(struct Quad* q);

struct WA_Point get_nearest(struct Quad* q, int32_t lat, int32_t lon);

extern const uint16_t WATER_ACCESS_COUNT;
extern const struct WA_Point WATER_ACCESS_POINTS[];

#define WA_K_NEAREST 5

struct WA_NearestResult {
    struct WA_Point points[WA_K_NEAREST];
    int64_t distances[WA_K_NEAREST];
    int count;
};

struct WA_NearestResult get_k_nearest(struct Quad* q, int32_t lat, int32_t lon);

#endif