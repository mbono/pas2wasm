/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

#include "ir/program.h"

/* Test a frontend routine edge in a bounded adjacency/reachability set. */
static int reaches(const Routine *caller, size_t callee)
{
    return (caller->calls[callee / 64] & (UINT64_C(1) << (callee % 64))) != 0;
}

void frontend_effects(Program *program)
{
    /* Warshall closure reaches the least fixed point, including recursive cycles.
     * At most 128 intermediate vertices and two words per set bound the work. */
    for (const Routine *via = program->routines; via; via = via->next) {
        for (Routine *caller = program->routines; caller; caller = caller->next) {
            if (reaches(caller, via->index)) {
                for (size_t word = 0; word < P2W_CALL_WORDS; ++word) {
                    caller->calls[word] |= via->calls[word];
                }
            }
        }
    }
    /* Every reachable direct effect is included. Previously merged sets remain
     * subsets of the caller's closure, so their union yields the same fixed point.
     * Future capture/reference effects can extend this frontend summary. */
    for (Routine *caller = program->routines; caller; caller = caller->next) {
        for (const Routine *callee = program->routines; callee; callee = callee->next) {
            if (reaches(caller, callee->index)) {
                for (size_t word = 0; word < P2W_WRITE_WORDS; ++word) {
                    caller->writes[word] |= callee->writes[word];
                }
            }
        }
    }
}
