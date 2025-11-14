#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <stdint.h>

#define TRUE 1
#define FALSE 0

// Output file
FILE* output_file;

// TLB replacement strategy (FIFO or LRU)
char* strategy;

int offset = -1;
int PFN;
int VPN;
int pid = 0;
uint32_t timestamp = 0;

typedef struct{
    int valid;
    int PFN;
} PTE;

typedef struct{
    unsigned int valid;
    unsigned int PFN;
    unsigned int VPN;
    unsigned int PID;
    uint32_t timestamp;
} TLB_entry;

typedef struct{
    int PID;
    PTE pte[8];
    int r1;
    int r2;
} page_table;




TLB_entry TLB[8];
page_table page_tables[4];
uint32_t *memory = NULL;

char** tokenize_input(char* input) {
    char** tokens = NULL;
    char* token = strtok(input, " ");
    int num_tokens = 0;

    while (token != NULL) {
        num_tokens++;
        tokens = realloc(tokens, num_tokens * sizeof(char*));
        tokens[num_tokens - 1] = malloc(strlen(token) + 1);
        strcpy(tokens[num_tokens - 1], token);
        token = strtok(NULL, " ");
    }

    num_tokens++;
    tokens = realloc(tokens, num_tokens * sizeof(char*));
    tokens[num_tokens - 1] = NULL;

    return tokens;
}

int oldest_TLB(){
    int best = -1;
    int min_time = -1;

    for (int i = 0; i < 8; i++) {
        if(!TLB[i].valid){
            return i;
        }
        if (TLB[i].valid && TLB[i].timestamp < min_time) {
            min_time = TLB[i].timestamp;
            best = i;
        }
    }
    return best;
}

int lookup_TLB(int vpn, int pid) {
    for (int i = 0; i < 8; i++) {
        if (TLB[i].valid && TLB[i].VPN == vpn && TLB[i].PID == pid) {
            return i;
        }
    }
    //Return -1 on miss
    return -1;
}




