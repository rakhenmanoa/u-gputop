/*
 * adreno-smi — moniteur GPU Adreno en ligne de commande, façon nvidia-smi.
 *
 * Cible : Xiaomi 12T Pro (diting, Adreno 730) sous Linux avec le pilote
 * DRM msm (Mesa freedreno). Aucune dépendance en dehors de la libc.
 *
 * Sources des données :
 *   - fréquence / gouverneur : /sys/class/devfreq/<...gpu...>/
 *   - température            : /sys/class/thermal/thermal_zone*  (type "gpu")
 *   - charge + processus     : /proc/<pid>/fdinfo/<fd>  (clés drm-engine-gpu,
 *                              drm-client-id, drm-resident-memory du pilote msm)
 *
 * Compilation :  gcc -O2 -Wall -Wextra -o adreno-smi adreno-smi.c
 *
 * ADRENO_SMI_ROOT (variable d'environnement) préfixe /sys et /proc : sert
 * uniquement à tester sur un faux arbre de fichiers.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#define VERSION "0.1"
#define INNER 77 /* largeur utile du cadre */
#define BAR_W 40

static const char *g_root = "";
static volatile sig_atomic_t g_stop = 0;

/* ------------------------------------------------------------------ */
/* Utilitaires                                                         */
/* ------------------------------------------------------------------ */

/* Construit un chemin préfixé par g_root (8 tampons tournants). */
static const char *P(const char *fmt, ...)
{
    static char bufs[8][PATH_MAX];
    static unsigned idx;
    char *b = bufs[idx++ & 7];
    int off = snprintf(b, PATH_MAX, "%s", g_root);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b + off, PATH_MAX - off, fmt, ap);
    va_end(ap);
    return b;
}

static int read_str(const char *path, char *buf, size_t n)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    size_t r = fread(buf, 1, n - 1, f);
    fclose(f);
    buf[r] = 0;
    size_t l = strlen(buf);
    while (l && (buf[l - 1] == '\n' || buf[l - 1] == ' ' || buf[l - 1] == '\t'))
        buf[--l] = 0;
    return 0;
}

static int read_ll(const char *path, long long *v)
{
    char b[64], *e;
    if (read_str(path, b, sizeof b))
        return -1;
    long long x = strtoll(b, &e, 10);
    if (e == b)
        return -1;
    *v = x;
    return 0;
}

static int u8len(const char *s)
{
    int n = 0;
    for (; *s; s++)
        if (((unsigned char)*s & 0xC0) != 0x80)
            n++;
    return n;
}

static void u8trunc(char *s, int max)
{
    int n = 0;
    for (char *p = s; *p; p++) {
        if (((unsigned char)*p & 0xC0) != 0x80) {
            if (n == max) {
                *p = 0;
                return;
            }
            n++;
        }
    }
}

static void rule(char c)
{
    putchar('+');
    for (int i = 0; i < INNER; i++)
        putchar(c);
    puts("+");
}

static void boxline(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    u8trunc(buf, INNER);
    printf("|%s%*s|\n", buf, INNER - u8len(buf), "");
}

/* ------------------------------------------------------------------ */
/* Détection du GPU                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    char dev[PATH_MAX]; /* /sys/class/devfreq/3d00000.gpu */
    char name[48];
} gpu_t;

