#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

typedef struct {
    char *instruction;
    int val;
} Operation;

typedef struct {
    int id;
    int value;
    Operation *operations;
    int op_size;
    pthread_t thread;
} Context;

#define NUM_CONTEXTS 16
#define LOG_BATCH_SIZE 10

static Context contexts[NUM_CONTEXTS];
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static FILE *output_file = NULL;

/* Helper to append formatted text to a dynamically growing buffer */
static void appendf(char **buf, size_t *cap, size_t *len, const char *fmt, ...) {
    va_list args;
    while (1) {
        if (*len + 1 >= *cap) {
            *cap *= 2;
            char *tmp = realloc(*buf, *cap);
            if (!tmp) {
                perror("realloc");
                exit(1);
            }
            *buf = tmp;
        }
        va_start(args, fmt);
        int written = vsnprintf(*buf + *len, *cap - *len, fmt, args);
        va_end(args);
        if (written < 0) {
            perror("vsnprintf");
            exit(1);
        }
        if ((size_t)written < *cap - *len) {
            *len += (size_t)written;
            break;
        }
        /* not enough space, grow and retry */
        *cap += (size_t)written + 16;
        char *tmp = realloc(*buf, *cap);
        if (!tmp) {
            perror("realloc");
            exit(1);
        }
        *buf = tmp;
    }
}

/* Tokenize a line into at most 3 tokens */
static char **tokenize(char *input) {
    static char *command[3];
    for (int i = 0; i < 3; i++) {
        command[i] = NULL;
    }
    char *token = strtok(input, " ");
    int i = 0;
    while (token != NULL && i < 3) {
        command[i++] = token;
        token = strtok(NULL, " ");
    }
    return command;
}

/* Add operation to context queue */
static void add_operation(Context *ctx, Operation op) {
    int new_size = ctx->op_size + 1;
    Operation *new_ops = realloc(ctx->operations, new_size * sizeof(Operation));
    if (!new_ops) {
        perror("realloc operations");
        exit(1);
    }
    ctx->operations = new_ops;
    ctx->operations[new_size - 1] = op;
    ctx->op_size = new_size;
}

/* Slow recursive Fibonacci as requested */
static int fib_recursive(int n) {
    if (n <= 1) return n;
    return fib_recursive(n - 1) + fib_recursive(n - 2);
}

