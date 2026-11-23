/*
===========================================================================
Copyright (C) 2026 the OpenMoHAA team

This file is part of OpenMoHAA source code.

OpenMoHAA source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.
===========================================================================
*/

// A held lean must step and stop as stock AA 1.11's pmove does: the larger
// of the proportional step and the minimum, clamped to the maximum.

#include "../bg_lean.h"

#include <cmath>
#include <cstdio>

static int failures;

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                     \
        }                                                                   \
    } while (0)

#define CHECK_NEAR(got, want)                                                                      \
    do {                                                                                           \
        double g = (got), w = (want);                                                              \
        if (fabs(g - w) > 1e-4) {                                                                  \
            fprintf(stderr, "FAIL %s:%d: %s = %.6f, want %.6f\n", __FILE__, __LINE__, #got, g, w); \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

// AA's values (player.cpp, cg_predict.c)
static const float kMax   = 40.f;
static const float kAdd   = 10.f;
static const float kSpeed = 4.f;

static void TestRight()
{
    // 50 ms frames from upright: 20, 30, 35, ... halving the rest
    CHECK_NEAR(PM_LeanRight(0, 0.05f, kMax, kAdd, kSpeed), 20);
    CHECK_NEAR(PM_LeanRight(20, 0.05f, kMax, kAdd, kSpeed), 30);

    // near the end the minimum of 4 degrees a second takes over (the fork
    // took the proportional step on both branches)
    CHECK_NEAR(PM_LeanRight(39.9f, 0.01f, kMax, kAdd, kSpeed), 39.94f);

    // and the step is clamped (stock 0x1e45d4)
    CHECK(PM_LeanRight(39.9f, 0.05f, kMax, kAdd, kSpeed) == kMax);
    CHECK(PM_LeanRight(kMax, 0.05f, kMax, kAdd, kSpeed) == kMax);
    CHECK(PM_LeanRight(41.f, 0.05f, kMax, kAdd, kSpeed) == kMax);

    // a held lean reaches the maximum exactly
    float lean = 0;
    int   frames;
    for (frames = 0; frames < 100 && lean != kMax; frames++) {
        lean = PM_LeanRight(lean, 0.05f, kMax, kAdd, kSpeed);
    }
    CHECK(lean == kMax);
    CHECK(frames == 9);
}

static void TestLeft()
{
    CHECK_NEAR(PM_LeanLeft(0, 0.05f, kMax, kAdd, kSpeed), -20);
    CHECK_NEAR(PM_LeanLeft(-20, 0.05f, kMax, kAdd, kSpeed), -30);
    CHECK_NEAR(PM_LeanLeft(-39.9f, 0.01f, kMax, kAdd, kSpeed), -39.94f);

    // clamped (stock 0x1e3fd2)
    CHECK(PM_LeanLeft(-39.9f, 0.05f, kMax, kAdd, kSpeed) == -kMax);
    CHECK(PM_LeanLeft(-kMax, 0.05f, kMax, kAdd, kSpeed) == -kMax);
    CHECK(PM_LeanLeft(-41.f, 0.05f, kMax, kAdd, kSpeed) == -kMax);

    // from a right lean the proportional step is the bigger one
    CHECK_NEAR(PM_LeanLeft(30, 0.05f, kMax, kAdd, kSpeed), -5);

    float lean = 0;
    int   frames;
    for (frames = 0; frames < 100 && lean != -kMax; frames++) {
        lean = PM_LeanLeft(lean, 0.05f, kMax, kAdd, kSpeed);
    }
    CHECK(lean == -kMax);
    CHECK(frames == 9);
}

static void TestNoLean()
{
    // single player sets the maximum to 0
    CHECK(PM_LeanRight(0, 0.05f, 0, kAdd, kSpeed) == 0);
    CHECK(PM_LeanLeft(0, 0.05f, 0, kAdd, kSpeed) == 0);
}

int main()
{
    TestRight();
    TestLeft();
    TestNoLean();

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }

    printf("test_lean: all passed\n");
    return 0;
}