static int detect_gpu(gpu_t *g)
{
    DIR *d = opendir(P("/sys/class/devfreq"));
    if (!d)
        return -1;
    struct dirent *e;
    int found = 0;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        if (strcasestr(e->d_name, "gpu") || strcasestr(e->d_name, "adreno")) {
            snprintf(g->dev, sizeof g->dev, "%s", P("/sys/class/devfreq/%s", e->d_name));
            found = 1;
            break;
        }
    }
    closedir(d);
    if (!found)
        return -1;

    /* Nom par défaut = le téléphone visé ; affiné via le device-tree si possible. */
    snprintf(g->name, sizeof g->name, "Adreno 730");
    char rp[PATH_MAX];
    if (realpath(g->dev, rp)) {
        char *s = strrchr(rp, '/'); /* .../3d00000.gpu/devfreq */
        if (s)
            *s = 0;
        s = strrchr(rp, '/'); /* .../3d00000.gpu */
        if (s)
            *s = 0;
        char path[PATH_MAX + 32], comp[96];
        snprintf(path, sizeof path, "%s/of_node/compatible", rp);
        if (!read_str(path, comp, sizeof comp) && strstr(comp, "adreno")) {
            const char *dash = strrchr(comp, '-');
            unsigned long id = dash ? strtoul(dash + 1, NULL, 16) : 0;
            if (id == 0x43050a01UL)
                snprintf(g->name, sizeof g->name, "Adreno 730");
            else if (id == 0x43051401UL)
                snprintf(g->name, sizeof g->name, "Adreno 740");
            else if (id)
                snprintf(g->name, sizeof g->name, "Adreno %08lx", id);
        }
    }
    return 0;
}

static int rd_dev(const gpu_t *g, const char *attr, long long *v)
{
    char p[PATH_MAX + 64];
    snprintf(p, sizeof p, "%s/%s", g->dev, attr);
    return read_ll(p, v);
}

/* Température max des zones thermiques dont le type contient "gpu". */
static int gpu_temp(double *maxc)
{
    DIR *d = opendir(P("/sys/class/thermal"));
    if (!d)
        return -1;
    struct dirent *e;
    int n = 0;
    double mx = -1e9;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "thermal_zone", 12))
            continue;
        char t[64];
        if (read_str(P("/sys/class/thermal/%s/type", e->d_name), t, sizeof t))
            continue;
        if (!strcasestr(t, "gpu"))
            continue;
        long long v;
        if (read_ll(P("/sys/class/thermal/%s/temp", e->d_name), &v))
            continue;
        double c = (v >= 1000 || v <= -1000) ? v / 1000.0 : (double)v;
        if (c > mx)
            mx = c;
        n++;
    }
    closedir(d);
    if (!n)
        return -1;
    *maxc = mx;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Échantillonnage des clients DRM via fdinfo                          */
/* ------------------------------------------------------------------ */

typedef struct {
    long pid;
    char comm[32];
    char driver[16];
    long long client; /* drm-client-id, -1 si inconnu */
    int has_engine;
    unsigned long long engine_ns;
    long long mem_kib; /* -1 si inconnu */
} client_t;

typedef struct {
    client_t *v;
    size_t n, cap;
    struct timespec t;
    int denied; /* processus dont /proc/<pid>/fd est illisible */
} sample_t;

static long long parse_kib(const char *val)
{
    char *e;
    unsigned long long x = strtoull(val, &e, 10);
    while (*e == ' ')
        e++;
    if (!strncmp(e, "KiB", 3))
        return (long long)x;
    if (!strncmp(e, "MiB", 3))
        return (long long)x * 1024;
    if (!strncmp(e, "GiB", 3))
        return (long long)x * 1024 * 1024;
    return (long long)(x / 1024); /* octets */
}

static int parse_fdinfo(const char *path, client_t *c)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    memset(c, 0, sizeof *c);
    c->client = -1;
    c->mem_kib = -1;
    long long resident = -1, total = -1;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char *colon = strchr(line, ':');
        if (!colon)
            continue;
        *colon = 0;
        char *val = colon + 1;
        while (*val == ' ' || *val == '\t')
            val++;
        if (!strcmp(line, "drm-driver"))
            sscanf(val, "%15s", c->driver);
        else if (!strcmp(line, "drm-client-id"))
            c->client = strtoll(val, NULL, 10);
        else if (!strcmp(line, "drm-engine-gpu")) {
            c->engine_ns = strtoull(val, NULL, 10);
            c->has_engine = 1;
        } else if (!strcmp(line, "drm-resident-memory"))
            resident = parse_kib(val);
        else if (!strcmp(line, "drm-total-memory"))
            total = parse_kib(val);
    }
    fclose(f);
    c->mem_kib = resident >= 0 ? resident : total;
    return 0;
}

