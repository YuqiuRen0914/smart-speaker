#pragma once
#include <stdint.h>
#include <stdbool.h>
// Internal runtime bridge; never an application-controlled timeout or SDK API.
typedef struct {
    uint32_t remaining;
    uint64_t deadline_us;
    bool failed;
    bool (*cancelled)(void *);
    void *context;
    uint64_t (*now_us)(void);
} LunarOSAotBudget;
static inline void lunaros_aot_budget_begin(LunarOSAotBudget *budget,
                                           uint32_t checkpoints,
                                           uint64_t duration_us) {
    budget->remaining = checkpoints;
    budget->failed = false;
    budget->deadline_us = budget->now_us() + duration_us;
}
static inline int32_t lunaros_aot_budget_poll(LunarOSAotBudget *budget) {
    if (!budget || budget->failed) return 1;
    if (!budget->remaining || budget->now_us() >= budget->deadline_us
        || (budget->cancelled && budget->cancelled(budget->context))) {
        budget->failed = true;
        return 1;
    }
    --budget->remaining;
    return 0;
}
