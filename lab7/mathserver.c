#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>


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

Context contexts[16];
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
FILE* output_file;

//Tokenize command
char** tokenize(char *input){
    char *token;
    static char* command[3];
    int i = 0;

    token = strtok(input, " ");
    while (token != NULL && i < 3) {
        command[i++] = token;
        token = strtok(NULL, " ");
    }
    return command;
}

//Add Operation
void add_operation(Context *ctx, Operation op){
    int size = ctx->op_size + 1;
    Operation *new_op = realloc(ctx->operations, size * sizeof(Operation));
    if (new_op == NULL){
        perror("Operation allocation error!");
        exit(-1);
    }
    ctx->operations = new_op;
    ctx->op_size = size;
    ctx->operations[size - 1] = op;
}

void *run_ops(void *arg){
    Context *ctx = (Context*) arg;
    char* log_batch[10];
    int batch_count = 0;
    for(int i = 0; i < ctx->op_size; i++){
        Operation *op = &ctx->operations[i];
        //Process operations and store results
        if(strcmp(op->instruction, "set") == 0){
            ctx->value = op->val;
            char *log_line = malloc(64); //64 char long just incase
            snprintf(log_line, 128, "ctx %02d: set to value %d\n", ctx->id, ctx->value);
            log_batch[batch_count++] = log_line;
        }
        if(strcmp(op->instruction, "add") == 0){
            ctx->value += op->val;
            char *log_line = malloc(64); //64 char long just incase
            snprintf(log_line, 128, "ctx %02d: add %d (result: %d)\n", ctx->id, op->val, ctx->value);
            log_batch[batch_count++] = log_line;

        }
        if(strcmp(op->instruction, "sub") == 0){
            ctx->value -= op->val;
            char *log_line = malloc(64); //64 char long just incase
            snprintf(log_line, 128, "ctx %02d: sub %d (result: %d)\n", ctx->id, op->val, ctx->value);
            log_batch[batch_count++] = log_line;
        }

        //TODO More Process operations !!!!!!!!!!!!!!!!!

        //Free duplicated string instruction
        free(op->instruction);
        //Log lines
        if(batch_count == 10){
            pthread_mutex_lock(&log_mutex);
            for(int x = 0; x < 10; x++){
                fputs(log_batch[x], output_file);
                free(log_batch[x]);
            }
            pthread_mutex_unlock(&log_mutex);
            batch_count = 0;
        } 
    }
    //Log lines if less than 10 remaining
    if (batch_count > 0) {
        pthread_mutex_lock(&log_mutex);
        for(int x = 0; x < batch_count; x++){
            fputs(log_batch[x], output_file);
            free(log_batch[x]);
        }
        pthread_mutex_unlock(&log_mutex);
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    const char usage[] = "Usage: mathserver.out <input trace> <output trace>\n";
    char* input_trace;
    char* output_trace;
    char buffer[1024];
    

    // Parse command line arguments
    if (argc != 3) {
        printf("%s", usage);
        return 1;
    }
    
    input_trace = argv[1];
    output_trace = argv[2];

    // Open input and output files
    FILE* input_file = fopen(input_trace, "r");
    output_file = fopen(output_trace, "w");

    //Initialize contexts
    for(int i = 0; i < 16; i++){
        contexts[i].id = i;
        contexts[i].value = 0;
        contexts[i].operations = NULL;
        contexts[i].op_size = 0;
    }


    while ( !feof(input_file) ) {
        // Read input file line by line
        char *rez = fgets(buffer, sizeof(buffer), input_file);
        if ( !rez )
            break;
        else {
            // Remove endline character
            buffer[strlen(buffer) - 1] = '\0';
        }
        char** command = tokenize(rez);
        
        Operation new_op;
        new_op.instruction = strdup(command[0]);
        new_op.val = atoi(command[2]);
        int context_num = atoi(command[1]);
        add_operation(&contexts[context_num], new_op); 
    }

    //Create threads
    for(int i = 0; i < 16; i++){
        if(contexts[i].op_size > 0){
            pthread_create(&contexts[i].thread, NULL, run_ops, &contexts[i]);
        }
    }

    //Join threads
    for(int i = 0; i < 16; i++){
        if(contexts[i].op_size > 0){
            pthread_join(contexts[i].thread, NULL);
        }
    }

    fclose(input_file);
    fclose(output_file);
    pthread_mutex_destroy(&log_mutex);

    return 0;
}