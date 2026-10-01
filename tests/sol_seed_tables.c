/*
 * Solitaire HD — offline generator of src/solitaire/winnable_seeds.c (make seed-tables).
 *
 * Solves the deal of every XP seed (0..32767, game.c sol_deal_board) with the full-information solver
 * (src/solitaire/solver.h) for the four rule cases of winnable_seeds.h, and replays every solution
 * through the session API (sol_press for draws, recycles and turns, sol_begin_drag + sol_drop for
 * moves, with the case's Options) to a win; a solution that does not replay is reported, kept as
 * unknown, and makes the tool exit with status 1. Each deal gets two searches at most: the solver's
 * default weights with budget -n, then (if that ran out) the other weight set with budget -N, which
 * finds some solutions the first ordering misses and proves more deals unsolvable. Deals and cases
 * outside -r / -c keep the status the linked table has. Results do not depend on the thread count.
 *
 *   sol_seed_tables [-o FILE] [-j THREADS] [-n NODES] [-N NODES] [-c CASES] [-r FROM TO] [-q]
 *     -o FILE      write the table (default: only print the statistics)
 *     -j THREADS   worker threads (default: the processors online)
 *     -n NODES     first search budget (default 150000)
 *     -N NODES     second search budget, 0 = none (default 500000)
 *     -c CASES     case digits, 0 draw 1, 1 draw 3, 2 draw 1 Vegas, 3 draw 3 Vegas (default 0123)
 *     -r FROM TO   seed range (default 0 32767)
 *     -q           no progress lines
 *
 * Built natively with -O2, -pthread and -DSOL_SOLVER_TUNING (sol_solver_tune), without the sanitizers.
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE              /* glibc: _SC_NPROCESSORS_ONLN */
#define _DARWIN_C_SOURCE             /* macOS: the same */

#include "solitaire/game.h"
#include "solitaire/session.h"
#include "solitaire/solver.h"
#include "solitaire/winnable_seeds.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char *case_name[SOL_SEEDS_CASES] = { "draw 1", "draw 3", "draw 1 Vegas", "draw 3 Vegas" };
static const int case_draw[SOL_SEEDS_CASES] = { 1, 3, 1, 3 };
static const int case_vegas[SOL_SEEDS_CASES] = { 0, 0, 1, 1 };
/* The second search's weights (FD, DEPTH, HOME, TALON, G): the other default set of solver.c. */
static const int alt_w[SOL_SEEDS_CASES][5] = { { 6, 1, 2, 1, 1 }, { 3, 0, 1, 0, 2 }, { 3, 0, 1, 0, 2 }, { 3, 0, 1, 0, 2 } };

typedef struct Job {
    int scase;
    unsigned seed;
} Job;

typedef struct Out {
    uint8_t  status;              /* SOL_SEED_* */
    uint8_t  phase;               /* search that decided: 1 or 2 (0 = unknown) */
    uint16_t moves;               /* solution length (actions) */
    uint32_t nodes;               /* nodes of the deciding (or last) search */
    double   ms;                  /* both searches */
} Out;

static Job *jobs;
static Out *outs;
static long njobs, next_job, done_jobs;
static uint32_t budget1 = 150000, budget2 = 500000;
static int quiet;
static int replay_failures;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct timespec t_start;

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (t.tv_sec - t_start.tv_sec) * 1000.0 + (t.tv_nsec - t_start.tv_nsec) / 1e6;
}

/* Replay a solution through the session: the case's Options, deal `seed`, then every action as the
 * input that plays it; after each, the board must equal sol_solve_apply's, and the game must be won by
 * the last action (and not before). */
