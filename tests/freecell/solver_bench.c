/*
 * FreeCell HD — solver benchmark (native, -O2, no sanitizers): `make solver-bench`.
 *
 *   build/bench/solver_bench [first last] [-n max_nodes] [-std] [-v] [-w w0,w1,...]
 *
 * Solves deals first..last (default 1..32000) under the XP rule (or -std, the standard supermove
 * rule), replays every solution through the session (fcs_click, scripted MoveCol answers) and prints
 * solved / unsolvable / gave-up counts, time and node percentiles, solution lengths, memory. -v prints
 * one line per deal (status, nodes, expanded, moves, ms).
 *
 * It also builds for 32-bit Windows, to check that the XP build finds the very same solutions:
 *   i686-w64-mingw32-gcc -static -std=gnu99 -O2 -march=i686 -mcrtdll=msvcrt-os -Isrc \
 *       -o build/bench/solver_bench.exe tests/freecell/solver_bench.c \
 *       src/freecell/{game,session,solver,stats,wondeals,assist}.c src/engine/store.c
 *   wine build/bench/solver_bench.exe 1 2000 -v     (compare the lines without the times)
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L             /* clock_gettime */
#endif
#include "freecell/solver.h"
#include "solver_replay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef FC_SOLVER_TUNING
void fc_solver_tune(FcSolver *sv, const int *w, int n);
#endif

static double now_ms(void)
{
#ifdef _WIN32                               /* a cross-built copy (32-bit determinism check under Wine) */
    return clock() * 1e3 / CLOCKS_PER_SEC;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
#endif
}

static int cmp_d(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static double pct(double *v, int n, double p)
{
    if (n <= 0) return 0;
    qsort(v, (size_t)n, sizeof *v, cmp_d);
    int i = (int)(p * (n - 1) + 0.5);
    return v[i];
}

int main(int argc, char **argv)
{
    int first = 1, last = 32000, std = 0, verbose = 0, npos = 0;
    uint32_t max_nodes = 0;
    int w[16], nw = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-n") && i + 1 < argc) max_nodes = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "-std")) std = 1;
        else if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (!strcmp(argv[i], "-w") && i + 1 < argc) {
            for (char *p = argv[++i]; *p && nw < 16;) {
                w[nw++] = (int)strtol(p, &p, 10);
                if (*p == ',') p++;
            }
        } else if (npos == 0) { first = last = atoi(argv[i]); npos++; }
        else { last = atoi(argv[i]); npos++; }
    }
    FcSolver *sv = fc_solver_new(max_nodes);
    if (!sv) { fprintf(stderr, "out of memory\n"); return 2; }
#ifdef FC_SOLVER_TUNING
    if (nw) fc_solver_tune(sv, w, nw);
#else
    (void)w; (void)nw;
#endif
    int total = last - first + 1;
    double *t_all = malloc(sizeof(double) * (size_t)total), *t_sol = malloc(sizeof(double) * (size_t)total);
    double *n_all = malloc(sizeof(double) * (size_t)total), *n_sol = malloc(sizeof(double) * (size_t)total);
    double *len = malloc(sizeof(double) * (size_t)total);
    int solved = 0, unsolvable = 0, gaveup = 0, bad = 0, nt = 0, ns = 0;
    double t0 = now_ms(), tmax = 0, tsum = 0, nsum = 0;
    int tmax_deal = 0;
    FcSession s;
    ReplayUI r;
    replay_init(&s, &r);
    for (int d = first; d <= last; d++) {
        FcBoard b;
        fc_deal(&b, d);
        FcSolveResult res;
        double t = now_ms();
        fc_solve(sv, &b, std, NULL, &res);
        t = now_ms() - t;
        t_all[nt] = t;
        n_all[nt++] = res.nodes;
        tsum += t;
        nsum += res.nodes;
        if (t > tmax) { tmax = t; tmax_deal = d; }
        if (res.status == FC_SOLVE_SOLVED) {
            solved++;
            t_sol[ns] = t;
            n_sol[ns] = res.nodes;
            len[ns++] = res.nmoves;
            replay_deal(&s, &r, d);
            int at;
            const char *err = replay_moves(&s, &r, res.moves, res.nmoves, std, 1, &at);
            if (err) { bad++; printf("deal %d: replay failed at move %d: %s\n", d, at, err); }
        } else if (res.status == FC_SOLVE_UNSOLVABLE) {
            unsolvable++;
        } else {
            gaveup++;
        }
        if (verbose || res.status != FC_SOLVE_SOLVED)
            printf("deal %6d: %s nodes %7u expanded %7u moves %3d  %.2f ms\n", d,
                   res.status == FC_SOLVE_SOLVED ? "solved    " : res.status == FC_SOLVE_UNSOLVABLE ? "UNSOLVABLE" : "GAVE UP   ",
                   res.nodes, res.expanded, res.nmoves, t);
    }
    double wall = now_ms() - t0;
    printf("\n%s rule, deals %d..%d, node budget %u (%.1f MiB allocated)\n", std ? "standard" : "XP", first, last,
           max_nodes ? max_nodes : FC_SOLVER_DEFAULT_NODES, fc_solver_memory(sv) / 1048576.0);
    printf("solved %d, unsolvable (proven) %d, gave up %d, replay failures %d\n", solved, unsolvable, gaveup, bad);
    printf("time ms   all: median %.3f  p95 %.3f  p99 %.3f  max %.2f (deal %d)   total %.1f s\n",
           pct(t_all, nt, 0.5), pct(t_all, nt, 0.95), pct(t_all, nt, 0.99), tmax, tmax_deal, wall / 1e3);
    printf("time ms   solved: median %.3f  p95 %.3f  p99 %.3f  max %.2f\n",
           pct(t_sol, ns, 0.5), pct(t_sol, ns, 0.95), pct(t_sol, ns, 0.99), pct(t_sol, ns, 1.0));
    printf("nodes     all: median %.0f  p95 %.0f  p99 %.0f  max %.0f\n",
           pct(n_all, nt, 0.5), pct(n_all, nt, 0.95), pct(n_all, nt, 0.99), pct(n_all, nt, 1.0));
    printf("moves     solved: median %.0f  p95 %.0f  max %.0f\n", pct(len, ns, 0.5), pct(len, ns, 0.95), pct(len, ns, 1.0));
    printf("search    %.0f nodes in %.2f s: %.0f ns per node\n", nsum, tsum / 1e3, nsum > 0 ? tsum * 1e6 / nsum : 0.0);
    printf("peak node memory (max nodes x %u B) %.1f MiB\n", FC_SOLVER_NODE_BYTES,
           pct(n_all, nt, 1.0) * FC_SOLVER_NODE_BYTES / 1048576.0);
    fcs_free(&s);
    fc_solver_free(sv);
    free(t_all); free(t_sol); free(n_all); free(n_sol); free(len);
    return bad != 0;
}