static void *run_ops(void *arg) {
    Context *ctx = (Context *)arg;
    char *log_batch[LOG_BATCH_SIZE];
    int batch_count = 0;

    for (int i = 0; i < ctx->op_size; i++) {
        Operation *op = &ctx->operations[i];
        char *log_line = NULL;

        if (strcmp(op->instruction, "set") == 0) {
            ctx->value = op->val;
            log_line = malloc(64);
            if (!log_line) { perror("malloc"); exit(1); }
            snprintf(log_line, 64, "ctx %02d: set to value %d\n", ctx->id, ctx->value);
        } else if (strcmp(op->instruction, "add") == 0) {
            ctx->value += op->val;
            log_line = malloc(64);
            if (!log_line) { perror("malloc"); exit(1); }
            snprintf(log_line, 64, "ctx %02d: add %d (result: %d)\n", ctx->id, op->val, ctx->value);
        } else if (strcmp(op->instruction, "sub") == 0) {
            ctx->value -= op->val;
            log_line = malloc(64);
            if (!log_line) { perror("malloc"); exit(1); }
            snprintf(log_line, 64, "ctx %02d: sub %d (result: %d)\n", ctx->id, op->val, ctx->value);
        } else if (strcmp(op->instruction, "mul") == 0) {
            ctx->value *= op->val;
            log_line = malloc(64);
            if (!log_line) { perror("malloc"); exit(1); }
            snprintf(log_line, 64, "ctx %02d: mul %d (result: %d)\n", ctx->id, op->val, ctx->value);
        } else if (strcmp(op->instruction, "div") == 0) {
            /* assume well-formed input; no division by zero */
            ctx->value /= op->val;
            log_line = malloc(64);
            if (!log_line) { perror("malloc"); exit(1); }
            snprintf(log_line, 64, "ctx %02d: div %d (result: %d)\n", ctx->id, op->val, ctx->value);
        } else if (strcmp(op->instruction, "fib") == 0) {
            int n = ctx->value;
            if (n < 0) n = 0;
            int f = fib_recursive(n);
            log_line = malloc(64);
            if (!log_line) { perror("malloc"); exit(1); }
            snprintf(log_line, 64, "ctx %02d: fib (result: %d)\n", ctx->id, f);
        } else if (strcmp(op->instruction, "pia") == 0) {
            int iterations = ctx->value;
            if (iterations < 0) iterations = 0;
            double sum = 0.0;
            for (int k = 0; k < iterations; k++) {
                double term = ((k % 2) == 0 ? 1.0 : -1.0) / (2.0 * k + 1.0);
                sum += term;
            }
            double pi_approx = 4.0 * sum;
            log_line = malloc(64);
            if (!log_line) { perror("malloc"); exit(1); }
            /* 15 digits after decimal point */
            snprintf(log_line, 64, "ctx %02d: pia (result %.15f)\n", ctx->id, pi_approx);
        } else if (strcmp(op->instruction, "pri") == 0) {
            int limit = ctx->value;
            if (limit < 2) {
                /* still need a log line, with empty list */
                size_t cap = 64;
                size_t len = 0;
                log_line = malloc(cap);
                if (!log_line) { perror("malloc"); exit(1); }
                appendf(&log_line, &cap, &len, "ctx %02d: primes (result:)", ctx->id);
                appendf(&log_line, &cap, &len, "\n");
            } else {
                size_t cap = 256;
                size_t len = 0;
                log_line = malloc(cap);
                if (!log_line) { perror("malloc"); exit(1); }
                appendf(&log_line, &cap, &len, "ctx %02d: primes (result:", ctx->id);

                int first = 1;
                for (int p = 2; p <= limit; p++) {
                    int is_prime = 1;
                    for (int d = 2; d * d <= p; d++) {
                        if (p % d == 0) {
                            is_prime = 0;
                            break;
                        }
                    }
                    if (is_prime) {
                        if (first) {
                            appendf(&log_line, &cap, &len, " %d", p);
                            first = 0;
                        } else {
                            appendf(&log_line, &cap, &len, ", %d", p);
                        }
                    }
                }
                appendf(&log_line, &cap, &len, ")\n");
            }
        } else {
            /* Unknown instruction; ignore */
        }

       
        free(op->instruction);

        if (log_line != NULL) {
            log_batch[batch_count++] = log_line;
        }

        if (batch_count == LOG_BATCH_SIZE) {
            pthread_mutex_lock(&log_mutex);
            for (int j = 0; j < LOG_BATCH_SIZE; j++) {
                fputs(log_batch[j], output_file);
                free(log_batch[j]);
            }
            pthread_mutex_unlock(&log_mutex);
            batch_count = 0;
        }
    }

    if (batch_count > 0) {
        pthread_mutex_lock(&log_mutex);
        for (int j = 0; j < batch_count; j++) {
            fputs(log_batch[j], output_file);
            free(log_batch[j]);
        }
        pthread_mutex_unlock(&log_mutex);
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    const char usage[] = "Usage: mathserver.out <input trace> <output trace>\n";
    char *input_trace;
    char *output_trace;
    char buffer[1024];

    if (argc != 3) {
        printf("%s", usage);
        return 1;
    }

    input_trace = argv[1];
    output_trace = argv[2];

    FILE *input_file = fopen(input_trace, "r");
    if (!input_file) {
        perror("Error opening input file");
        return 1;
    }
    output_file = fopen(output_trace, "w");
    if (!output_file) {
        perror("Error opening output file");
        fclose(input_file);
        return 1;
    }

    /* Initialize contexts */
    for (int i = 0; i < NUM_CONTEXTS; i++) {
        contexts[i].id = i;
        contexts[i].value = 0;
        contexts[i].operations = NULL;
        contexts[i].op_size = 0;
    }

    /* Read input and build operation queues */
    while (1) {
        char *rez = fgets(buffer, sizeof(buffer), input_file);
        if (!rez) break;

        /* Strip newline */
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
        }

        char *line = buffer;
        if (line[0] == '\0') continue;

        char **command = tokenize(line);
        if (!command[0] || !command[1]) {
            continue;
        }

        Operation op;
        op.instruction = strdup(command[0]);
        if (!op.instruction) {
            perror("strdup");
            exit(1);
        }

        if (command[2]) {
            op.val = atoi(command[2]);
        } else {
            op.val = 0;
        }

        int ctx_id = atoi(command[1]);
        if (ctx_id < 0 || ctx_id >= NUM_CONTEXTS) {
            free(op.instruction);
            continue;
        }

        add_operation(&contexts[ctx_id], op);
    }

    fclose(input_file);

    /* Create threads */
    for (int i = 0; i < NUM_CONTEXTS; i++) {
        if (contexts[i].op_size > 0) {
            if (pthread_create(&contexts[i].thread, NULL, run_ops, &contexts[i]) != 0) {
                perror("pthread_create");
                return 1;
            }
        }
    }

    /* Join threads */
    for (int i = 0; i < NUM_CONTEXTS; i++) {
        if (contexts[i].op_size > 0) {
            pthread_join(contexts[i].thread, NULL);
        }
    }

    /* Cleanup */
    for (int i = 0; i < NUM_CONTEXTS; i++) {
        free(contexts[i].operations);
    }

    fclose(output_file);
    pthread_mutex_destroy(&log_mutex);

    return 0;
}
