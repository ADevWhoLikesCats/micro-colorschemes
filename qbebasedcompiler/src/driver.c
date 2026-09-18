/* driver.c — CLI driver.
 * Pipeline:
 *   input.c -> gcc -E -> input.i
 *   input.i -> minicc frontend + qbe emitter -> input.qbe
 *   input.qbe -> qbe -> input.s
 *   input.s -> as -> input.o
 *   input.o -> ld + crt + libc -> executable
 *   copy msys-2.0.dll next to the executable
 */

/* Where the bundled toolchain lives, relative to the compiler binary.
 * Override with MINICC_BIN. */
static char driver_bin_dir[1024];

static void driver_find_own_dir(void) {
    char buf[1024];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n > 0) {
        buf[n] = '\0';
        char *slash = strrchr(buf, '/');
        if (slash) *slash = '\0';
        snprintf(driver_bin_dir, sizeof driver_bin_dir, "%s", buf);
        return;
    }
    /* fallback: current directory */
    snprintf(driver_bin_dir, sizeof driver_bin_dir, ".");
}

/* Build "bin/<name>" as an absolute path relative to the compiler. */
static const char *driver_tool_path(const char *name) {
    static char buf[1200];
    snprintf(buf, sizeof buf, "%s/%s", driver_bin_dir, name);
    return buf;
}

/* Build "bin/<name>" for the directory holding the tools. */
static const char *driver_bin_file(const char *name) {
    static char buf[1200];
    snprintf(buf, sizeof buf, "%s/%s", driver_bin_dir, name);
    return buf;
}

static char *driver_read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, (size_t)n, f);
    buf[n] = '\0';
    fclose(f);
    if (out_len) *out_len = (size_t)n;
    return buf;
}

static char *driver_derive_name(const char *path, const char *ext) {
    const char *base = strrchr(path, '/');
    const char *base2 = strrchr(path, '\\');
    if (base2 && (!base || base2 > base)) base = base2;
    base = base ? base + 1 : path;
    const char *dot = strrchr(base, '.');
    size_t stem = dot ? (size_t)(dot - base) : strlen(base);
    size_t elen = strlen(ext);
    char *out = malloc(stem + elen + 1);
    memcpy(out, base, stem);
    memcpy(out + stem, ext, elen + 1);
    return out;
}