int main(int argc, char* argv[]) {
    const char usage[] = "Usage: memsym.out <strategy> <input trace> <output trace>\n";
    char* input_trace;
    char* output_trace;
    char buffer[1024];

    // Parse command line arguments
    if (argc != 4) {
        printf("%s", usage);
        return 1;
    }
    strategy = argv[1];
    input_trace = argv[2];
    output_trace = argv[3];

    // Open input and output files
    FILE* input_file = fopen(input_trace, "r");
    output_file = fopen(output_trace, "w");  

    

    while ( !feof(input_file) ) {
        // Read input file line by line
        char *rez = fgets(buffer, sizeof(buffer), input_file);
        if ( !rez ) {
            fprintf(stderr, "Reached end of trace. Exiting...\n");
            return -1;
        } else {
            // Remove endline character
            buffer[strlen(buffer) - 1] = '\0';
        }
        //Skip Commented Lines
        if (buffer[0] == '%'){
            continue;
        }
        char** tokens = tokenize_input(buffer);
        
        if(offset == -1 && strcmp(tokens[0], "define") != 0){
            fprintf(output_file, "Current PID: %d. ", pid);
            fprintf(output_file, "Error: attempt to execute instruction before define\n");
            return -1;
        }
        if(strcmp(tokens[0], "define") == 0){
            if(offset != -1){
                fprintf(output_file, "Current PID: %d. ", pid);
                fprintf(output_file, "Error: multiple calls to define in the same trace\n");
                return -1;
            }
            offset = atoi(tokens[1]);
            PFN = atoi(tokens[2]);
            VPN = atoi(tokens[3]);
            //Represents Memory
            size_t memory_size = 1<<(offset+PFN);
            memory = malloc (memory_size * sizeof(uint32_t));
            memset(memory, 0, memory_size * sizeof(uint32_t));
            
            //Page tables
            for (int x = 0; x < 4; x++){
                page_tables[x].PID = x;
            }
            fprintf(output_file, "Current PID: %d. ", pid);
            fprintf(output_file, "Memory instantiation complete. OFF bits: %d. PFN bits: %d. VPN bits: %d\n", offset, PFN, VPN);
        }
        else if(strcmp(tokens[0], "ctxswitch") == 0){
            int prev_pid = pid;
            pid = atoi(tokens[1]);
            if(pid < 0 || pid > 3){
                fprintf(output_file, "Current PID: %d. ", prev_pid);
                fprintf(output_file, "Invalid context switch to process %d\n", pid);
                return -1;
            }
            fprintf(output_file, "Current PID: %d. ", pid);
            fprintf(output_file, "Switched execution context to process: %d\n", pid);
        }
        else if(strcmp(tokens[0], "load") == 0){
            //print PID
            fprintf(output_file, "Current PID: %d. ", pid);
            int src = -1;

            // Load immediate value
            if(tokens[2][0] == '#'){
                //Remove #
                for(int i = 0; tokens[2][i] != '\0'; i++){
                    tokens[2][i] = tokens[2][i+1];
                }
                src = atoi(tokens[2]);

                //Load immediate into register
                if(strcmp(tokens[1], "r1") == 0){
                    page_tables[pid].r1 = src;
                }
                else if(strcmp(tokens[1], "r2") == 0){
                    page_tables[pid].r2 = src;
                }
                else{
                    fprintf(output_file, "Error: invalid register operand %s\n", tokens[1]);
                    return -1;
                }

                fprintf(output_file, "Loaded immediate %d into register %s\n", src, tokens[1]);
            }
            //Load value from virtual memory location
            else if(tokens[2][0] != '#'){
                int virtual_address = atoi(tokens[2]);
                int vpn = virtual_address >> offset;
                int tlb_index = lookup_TLB(vpn, pid);

                int pfn = -1;

                if(tlb_index != -1 && TLB[tlb_index].valid){
                    pfn = TLB[tlb_index].PFN;
                    fprintf(output_file, "Translating. Lookup for VPN %d hit in TLB entry %d. PFN is %d\n", vpn, tlb_index, pfn);
                }
                else{
                    fprintf(output_file, "Translating. Lookup for VPN %d caused a TLB miss\n", vpn);

                    if(page_tables[pid].pte[vpn].valid){
                        pfn = page_tables[pid].pte[vpn].PFN;
                        fprintf(output_file, "Current PID: %d. ", pid);
                        fprintf(output_file, "Translating. Successfully mapped VPN %d to PFN %d\n", vpn, pfn);

                        int insert_idx = oldest_TLB();
                        TLB[insert_idx].valid = 1;
                        TLB[insert_idx].VPN = vpn;
                        TLB[insert_idx].PFN = pfn;
                        TLB[insert_idx].PID = pid;
                        TLB[insert_idx].timestamp = timestamp;
                    }
                    else{
                        fprintf(output_file, "Current PID: %d. ", pid);
                        fprintf(output_file, "Translating. Translation for VPN %d not found in page table\n", vpn);
                        return -1;
                    }
                }

                int physical_address = (pfn << offset) | (virtual_address & ((1 << offset) - 1));
                src = memory[physical_address];

                //Load value to register
                if(strcmp(tokens[1], "r1") == 0){
                    page_tables[pid].r1 = src;
                }
                else if(strcmp(tokens[1], "r2") == 0){
                    page_tables[pid].r2 = src;
                }
                else{
                    fprintf(output_file, "Current PID: %d. ", pid);
                    fprintf(output_file, "Error: invalid register operand %s\n", tokens[1]);
                    return -1;
                }
                fprintf(output_file, "Current PID: %d. ", pid);
                fprintf(output_file, "Loaded value of location %s (%d) into register %s\n", tokens[2], src, tokens[1]);
            }
        }
        else if(strcmp(tokens[0], "store") == 0) {
            //print PID
            fprintf(output_file, "Current PID: %d. ", pid);

            //Determine if <src> is register or immediate
            int value;
            int immediate_flag = 0;
            if (strcmp(tokens[2], "r1") == 0)
                value = page_tables[pid].r1;
            else if (strcmp(tokens[2], "r2") == 0)
                value = page_tables[pid].r2;
            else if (tokens[2][0] == '#'){
                value = atoi(tokens[2] + 1);
                immediate_flag = 1;
            }
            else {
                fprintf(output_file, "Error: invalid register operand %s\n", tokens[1]);
                return -1;
            }

            int virtual_address = atoi(tokens[1]);
            int vpn = virtual_address >> offset;
            int tlb_index = lookup_TLB(vpn, pid);

            if (tlb_index != -1 && TLB[tlb_index].valid) {
                int pfn = TLB[tlb_index].PFN;
                fprintf(output_file, "Translating. Lookup for VPN %d hit in TLB entry %d. PFN is %d\n", vpn, tlb_index, pfn);
            } else {
                fprintf(output_file, "Translating. Lookup for VPN %d caused a TLB miss\n", vpn);

                //Check Page Tables
                if (page_tables[pid].pte[vpn].valid) {
                    int pfn = page_tables[pid].pte[vpn].PFN;
                    fprintf(output_file, "Translating. Successfully mapped VPN %d to PFN %d\n", vpn, pfn);
                    int insert_idx = oldest_TLB();
                    TLB[insert_idx].valid = 1;
                    TLB[insert_idx].VPN = vpn;
                    TLB[insert_idx].PFN = pfn;
                    TLB[insert_idx].PID = pid;
                    TLB[insert_idx].timestamp = timestamp;


                } else {
                    fprintf(output_file, "Translating. Translation for VPN %d not found in page table\n", vpn);
                    return -1;
                }
            }

            int pfn = (tlb_index != -1 && TLB[tlb_index].valid) ? TLB[tlb_index].PFN : page_tables[pid].pte[vpn].PFN;
            int physical_address = (pfn << offset) | (virtual_address & ((1 << offset) - 1));
            memory[physical_address] = value;
            //print PID
            fprintf(output_file, "Current PID: %d. ", pid);
            if(!immediate_flag){
                fprintf(output_file, "Stored value of register %s (%d) into location %s\n",tokens[2], value, tokens[1]);
            }
            else{
                fprintf(output_file, "Stored immediate %d into location %s\n", value, tokens[1]);
            }
            
        }
        else if(strcmp(tokens[0], "add") == 0){
            //print PID
            fprintf(output_file, "Current PID: %d. ", pid);
            int prev_r1 = page_tables[pid].r1;
            page_tables[pid].r1 = page_tables[pid].r1 + page_tables[pid].r2;
            fprintf(output_file, "Added contents of registers r1 (%d) and r2 (%d). Result: %d\n",prev_r1, page_tables[pid].r2, page_tables[pid].r1);
        }
        else if(strcmp(tokens[0], "map") == 0){
            //print PID
            fprintf(output_file, "Current PID: %d. ", pid);

            int index = atoi(tokens[1]);
            int pfn = atoi(tokens[2]);
            page_tables[pid].pte[index].valid = 1;
            page_tables[pid].pte[index].PFN = pfn;
            int insert_idx = oldest_TLB();
            TLB[insert_idx].valid = 1;
            TLB[insert_idx].VPN = index;
            TLB[insert_idx].PFN = pfn;
            TLB[insert_idx].PID = pid;
            TLB[insert_idx].timestamp = timestamp;

            fprintf(output_file, "Mapped virtual page number %d to physical frame number %d\n", index , page_tables[pid].pte[index].PFN);
        }
        else if(strcmp(tokens[0], "unmap") == 0){
            //print PID
            fprintf(output_file, "Current PID: %d. ", pid);

            int unmapped_vpn = atoi(tokens[1]);
            page_tables[pid].pte[unmapped_vpn].valid = 0;
            //Check TLB
            int unmapped_tlb = lookup_TLB(unmapped_vpn, pid);
            if(unmapped_tlb != -1){
                TLB[unmapped_tlb].valid = 0;
            }
            fprintf(output_file, "Unmapped virtual page number %d\n", unmapped_vpn);

        }
        // Deallocate tokens 
        for (int i = 0; tokens[i] != NULL; i++)
            free(tokens[i]);
        free(tokens);
        //Increment timestamp for our TLB entry replacement strategies
        timestamp += 1;
    }

    // Close input and output files
    fclose(input_file);
    fclose(output_file);

    return 0;
}