static int replay(int scase, unsigned seed, const SolSolveMove *m, int n, char *why, size_t nwhy)
{
    SolSession s;
    sol_init(&s, NULL, NULL);
    s.opts.draw = case_draw[scase];
    s.opts.scoring = case_vegas[scase] ? SOL_SCORING_VEGAS : SOL_SCORING_STANDARD;
    sol_deal(&s, seed, 0);
    SolBoard shadow = s.board;
    int ok = 1;
    for (int i = 0; i < n && ok; i++) {
        if (s.won || !s.dealt) { snprintf(why, nwhy, "won before action %d", i); ok = 0; break; }
        switch (m[i].kind) {
        case SOL_SM_DRAW:
        case SOL_SM_RECYCLE:
            ok = sol_press(&s, SOL_STOCK, 0, 0) == SOL_PRESS_DONE;
            break;
        case SOL_SM_TURN:
            ok = sol_press(&s, m[i].src, m[i].index, 0) == SOL_PRESS_DONE;
            break;
        case SOL_SM_MOVE:
            ok = sol_begin_drag(&s, m[i].src, m[i].index) && sol_drop(&s, m[i].dst);
            break;
        default:
            ok = 0;
        }
        if (!ok) { snprintf(why, nwhy, "action %d (kind %d) refused by the session", i, m[i].kind); break; }
        if (!sol_solve_apply(&shadow, &m[i], case_draw[scase])) {
            snprintf(why, nwhy, "action %d does not apply", i);
            ok = 0;
        } else if (!sol_board_equal(&shadow, &s.board)) {
            snprintf(why, nwhy, "boards differ after action %d", i);
            ok = 0;
        }
    }
    if (ok && !(s.won && !s.forced_win)) { snprintf(why, nwhy, "not won after %d actions", n); ok = 0; }
    sol_free(&s);
    return ok;
}

static void *worker(void *arg)
{
    (void)arg;
    SolSolver *sv1 = sol_solver_new(budget1), *sv2 = budget2 ? sol_solver_new(budget2) : NULL;
    if (!sv1 || (budget2 && !sv2)) {
        fprintf(stderr, "out of memory\n");
        exit(2);
    }
    for (;;) {
        long j = __atomic_fetch_add(&next_job, 1, __ATOMIC_RELAXED);
        if (j >= njobs) break;
        const Job *jb = &jobs[j];
        Out *o = &outs[j];
        int draw = case_draw[jb->scase];
        int left = sol_solve_recycles_left(case_vegas[jb->scase] ? SOL_SCORING_VEGAS : SOL_SCORING_STANDARD, draw, 0);
        SolBoard b;
        sol_deal_board(&b, jb->seed, NULL);
        SolSolveResult r;
        double t0 = now_ms();
        sol_solve(sv1, &b, draw, left, NULL, &r);
        o->phase = 1;
        if (r.status == SOL_SOLVE_GAVE_UP && sv2) {
            sol_solver_tune(sv2, alt_w[jb->scase], 5);
            sol_solve(sv2, &b, draw, left, NULL, &r);
            o->phase = 2;
        }
        o->ms = now_ms() - t0;
        o->nodes = r.nodes;
        if (r.status == SOL_SOLVE_SOLVED) {
            char why[128];
            o->status = SOL_SEED_WINNABLE;
            o->moves = (uint16_t)r.nmoves;
            if (!replay(jb->scase, jb->seed, r.moves, r.nmoves, why, sizeof why)) {
                pthread_mutex_lock(&lock);
                fprintf(stderr, "REPLAY FAILED: %s seed %u: %s\n", case_name[jb->scase], jb->seed, why);
                replay_failures++;
                pthread_mutex_unlock(&lock);
                o->status = SOL_SEED_UNKNOWN;
            }
        } else if (r.status == SOL_SOLVE_UNSOLVABLE) {
            o->status = SOL_SEED_UNSOLVABLE;
        } else {
            o->status = SOL_SEED_UNKNOWN;
            o->phase = 0;
        }
        long d = __atomic_add_fetch(&done_jobs, 1, __ATOMIC_RELAXED);
        if (!quiet && d % 2000 == 0) {
            pthread_mutex_lock(&lock);
            fprintf(stderr, "  %ld / %ld deals, %.0f s\n", d, njobs, now_ms() / 1000);
            pthread_mutex_unlock(&lock);
        }
    }
    sol_solver_free(sv1);
    sol_solver_free(sv2);
    return NULL;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static int write_table(const char *path, uint8_t table[SOL_SEEDS_CASES][SOL_SEEDS / 4], const char *info)
{
    uint32_t counts[SOL_SEEDS_CASES][3];
    memset(counts, 0, sizeof counts);
    for (int c = 0; c < SOL_SEEDS_CASES; c++)
        for (unsigned s = 0; s < SOL_SEEDS; s++) {
            int st = (table[c][s >> 2] >> ((s & 3) * 2)) & 3;
            counts[c][st < 3 ? st : SOL_SEED_UNKNOWN]++;
        }
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return 0; }
    fprintf(f, "/*\n * Solitaire HD — GENERATED by tests/sol_seed_tables.c (make seed-tables); do not edit.\n");
    fprintf(f, " * %s\n *\n", info);
    fprintf(f, " *   case            winnable  unsolvable  unknown\n");
    for (int c = 0; c < SOL_SEEDS_CASES; c++)
        fprintf(f, " *   %-14s  %8u  %10u  %7u\n", case_name[c], counts[c][SOL_SEED_WINNABLE],
                counts[c][SOL_SEED_UNSOLVABLE], counts[c][SOL_SEED_UNKNOWN]);
    fprintf(f, " */\n#include \"winnable_seeds.h\"\n\n");
    fprintf(f, "const char sol_seed_table_info[] = \"%s\";\n\n", info);
    fprintf(f, "const uint32_t sol_seed_counts[SOL_SEEDS_CASES][3] = {\n");
    for (int c = 0; c < SOL_SEEDS_CASES; c++)
        fprintf(f, "    { %u, %u, %u },%s\n", counts[c][0], counts[c][1], counts[c][2], c == 0 ? "   /* unknown, winnable, unsolvable */" : "");
    fprintf(f, "};\n\nconst uint8_t sol_seed_table[SOL_SEEDS_CASES][SOL_SEEDS / 4] = {\n");
    static const char *enum_name[SOL_SEEDS_CASES] = { "SOL_SEEDS_DRAW1", "SOL_SEEDS_DRAW3", "SOL_SEEDS_DRAW1_VEGAS",
                                                      "SOL_SEEDS_DRAW3_VEGAS" };
    for (int c = 0; c < SOL_SEEDS_CASES; c++) {
        fprintf(f, "    {   /* %s */\n", enum_name[c]);
        for (unsigned i = 0; i < SOL_SEEDS / 4; i++)
            fprintf(f, "%s0x%02X,%s", i % 16 == 0 ? "        " : "", table[c][i], i % 16 == 15 ? "\n" : " ");
        fprintf(f, "    },\n");
    }
    fprintf(f, "};\n");
    if (fclose(f) != 0) { perror(path); return 0; }
    return 1;
}