static void add_client(sample_t *s, const client_t *c)
{
    for (size_t i = 0; i < s->n; i++) {
        if (c->client >= 0 && s->v[i].client == c->client)
            return; /* même client (fd dupliqué ou hérité) */
        if (c->client < 0 && s->v[i].client < 0 && s->v[i].pid == c->pid)
            return;
    }
    if (s->n == s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 16;
        client_t *nv = realloc(s->v, nc * sizeof *nv);
        if (!nv)
            return;
        s->v = nv;
        s->cap = nc;
    }
    s->v[s->n++] = *c;
}

static void read_comm(long pid, char *out, size_t n)
{
    snprintf(out, n, "?");
    char b[64];
    if (read_str(P("/proc/%ld/comm", pid), b, sizeof b)) {
        return;
    }
    for (char *p = b; *p; p++)
        if ((unsigned char)*p < 0x20)
            *p = '?';
    snprintf(out, n, "%.31s", b);
}

static void sample(sample_t *s)
{
    memset(s, 0, sizeof *s);
    clock_gettime(CLOCK_MONOTONIC, &s->t);
    DIR *pd = opendir(P("/proc"));
    if (!pd)
        return;
    struct dirent *pe;
    while ((pe = readdir(pd))) {
        char *end;
        long pid = strtol(pe->d_name, &end, 10);
        if (*end || pid <= 0)
            continue;
        DIR *fd = opendir(P("/proc/%ld/fd", pid));
        if (!fd) {
            if (errno == EACCES || errno == EPERM)
                s->denied++;
            continue;
        }
        char comm[32];
        int have_comm = 0;
        struct dirent *fe;
        while ((fe = readdir(fd))) {
            if (fe->d_name[0] == '.')
                continue;
            char target[PATH_MAX];
            ssize_t n = readlink(P("/proc/%ld/fd/%s", pid, fe->d_name), target, sizeof target - 1);
            if (n <= 0)
                continue;
            target[n] = 0;
            if (strncmp(target, "/dev/dri/", 9))
                continue;
            client_t c;
            if (parse_fdinfo(P("/proc/%ld/fdinfo/%s", pid, fe->d_name), &c))
                continue;
            if (c.driver[0] && strcmp(c.driver, "msm"))
                continue; /* un autre GPU */
            if (!have_comm) {
                read_comm(pid, comm, sizeof comm);
                have_comm = 1;
            }
            c.pid = pid;
            snprintf(c.comm, sizeof c.comm, "%s", comm);
            add_client(s, &c);
        }
        closedir(fd);
    }
    closedir(pd);
}

/* ------------------------------------------------------------------ */
/* Affichage                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    long pid;
    char comm[32];
    double pct;       /* -1 = inconnu */
    long long mem_kib; /* -1 = inconnu */
} row_t;

static int cmp_row(const void *x, const void *y)
{
    const row_t *a = x, *b = y;
    if (a->pct != b->pct)
        return a->pct < b->pct ? 1 : -1;
    if (a->mem_kib != b->mem_kib)
        return a->mem_kib < b->mem_kib ? 1 : -1;
    return a->pid < b->pid ? -1 : a->pid > b->pid;
}

static void fmt_mem(char *out, size_t n, long long kib)
{
    if (kib < 0)
        snprintf(out, n, "-");
    else if (kib >= 1024)
        snprintf(out, n, "%lld MiB", kib / 1024);
    else
        snprintf(out, n, "%lld KiB", kib);
}

