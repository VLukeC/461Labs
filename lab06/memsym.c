#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdarg.h>

#define TRUE 1
#define FALSE 0

// Output file
FILE* output_file = NULL;

// TLB replacement strategy (FIFO or LRU)
char* strategy = NULL;

int offset_bits = -1;
int PFN_bits = 0;
int VPN_bits = 0;
int current_pid = 0;          // PID currently executing
uint32_t timestamp = 0;       // Global timestamp, incremented once per trace instruction
size_t num_pages = 0;         // 2^VPN_bits
size_t memory_size = 0;       // 2^(OFF+PFN)

typedef struct {
    unsigned int valid;
    unsigned int PFN;
} PTE;

typedef struct {
    unsigned int valid;
    unsigned int PFN;
    unsigned int VPN;
    unsigned int PID;
    uint32_t timestamp;
} TLB_entry;

typedef struct {
    int PID;
    PTE* pte;         // dynamically sized page table (num_pages entries)
    uint32_t r1;
    uint32_t r2;
} page_table;

TLB_entry TLB[8];
page_table page_tables[4];
uint32_t* memory = NULL;

char** tokenize_input(char* input) {
    char** tokens = NULL;
    char* token = strtok(input, " ");
    int num_tokens = 0;

    while (token != NULL) {
        num_tokens++;
        tokens = (char**)realloc(tokens, num_tokens * sizeof(char*));
        tokens[num_tokens - 1] = (char*)malloc(strlen(token) + 1);
        strcpy(tokens[num_tokens - 1], token);
        token = strtok(NULL, " ");
    }

    num_tokens++;
    tokens = (char**)realloc(tokens, num_tokens * sizeof(char*));
    tokens[num_tokens - 1] = NULL;

    return tokens;
}

static void log_msg(int pid, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(output_file, "Current PID: %d. ", pid);
    vfprintf(output_file, fmt, args);
    fprintf(output_file, "\n");
    va_end(args);
}

int oldest_TLB() {
    int best = -1;

    // First, return the index of the first invalid entry (free slot), if any
    for (int i = 0; i < 8; i++) {
        if (!TLB[i].valid) {
            return i;
        }
    }

    // All valid: choose the one with the smallest timestamp
    uint32_t min_time = 0;
    for (int i = 0; i < 8; i++) {
        if (best == -1 || TLB[i].timestamp < min_time) {
            best = i;
            min_time = TLB[i].timestamp;
        }
    }
    return best;
}

int lookup_TLB(int vpn, int pid) {
    for (int i = 0; i < 8; i++) {
        if (TLB[i].valid && TLB[i].VPN == (unsigned int)vpn && TLB[i].PID == (unsigned int)pid) {
            // LRU: update timestamp on hit
            if (strategy != NULL && strcmp(strategy, "LRU") == 0) {
                TLB[i].timestamp = timestamp;
            }
            return i;
        }
    }
    return -1; // miss
}

void tlb_insert_or_update(int vpn, int pfn, int pid) {
    // First, see if an entry for this (pid,vpn) already exists
    for (int i = 0; i < 8; i++) {
        if (TLB[i].valid && TLB[i].VPN == (unsigned int)vpn && TLB[i].PID == (unsigned int)pid) {
            TLB[i].PFN = (unsigned int)pfn;
            TLB[i].timestamp = timestamp;
            return;
        }
    }

    int idx = oldest_TLB();
    TLB[idx].valid = 1;
    TLB[idx].VPN = (unsigned int)vpn;
    TLB[idx].PFN = (unsigned int)pfn;
    TLB[idx].PID = (unsigned int)pid;
    TLB[idx].timestamp = timestamp;
}

void tlb_invalidate_vpn_pid(int vpn, int pid) {
    for (int i = 0; i < 8; i++) {
        if (TLB[i].valid && TLB[i].VPN == (unsigned int)vpn && TLB[i].PID == (unsigned int)pid) {
            TLB[i].valid = 0;
            return;
        }
    }
}