static int driver_run(char *const argv[], bool verbose) {
    if (verbose) {
        for (int i = 0; argv[i]; i++)
            fprintf(stderr, "%s%s", i ? " " : "", argv[i]);
        fputc('\n', stderr);
    }
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }
    if (pid == 0) {
        execvp(argv[0], argv);
        fprintf(stderr, "%s: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

static void driver_usage(const char *prog) {
    fprintf(stderr,
        "usage: %s [options] file.c\n"
        "  -o FILE     output file (default: a.out)\n"
        "  -S          emit assembly only\n"
        "  -c          emit object only, don't link\n"
        "  -emit-qbe   print QBE IL to stdout and stop\n"
        "  -E          preprocess only, print result to stdout\n"
        "  -I DIR      add include directory\n"
        "  -D NAME[=V] define macro\n"
        "  -U NAME     undefine macro\n"
        "  -v          print each command\n"
        "  -h, --help  show this\n",
        prog);
}

/* --- preprocessing ------------------------------------------------ */

static char *driver_preprocess(const char *input,
                               char **extra, int nextra,
                               bool verbose) {
    char *tmp = driver_derive_name(input, ".i");

    const char *cpp_bin = driver_tool_path("cpp.exe");
    /* fall back to system gcc if the bundled cpp isn't there */
    if (access(cpp_bin, X_OK) != 0) cpp_bin = "gcc";

    char **argv = malloc((size_t)(8 + nextra) * sizeof(char *));
    int i = 0;
    argv[i++] = (char *)cpp_bin;
    argv[i++] = "-E";
    argv[i++] = "-P";
    argv[i++] = "-o";
    argv[i++] = tmp;
    argv[i++] = (char *)input;
    for (int k = 0; k < nextra; k++) argv[i++] = extra[k];
    argv[i] = NULL;

    int rc = driver_run(argv, verbose);
    free(argv);
    if (rc != 0) {
        fprintf(stderr, "minicc: preprocessor failed\n");
        free(tmp);
        return NULL;
    }
    size_t len = 0;
    char *src = driver_read_file(tmp, &len);
    free(tmp);
    return src;
}

/* --- frontend ----------------------------------------------------- */

static int driver_compile_qbe(const char *src, size_t len,
                              const char *filename,
                              const char *qbe_path) {
    Lexer L;
    lexer_init(&L, src, len, filename);
    Node *root = parse_file(&L);

    Sema S;
    memset(&S, 0, sizeof(S));
    sema_program(&S, root);

    FILE *qf = fopen(qbe_path, "w");
    if (!qf) { perror(qbe_path); return 1; }
    qbe_emit(qf, root);
    fclose(qf);
    return 0;
}

/* --- linking ------------------------------------------------------ */

static int driver_link(const char *obj_path, const char *exe, bool verbose) {
    const char *ld_bin = driver_tool_path("ld.exe");
    if (access(ld_bin, X_OK) != 0) {
        fprintf(stderr, "minicc: cannot find %s\n", ld_bin);
        return 1;
    }

    const char *crt0     = driver_bin_file("crt0.o");
    const char *crti     = driver_bin_file("crti.o");
    const char *crtbegin = driver_bin_file("crtbegin.o");
    const char *crtend   = driver_bin_file("crtend.o");
    const char *crtn     = driver_bin_file("crtn.o");
    const char *libdir   = driver_bin_dir;

    char libflag[1200];
    snprintf(libflag, sizeof libflag, "-L%s", libdir);

    char *const argv[] = {
        (char *)ld_bin,
        "-o", (char *)exe,
        (char *)crt0,
        (char *)crti,
        (char *)crtbegin,
        (char *)obj_path,
        libflag,
        "-lmsys-2.0",
        "-lmingw32",
        "-lmsvcrt",
        "-lgcc",
        "-ladvapi32",
        "-lshell32",
        "-luser32",
        "-lkernel32",
        (char *)crtend,
        (char *)crtn,
        NULL
    };

    int rc = driver_run(argv, verbose);
    if (rc != 0) {
        fprintf(stderr, "minicc: link failed (ld exited %d)\n", rc);
        return rc;
    }
    return 0;
}

/* Copy msys-2.0.dll next to the produced executable, if the DLL isn't
 * already there. Windows will find it via the DLL search order. */
static void driver_copy_runtime(const char *exe) {
    char dst[1200];
    const char *slash = strrchr(exe, '/');
    const char *bslash = strrchr(exe, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
    if (slash) {
        size_t n = (size_t)(slash - exe);
        snprintf(dst, sizeof dst, "%.*s/msys-2.0.dll", (int)n, exe);
    } else {
        snprintf(dst, sizeof dst, "msys-2.0.dll");
    }

    if (access(dst, F_OK) == 0) return;

    const char *src = driver_bin_file("msys-2.0.dll");
    FILE *in = fopen(src, "rb");
    if (!in) return;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return; }

    char buf[65536];
    size_t r;
    while ((r = fread(buf, 1, sizeof buf, in)) > 0)
        fwrite(buf, 1, r, out);
    fclose(in);
    fclose(out);
}

/* --- main --------------------------------------------------------- */

static int driver_main(int argc, char **argv) {
    driver_find_own_dir();

    const char *input  = NULL;
    const char *output = NULL;
    bool emit_qbe_only = false;
    bool emit_asm_only = false;
    bool emit_obj_only = false;
    bool emit_pp_only  = false;
    bool verbose       = false;

    char *extra[256];
    int  nextra = 0;

    for (int i = 1; i < argc; i++) {
        char *a = argv[i];
        if (strcmp(a, "-o") == 0 && i + 1 < argc) {
            output = argv[++i];
        } else if (strcmp(a, "-S") == 0) {
            emit_asm_only = true;
        } else if (strcmp(a, "-c") == 0) {
            emit_obj_only = true;
        } else if (strcmp(a, "-emit-qbe") == 0) {
            emit_qbe_only = true;
        } else if (strcmp(a, "-E") == 0) {
            emit_pp_only = true;
        } else if (strcmp(a, "-v") == 0) {
            verbose = true;
        } else if (strcmp(a, "-I") == 0 && i + 1 < argc) {
            if (nextra + 2 < 256) { extra[nextra++] = a; extra[nextra++] = argv[++i]; }
        } else if ((strcmp(a, "-D") == 0 || strcmp(a, "-U") == 0) && i + 1 < argc) {
            if (nextra + 2 < 256) { extra[nextra++] = a; extra[nextra++] = argv[++i]; }
        } else if ((a[0] == '-' && a[1] == 'I' && a[2]) ||
                   (a[0] == '-' && a[1] == 'D' && a[2]) ||
                   (a[0] == '-' && a[1] == 'U' && a[2])) {
            if (nextra < 256) extra[nextra++] = a;
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            driver_usage(argv[0]);
            return 0;
        } else if (a[0] == '-') {
            fprintf(stderr, "%s: unknown option '%s'\n", argv[0], a);
            driver_usage(argv[0]);
            return 1;
        } else {
            if (input) {
                fprintf(stderr, "%s: multiple inputs not supported\n", argv[0]);
                return 1;
            }
            input = a;
        }
    }

    if (!input) { driver_usage(argv[0]); return 1; }

    if (emit_pp_only) {
        char *src = driver_preprocess(input, extra, nextra, verbose);
        if (!src) return 1;
        fputs(src, stdout);
        free(src);
        return 0;
    }

    char *src = driver_preprocess(input, extra, nextra, verbose);
    if (!src) return 1;
    size_t src_len = strlen(src);

    if (emit_qbe_only) {
        Lexer L;
        lexer_init(&L, src, src_len, input);
        Node *root = parse_file(&L);
        Sema S;
        memset(&S, 0, sizeof(S));
        sema_program(&S, root);
        qbe_emit(stdout, root);
        free(src);
        return 0;
    }

    /* 1. frontend -> .qbe */
    char *qbe_path = driver_derive_name(input, ".qbe");
    if (driver_compile_qbe(src, src_len, input, qbe_path) != 0) {
        free(src); return 1;
    }
    free(src);

    /* 2. qbe -> .s */
    const char *qbe_bin = driver_tool_path("qbe.exe");
    if (access(qbe_bin, X_OK) != 0) qbe_bin = "qbe";

    char *asm_path = driver_derive_name(input, ".s");
    char *const qbe_argv[] = {
        (char *)qbe_bin, "-o", asm_path, qbe_path, NULL
    };
    if (driver_run(qbe_argv, verbose) != 0) {
        fprintf(stderr, "%s: qbe failed on %s\n", argv[0], qbe_path);
        return 1;
    }

    if (emit_asm_only) {
        if (output && strcmp(output, asm_path) != 0)
            rename(asm_path, output);
        return 0;
    }

    /* 3. as -> .o */
    const char *as_bin = driver_tool_path("as.exe");
    if (access(as_bin, X_OK) != 0) as_bin = "as";

    char *obj_path = driver_derive_name(input, ".o");
    char *const as_argv[] = {
        (char *)as_bin, "-o", obj_path, asm_path, NULL
    };
    if (driver_run(as_argv, verbose) != 0) {
        fprintf(stderr, "%s: as failed on %s\n", argv[0], asm_path);
        return 1;
    }

    if (emit_obj_only) {
        if (output && strcmp(output, obj_path) != 0)
            rename(obj_path, output);
        return 0;
    }

    /* 4. link */
    const char *exe = output ? output : "a.out";
    if (driver_link(obj_path, exe, verbose) != 0) return 1;

    /* 5. put msys-2.0.dll next to the executable */
    driver_copy_runtime(exe);

    return 0;
}