static void render(const gpu_t *g, const sample_t *a, const sample_t *b, int loop)
{
    static int first = 1;
    if (loop && isatty(STDOUT_FILENO)) {
        fputs(first ? "\033[2J\033[H" : "\033[H", stdout);
    }
    first = 0;

    time_t now = time(NULL);
    char ts[64];
    strftime(ts, sizeof ts, "%a %b %e %H:%M:%S %Y", localtime(&now));
    struct utsname u;
    uname(&u);

    long long cur = -1, mx = -1, mn = -1;
    rd_dev(g, "cur_freq", &cur);
    rd_dev(g, "max_freq", &mx);
    rd_dev(g, "min_freq", &mn);
    char gov[48] = "n/a";
    {
        char p[PATH_MAX + 64];
        snprintf(p, sizeof p, "%s/governor", g->dev);
        read_str(p, gov, sizeof gov);
    }
    double temp = 0;
    int has_temp = gpu_temp(&temp) == 0;

    /* --- lignes par processus --- */
    double dt = (b->t.tv_sec - a->t.tv_sec) + (b->t.tv_nsec - a->t.tv_nsec) / 1e9;
    if (dt <= 0)
        dt = 1e-9;
    row_t *rows = calloc(b->n + 1, sizeof *rows);
    size_t nrows = 0;
    int any_engine = 0;
    double total = 0;
    for (size_t i = 0; i < b->n; i++) {
        const client_t *c = &b->v[i];
        row_t *r = NULL;
        for (size_t j = 0; j < nrows; j++)
            if (rows[j].pid == c->pid) {
                r = &rows[j];
                break;
            }
        if (!r) {
            r = &rows[nrows++];
            r->pid = c->pid;
            snprintf(r->comm, sizeof r->comm, "%s", c->comm);
            r->pct = -1;
            r->mem_kib = -1;
        }
        if (c->has_engine) {
            any_engine = 1;
            if (r->pct < 0)
                r->pct = 0;
            for (size_t j = 0; j < a->n; j++) {
                const client_t *p = &a->v[j];
                if (c->client >= 0 && p->client == c->client && p->has_engine &&
                    c->engine_ns >= p->engine_ns) {
                    double d = (double)(c->engine_ns - p->engine_ns) / 1e9 / dt * 100.0;
                    r->pct += d;
                    total += d;
                    break;
                }
            }
            if (r->pct > 100)
                r->pct = 100;
        }
        if (c->mem_kib >= 0) {
            if (r->mem_kib < 0)
                r->mem_kib = 0;
            r->mem_kib += c->mem_kib;
        }
    }
    qsort(rows, nrows, sizeof *rows, cmp_row);
    if (total > 100)
        total = 100;

    /* --- cadre principal --- */
    puts(ts);
    rule('-');
    boxline(" adreno-smi %s          Pilote: msm (DRM)          Noyau: %s", VERSION, u.release);
    rule('-');
    boxline(" %-4s %-14s %-22s %-8s %s", "GPU", "Nom", "Freq (actuelle / max)", "Temp", "Charge");
    boxline("%s", "");
    char fs[48], ts2[16], ls[16];
    if (cur >= 0 && mx >= 0)
        snprintf(fs, sizeof fs, "%lld / %lld MHz", cur / 1000000, mx / 1000000);
    else if (cur >= 0)
        snprintf(fs, sizeof fs, "%lld MHz", cur / 1000000);
    else
        snprintf(fs, sizeof fs, "n/a");
    if (has_temp)
        snprintf(ts2, sizeof ts2, "%.0f C", temp);
    else
        snprintf(ts2, sizeof ts2, "n/a");
    if (any_engine)
        snprintf(ls, sizeof ls, "%.0f %%", total);
    else
        snprintf(ls, sizeof ls, "n/a");
    boxline(" %-4s %-14s %-22s %-8s %s", "0", g->name, fs, ts2, ls);

    if (any_engine) {
        char bar[BAR_W + 3];
        int fill = (int)(total / 100.0 * BAR_W + 0.5);
        bar[0] = '[';
        for (int i = 0; i < BAR_W; i++)
            bar[1 + i] = i < fill ? '#' : '-';
        bar[BAR_W + 1] = ']';
        bar[BAR_W + 2] = 0;
        boxline(" %s", bar);
    }
    if (mn >= 0 && mx >= 0)
        boxline(" Gouverneur: %s   Plage: %lld-%lld MHz", gov, mn / 1000000, mx / 1000000);
    else
        boxline(" Gouverneur: %s", gov);

    /* --- processus --- */
    rule('-');
    boxline(" Processus utilisant le GPU");
    boxline(" %7s %-5s %10s %6s  %s", "PID", "Type", "Mem GPU", "GPU", "Nom");
    rule('=');
    if (!nrows)
        boxline("  Aucun processus utilisant le GPU n'a ete trouve.");
    for (size_t i = 0; i < nrows; i++) {
        char ms[32], ps[16];
        fmt_mem(ms, sizeof ms, rows[i].mem_kib);
        if (rows[i].pct < 0)
            snprintf(ps, sizeof ps, "-");
        else
            snprintf(ps, sizeof ps, "%.0f %%", rows[i].pct);
        boxline(" %7ld %-5s %10s %6s  %s", rows[i].pid, "DRM", ms, ps, rows[i].comm);
    }
    rule('-');
    if (!any_engine)
        boxline(" Charge indisponible : pas de stats drm-engine-gpu lisibles (sudo ? noyau ?)");
    if (b->denied)
        boxline(" %d processus inaccessibles : relancer avec sudo pour tout voir", b->denied);
    if (!any_engine || b->denied)
        rule('-');

    free(rows);
    if (loop && isatty(STDOUT_FILENO))
        fputs("\033[J", stdout);
    fflush(stdout);
}