int main(int argc, char **argv)
{
    const char *out_path = NULL, *cases = "0123";
    unsigned from = 0, to = SOL_SEEDS - 1;
    long nthreads = sysconf(_SC_NPROCESSORS_ONLN);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) out_path = argv[++i];
        else if (!strcmp(argv[i], "-j") && i + 1 < argc) nthreads = atol(argv[++i]);
        else if (!strcmp(argv[i], "-n") && i + 1 < argc) budget1 = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "-N") && i + 1 < argc) budget2 = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "-c") && i + 1 < argc) cases = argv[++i];
        else if (!strcmp(argv[i], "-r") && i + 2 < argc) { from = (unsigned)atoi(argv[++i]); to = (unsigned)atoi(argv[++i]); }
        else if (!strcmp(argv[i], "-q")) quiet = 1;
        else {
            fprintf(stderr, "usage: %s [-o FILE] [-j THREADS] [-n NODES] [-N NODES] [-c CASES] [-r FROM TO] [-q]\n", argv[0]);
            return 2;
        }
    }
    if (nthreads < 1) nthreads = 1;
    if (to >= SOL_SEEDS) to = SOL_SEEDS - 1;
    if (from > to) from = to;
    int want[SOL_SEEDS_CASES] = { 0 };
    for (const char *p = cases; *p; p++)
        if (*p >= '0' && *p < '0' + SOL_SEEDS_CASES) want[*p - '0'] = 1;

    jobs = malloc(sizeof *jobs * SOL_SEEDS_CASES * SOL_SEEDS);
    outs = calloc(SOL_SEEDS_CASES * SOL_SEEDS, sizeof *outs);
    if (!jobs || !outs) return 2;
    for (unsigned s = from; s <= to; s++)            /* seed-major: the slow cases spread out */
        for (int c = 0; c < SOL_SEEDS_CASES; c++)
            if (want[c]) jobs[njobs++] = (Job){ c, s };

    clock_gettime(CLOCK_MONOTONIC, &t_start);
    if (!quiet)
        fprintf(stderr, "solving %ld deals (cases %s, seeds %u..%u) on %ld threads, budgets %u + %u nodes\n",
                njobs, cases, from, to, nthreads, budget1, budget2);
    pthread_t *th = malloc(sizeof *th * (size_t)nthreads);
    for (long i = 0; i < nthreads; i++) pthread_create(&th[i], NULL, worker, NULL);
    for (long i = 0; i < nthreads; i++) pthread_join(th[i], NULL);
    double wall = now_ms() / 1000;

    /* statistics per case */
    uint8_t table[SOL_SEEDS_CASES][SOL_SEEDS / 4];
    memcpy(table, sol_seed_table, sizeof table);
    double *ms = malloc(sizeof *ms * SOL_SEEDS);
    printf("%-13s %8s %8s %10s %8s %8s %9s %9s %9s %9s %10s\n", "case", "deals", "winnable", "unsolvable",
           "unknown", "2nd srch", "moves avg", "moves max", "ms median", "ms p99", "cpu s");
    for (int c = 0; c < SOL_SEEDS_CASES; c++) {
        if (!want[c]) continue;
        long n = 0, cnt[3] = { 0, 0, 0 }, phase2 = 0, msum = 0, mmax = 0;
        double cpu = 0;
        for (long j = 0; j < njobs; j++) {
            if (jobs[j].scase != c) continue;
            const Out *o = &outs[j];
            unsigned s = jobs[j].seed;
            table[c][s >> 2] = (uint8_t)((table[c][s >> 2] & ~(3u << ((s & 3) * 2))) | (unsigned)o->status << ((s & 3) * 2));
            cnt[o->status]++;
            if (o->phase == 2) phase2++;
            if (o->status == SOL_SEED_WINNABLE) {
                msum += o->moves;
                if (o->moves > mmax) mmax = o->moves;
            }
            ms[n++] = o->ms;
            cpu += o->ms / 1000;
        }
        qsort(ms, (size_t)n, sizeof *ms, cmp_double);
        printf("%-13s %8ld %8ld %10ld %8ld %8ld %9.0f %9ld %9.1f %9.0f %10.0f\n", case_name[c], n,
               cnt[SOL_SEED_WINNABLE], cnt[SOL_SEED_UNSOLVABLE], cnt[SOL_SEED_UNKNOWN], phase2,
               cnt[SOL_SEED_WINNABLE] ? (double)msum / cnt[SOL_SEED_WINNABLE] : 0.0, mmax,
               n ? ms[n / 2] : 0.0, n ? ms[(n * 99) / 100] : 0.0, cpu);
        printf("              %.2f%% winnable, %.2f%% unsolvable, %.2f%% unknown\n",
               100.0 * cnt[SOL_SEED_WINNABLE] / (n ? n : 1), 100.0 * cnt[SOL_SEED_UNSOLVABLE] / (n ? n : 1),
               100.0 * cnt[SOL_SEED_UNKNOWN] / (n ? n : 1));
    }
    printf("wall %.0f s on %ld threads; %d replay failures\n", wall, nthreads, replay_failures);

    if (out_path) {
        char info[256];
        snprintf(info, sizeof info, "budgets %u + %u nodes (default, then alternate weights); last run: cases %s, seeds %u..%u",
                 budget1, budget2, cases, from, to);
        if (!write_table(out_path, table, info)) return 1;
        printf("wrote %s\n", out_path);
    }
    free(ms);
    free(th);
    free(jobs);
    free(outs);
    return replay_failures ? 1 : 0;
}
