#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

// LRU-based page replacement with 128 physical frames
#define FRAMES   128   // physical frames will adjust as needed for data
#define SIZE     256   // page / frame size in bytes
#define TLB_SIZE 16    // number of TLB entries
#define PAGES    256   // number of virtual pages

uint32_t l_masked_address; // masked logical address
uint32_t l_offset;         // page offset
uint32_t page;             // 8-bit page number
unsigned int frame;        // resolved physical frame number

int pg_faults   = 0;  // total page faults
int tlb_hits    = 0;  // total TLB hits
int q           = 0;  // FIFO pointer for TLB replacement
int lru_counter = 0;  // global clock; incremented on every memory access

signed char physical_memory[FRAMES][SIZE]; // 128 frames x 256 bytes
int next_free_frame = 0;                   // next frame to allocate (while free frames remain)

// LRU tracking: per-frame last-used timestamp and page mapping
int frame_last_used[FRAMES]; // frame -> last access time
int frame_to_page[FRAMES];   // frame -> which virtual page is stored there

struct tlb_entry {
    bool         valid_bit;
    unsigned int page_num;
    unsigned int frame_num;
};

struct page_t_entry {
    unsigned int frame_num;
    bool         valid_bit;
};

struct tlb_entry    tlb[TLB_SIZE];
struct page_t_entry page_table[PAGES];

// Return the frame index whose last-used timestamp is smallest (LRU victim)
int find_lru_victim() {
    int min_time = frame_last_used[0];
    int victim   = 0;
    for (int i = 1; i < FRAMES; i++) {
        if (frame_last_used[i] < min_time) {
            min_time = frame_last_used[i];
            victim   = i;
        }
    }
    return victim;
}

// FIFO TLB replacement: overwrite the oldest TLB slot
void tlb_fifo_update(unsigned int page_num, unsigned int frame_num) {
    tlb[q].page_num  = page_num;
    tlb[q].frame_num = frame_num;
    tlb[q].valid_bit = true;
    q = (q + 1) % TLB_SIZE;
}

// Load a page from backing store; evict the least recently used frame when physical memory is full
void handle_page_fault(int page_number, FILE *backing_store) {
    int target_frame;

    if (next_free_frame < FRAMES) {
        // Free frame available; nothing needed
        target_frame = next_free_frame++;
    } else {
        // All frames occupied: evict the least recently used frame
        target_frame = find_lru_victim();

        int old_page = frame_to_page[target_frame];

        // Invalidate evicted page in the page table
        page_table[old_page].valid_bit = false;

        // Invalidate evicted page in the TLB
        for (int i = 0; i < TLB_SIZE; i++) {
            if (tlb[i].valid_bit && tlb[i].page_num == (unsigned int)old_page) {
                tlb[i].valid_bit = false;
            }
        }
    }

    // Seek to the page's location in the backing store and read it
    long offset = (long)page_number * SIZE;
    if (fseek(backing_store, offset, SEEK_SET) != 0) {
        perror("Error seeking");
        return;
    }
    if (fread(physical_memory[target_frame], sizeof(signed char), SIZE, backing_store) != SIZE) {
        perror("Error reading");
        return;
    }

    // Update page table, frame-to-page map, and LRU timestamp
    page_table[page_number].frame_num = target_frame;
    page_table[page_number].valid_bit = true;
    frame_to_page[target_frame]       = page_number;
    frame_last_used[target_frame]     = lru_counter++;
}

// Translate page number to frame number; do TLB/page table/page fault stuff
int access_memory(unsigned int page_num, FILE *backing_store) {
    bool         tlb_hit     = false;
    unsigned int target_frame;

    // Search TLB first
    for (int i = 0; i < TLB_SIZE; i++) {
        if (tlb[i].valid_bit && tlb[i].page_num == page_num) {
            target_frame = tlb[i].frame_num;
            tlb_hit      = true;
            tlb_hits++;
            break;
        }
    }

    if (!tlb_hit) {
        if (page_table[page_num].valid_bit) {
            // Page hit!
            target_frame = page_table[page_num].frame_num;
        } else {
            // Page fault! need to load from backing store
            handle_page_fault(page_num, backing_store);
            target_frame = page_table[page_num].frame_num;
            pg_faults++;
        }
        // Bring TLB up to date (FIFO replacement)
        tlb_fifo_update(page_num, target_frame);
    }

    // Update LRU timestamp for this frame on every access
    frame_last_used[target_frame] = lru_counter++;

    return target_frame;
}

// Split a logical address into masked address, offset, and page number
int div_addr(unsigned int l_addr) {
    l_masked_address = l_addr & 0xFFFF;
    l_offset         = l_masked_address & 0xFF;
    page             = (l_masked_address >> 8) & 0xFF;
    return 0;
}

int main() {
    unsigned int line;
    int total = 0;

    FILE *fp = fopen("addresses.txt", "rt");
    if (fp == NULL) { perror("Error opening addresses.txt"); return 1; }

    FILE *backing_store = fopen("BACKING_STORE.bin", "rb");
    if (backing_store == NULL) { perror("Error opening BACKING_STORE.bin"); return 1; }

    FILE *out1 = fopen("out1.txt", "wt");
    if (out1 == NULL) { perror("Error creating out1.txt"); return 1; }

    FILE *out2 = fopen("out2.txt", "wt");
    if (out2 == NULL) { perror("Error creating out2.txt"); return 1; }

    FILE *out3 = fopen("out3.txt", "wt");
    if (out3 == NULL) { perror("Error creating out3.txt"); return 1; }

    while (fscanf(fp, "%u", &line) == 1) {
        div_addr(line);

        unsigned int current_frame = access_memory(page, backing_store);
        int          phys_addr     = (current_frame * SIZE) + l_offset;
        signed char  value         = physical_memory[current_frame][l_offset];

        fprintf(out1, "%d\n", line);
        fprintf(out2, "%d\n", phys_addr);
        fprintf(out3, "%d\n", value);
        total++;
    }

    // Print statistics for tracking and logging
    printf("Page faults = %d / %d, %.2f\n", pg_faults, total, (float)pg_faults / total);
    printf("TLB hits = %d / %d, %.2f\n",    tlb_hits,  total, (float)tlb_hits  / total);

    fclose(fp);
    fclose(backing_store);
    fclose(out1);
    fclose(out2);
    fclose(out3);
    return 0;
}