/* ------------------------------------------------------------------ */

static void on_sig(int s)
{
    (void)s;
    g_stop = 1;
}

static void usage(const char *p)
{
    printf("adreno-smi %s — moniteur GPU Adreno (pilote msm)\n\n"
           "Usage : %s [-l [SEC]] [-h] [-V]\n\n"
           "  (sans option)  un seul affichage (mesure sur 0,5 s)\n"
           "  -l, --loop [SEC]  rafraichit toutes les SEC secondes (1 par defaut), Ctrl+C pour quitter\n"
           "  -h, --help     cette aide\n"
           "  -V, --version  version\n",
           VERSION, p);
}

int main(int argc, char **argv)
{
    double interval = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        } else if (!strcmp(argv[i], "-V") || !strcmp(argv[i], "--version")) {
            puts("adreno-smi " VERSION);
            return 0;
        } else if (!strcmp(argv[i], "-l") || !strcmp(argv[i], "--loop")) {
            interval = 1;
            if (i + 1 < argc) {
                char *e;
                double v = strtod(argv[i + 1], &e);
                if (*e == 0 && e != argv[i + 1] && v > 0) {
                    interval = v;
                    i++;
                }
            }
        } else {
            fprintf(stderr, "option inconnue : %s\n", argv[i]);
            usage(argv[0]);
            return 2;
        }
    }

    const char *r = getenv("ADRENO_SMI_ROOT");
    if (r)
        g_root = r;

    gpu_t g;
    if (detect_gpu(&g)) {
        fprintf(stderr, "adreno-smi : aucun GPU trouve (pas de /sys/class/devfreq/*gpu*).\n"
                        "Le pilote msm est-il charge ?\n");
        return 1;
    }

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    sample_t prev, cur;
    sample(&prev);
    for (;;) {
        double wait = interval > 0 ? interval : 0.5;
        struct timespec ts = {(time_t)wait, (long)((wait - (time_t)wait) * 1e9)};
        nanosleep(&ts, NULL);
        if (g_stop)
            break;
        sample(&cur);
        render(&g, &prev, &cur, interval > 0);
        free(prev.v);
        prev = cur;
        if (interval <= 0)
            break;
    }
    free(prev.v);
    return 0;
}