int translate_address(int virtual_address, int* physical_address_out) {
    int vpn = virtual_address >> offset_bits;

    int tlb_index = lookup_TLB(vpn, current_pid);

    int pfn;
    if (tlb_index != -1 && TLB[tlb_index].valid) {
        pfn = TLB[tlb_index].PFN;
        log_msg(current_pid, "Translating. Lookup for VPN %d hit in TLB entry %d. PFN is %d", vpn, tlb_index, pfn);
    } else {
        log_msg(current_pid, "Translating. Lookup for VPN %d caused a TLB miss", vpn);

        if (vpn < 0 || (size_t)vpn >= num_pages) {
            log_msg(current_pid, "Translating. Translation for VPN %d not found in page table", vpn);
            return -1;
        }

        if (page_tables[current_pid].pte[vpn].valid) {
            pfn = page_tables[current_pid].pte[vpn].PFN;
            log_msg(current_pid, "Translating. Successfully mapped VPN %d to PFN %d", vpn, pfn);
            tlb_insert_or_update(vpn, pfn, current_pid);
        } else {
            log_msg(current_pid, "Translating. Translation for VPN %d not found in page table", vpn);
            return -1;
        }
    }

    int offset_mask = (1 << offset_bits) - 1;
    int physical_address = (pfn << offset_bits) | (virtual_address & offset_mask);
    *physical_address_out = physical_address;
    return 0;
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

    // Initialize page tables and TLB/memory pointers
    for (int i = 0; i < 4; i++) {
        page_tables[i].PID = i;
        page_tables[i].pte = NULL;
        page_tables[i].r1 = 0;
        page_tables[i].r2 = 0;
    }
    for (int i = 0; i < 8; i++) {
        TLB[i].valid = 0;
        TLB[i].PFN = 0;
        TLB[i].VPN = 0;
        TLB[i].PID = 0;
        TLB[i].timestamp = 0;
    }

    while (fgets(buffer, sizeof(buffer), input_file) != NULL) {
        // Strip trailing newline if present
        size_t len = strlen(buffer);
        if (len > 0 && (buffer[len-1] == '\n' || buffer[len-1] == '\r')) {
            buffer[len-1] = '\0';
            len--;
            if (len > 0 && buffer[len-1] == '\r') {
                buffer[len-1] = '\0';
            }
        }

        // Skip comments
        if (buffer[0] == '%') {
            continue;
        }

        // Increment timestamp for each (non-comment) trace instruction
        timestamp += 1;

        char** tokens = tokenize_input(buffer);
        if (tokens[0] == NULL) {
            // Empty line
            for (int i = 0; tokens[i] != NULL; i++) free(tokens[i]);
            free(tokens);
            continue;
        }

        // Check define usage
        if (offset_bits == -1 && strcmp(tokens[0], "define") != 0) {
            log_msg(current_pid, "Error: attempt to execute instruction before define");
            for (int i = 0; tokens[i] != NULL; i++) free(tokens[i]);
            free(tokens);
            break;
        }

        if (strcmp(tokens[0], "define") == 0) {
            if (offset_bits != -1) {
                log_msg(current_pid, "Error: multiple calls to define in the same trace");
                for (int i = 0; tokens[i] != NULL; i++) free(tokens[i]);
                free(tokens);
                break;
            }

            offset_bits = atoi(tokens[1]);
            PFN_bits = atoi(tokens[2]);
            VPN_bits = atoi(tokens[3]);

            num_pages = ((size_t)1) << VPN_bits;
            memory_size = ((size_t)1) << (offset_bits + PFN_bits);

            // Allocate physical memory
            memory = (uint32_t*)malloc(memory_size * sizeof(uint32_t));
            if (!memory) {
                for (int i = 0; tokens[i] != NULL; i++) free(tokens[i]);
                free(tokens);
                fclose(input_file);
                fclose(output_file);
                return 1;
            }
            memset(memory, 0, memory_size * sizeof(uint32_t));

            // Allocate per-process page tables
            for (int p = 0; p < 4; p++) {
                page_tables[p].PID = p;
                page_tables[p].pte = (PTE*)calloc(num_pages, sizeof(PTE));
            }

            log_msg(current_pid, "Memory instantiation complete. OFF bits: %d. PFN bits: %d. VPN bits: %d",
                    offset_bits, PFN_bits, VPN_bits);
        }
        else if (strcmp(tokens[0], "ctxswitch") == 0) {
            int new_pid = atoi(tokens[1]);
            int prev_pid = current_pid;
            if (new_pid < 0 || new_pid > 3) {
                log_msg(prev_pid, "Invalid context switch to process %d", new_pid);
                for (int i = 0; tokens[i] != NULL; i++) free(tokens[i]);
                free(tokens);
                break;
            }
            current_pid = new_pid;
            log_msg(current_pid, "Switched execution context to process: %d", current_pid);
        }
        else if (strcmp(tokens[0], "load") == 0) {
            const char* dst = tokens[1];
            const char* src_str = tokens[2];

            // Immediate
            if (src_str[0] == '#') {
                int value = atoi(src_str + 1);
                if (strcmp(dst, "r1") == 0) {
                    page_tables[current_pid].r1 = (uint32_t)value;
                } else if (strcmp(dst, "r2") == 0) {
                    page_tables[current_pid].r2 = (uint32_t)value;
                } else {
                    log_msg(current_pid, "Error: invalid register operand %s", dst);
                    for (int i = 0; tokens[i] != NULL; i++) free(tokens[i]);
                    free(tokens);
                    break;
                }
                log_msg(current_pid, "Loaded immediate %d into register %s", value, dst);
            } else {
                // Load from memory (virtual address)
                int virtual_address = atoi(src_str);
                int physical_address;
                if (translate_address(virtual_address, &physical_address) != 0) {
                    for (int i = 0; tokens[i] != NULL; i++) free(tokens[i]);
                    free(tokens);
                    break;
                }
                uint32_t value = memory[physical_address];

                if (strcmp(dst, "r1") == 0) {
                    page_tables[current_pid].r1 = value;
                } else if (strcmp(dst, "r2") == 0) {
                    page_tables[current_pid].r2 = value;
                } else {
                    log_msg(current_pid, "Error: invalid register operand %s", dst);
                    for (int i = 0; tokens[i] != NULL; i++) free(tokens[i]);
                    free(tokens);
                    break;
                }

                log_msg(current_pid, "Loaded value of location %s (%u) into register %s", src_str, value, dst);
            }
        }
        else if (strcmp(tokens[0], "store") == 0) {
            const char* dst_str = tokens[1];
            const char* src_str = tokens[2];

            int value;
            int from_register = 0;
            const char* regname = NULL;

            if (strcmp(src_str, "r1") == 0) {
                value = (int)page_tables[current_pid].r1;
                from_register = 1;
                regname = "r1";
            } else if (strcmp(src_str, "r2") == 0) {
                value = (int)page_tables[current_pid].r2;
                from_register = 1;
                regname = "r2";
            } else if (src_str[0] == '#') {
                value = atoi(src_str + 1);
                from_register = 0;
            } else {
                log_msg(current_pid, "Error: invalid register operand %s", src_str);
                for (int i = 0; tokens[i] != NULL; i++) free(tokens[i]);
                free(tokens);
                break;
            }

            int virtual_address = atoi(dst_str);
            int physical_address;
            if (translate_address(virtual_address, &physical_address) != 0) {
                for (int i = 0; tokens[i] != NULL; i++) free(tokens[i]);
                free(tokens);
                break;
            }

            memory[physical_address] = (uint32_t)value;

            if (from_register) {
                log_msg(current_pid, "Stored value of register %s (%d) into location %s", regname, value, dst_str);
            } else {
                log_msg(current_pid, "Stored immediate %d into location %s", value, dst_str);
            }
        }
        else if (strcmp(tokens[0], "add") == 0) {
            uint32_t before_r1 = page_tables[current_pid].r1;
            uint32_t before_r2 = page_tables[current_pid].r2;
            page_tables[current_pid].r1 = before_r1 + before_r2;
            log_msg(current_pid,
                    "Added contents of registers r1 (%u) and r2 (%u). Result: %u",
                    before_r1, before_r2, page_tables[current_pid].r1);
        }
        else if (strcmp(tokens[0], "map") == 0) {
            int vpn = atoi(tokens[1]);
            int pfn = atoi(tokens[2]);

            if (vpn >= 0 && (size_t)vpn < num_pages) {
                page_tables[current_pid].pte[vpn].valid = 1;
                page_tables[current_pid].pte[vpn].PFN = (unsigned int)pfn;
            }
            tlb_insert_or_update(vpn, pfn, current_pid);

            log_msg(current_pid, "Mapped virtual page number %d to physical frame number %d", vpn, pfn);
        }
        else if (strcmp(tokens[0], "unmap") == 0) {
            int vpn = atoi(tokens[1]);

            if (vpn >= 0 && (size_t)vpn < num_pages) {
                page_tables[current_pid].pte[vpn].valid = 0;
                page_tables[current_pid].pte[vpn].PFN = 0;
            }
            tlb_invalidate_vpn_pid(vpn, current_pid);

            log_msg(current_pid, "Unmapped virtual page number %d", vpn);
        }
        else if (strcmp(tokens[0], "pinspect") == 0) {
            int vpn = atoi(tokens[1]);
            uint32_t pfn = 0;
            unsigned int valid = 0;

            if (vpn >= 0 && (size_t)vpn < num_pages) {
                pfn = page_tables[current_pid].pte[vpn].PFN;
                valid = page_tables[current_pid].pte[vpn].valid;
            }
            log_msg(current_pid,
                    "Inspected page table entry %d. Physical frame number: %u. Valid: %u",
                    vpn, pfn, valid);
        }
        else if (strcmp(tokens[0], "tinspect") == 0) {
            int idx = atoi(tokens[1]);
            if (idx < 0 || idx >= 8) {
                log_msg(current_pid,
                        "Inspected TLB entry %d. VPN: %d. PFN: %d. Valid: %d. PID: %d. Timestamp: %d",
                        idx, 0, 0, 0, 0, 0);
            } else {
                TLB_entry e = TLB[idx];
                log_msg(current_pid,
                        "Inspected TLB entry %d. VPN: %u. PFN: %u. Valid: %u. PID: %u. Timestamp: %u",
                        idx, e.VPN, e.PFN, e.valid, e.PID, e.timestamp);
            }
        }
        else if (strcmp(tokens[0], "linspect") == 0) {
            int pl = atoi(tokens[1]);
            uint32_t value = 0;
            if (pl >= 0 && (size_t)pl < memory_size && memory != NULL) {
                value = memory[pl];
            }
            log_msg(current_pid, "Inspected physical location %d. Value: %u", pl, value);
        }
        else if (strcmp(tokens[0], "rinspect") == 0) {
            const char* reg = tokens[1];
            if (strcmp(reg, "r1") == 0) {
                log_msg(current_pid, "Inspected register r1. Content: %u", page_tables[current_pid].r1);
            } else if (strcmp(reg, "r2") == 0) {
                log_msg(current_pid, "Inspected register r2. Content: %u", page_tables[current_pid].r2);
            } else {
                log_msg(current_pid, "Error: invalid register operand %s", reg);
                for (int i = 0; tokens[i] != NULL; i++) free(tokens[i]);
                free(tokens);
                break;
            }
        }
        // Any other token[0] (including "...") is ignored but still advances the timestamp

        // Deallocate tokens
        for (int i = 0; tokens[i] != NULL; i++)
            free(tokens[i]);
        free(tokens);
    }

    // Close input and output files
    fclose(input_file);
    fclose(output_file);

    // Free allocated memory
    if (memory) free(memory);
    for (int p = 0; p < 4; p++) {
        if (page_tables[p].pte) {
            free(page_tables[p].pte);
        }
    }

    return 0;
}
