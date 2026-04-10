#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

#define FRAMES 256
#define TLB_SIZE 16
#define PHYS_MEM 65536

uint32_t l_masked_address; // 16 bit addr
uint32_t l_offset; // 8 bit offset
uint32_t page; // 8 bit (logical memory) page number
unsigned int frame; // frame number (index in physical memory)
int pg_faults = 0; // track number of page faults
int tlb_hits = 0; // track number of tlb hits
int q = 0; // pointer to next tlb entry to write to in case of fault

signed char physical_memory[FRAMES][FRAMES]; // 256 frames of 256 bytes signifying physical memory
int next_free_frame = 0; // track which frame to use next

struct tlb_entry
{
    bool valid_bit;
    unsigned int page_num;
    unsigned int frame_num;
};

struct page_t_entry
{
    unsigned int frame_num;
    bool valid_bit;
};

struct tlb_entry tlb[TLB_SIZE];
struct page_t_entry page_table[FRAMES];

void handle_page_fault(int page_number, FILE *backing_store) {
    // Calculate offset: page_number * 256 bytes
    long offset = (long)page_number * FRAMES;

    if (fseek(backing_store, offset, SEEK_SET) != 0) { // reads from spot in bin, handles error finding
        perror("Error seeking");
        return;
    }

    // Read into the next available physical frame - 1st main doesnt't need an algo, change this for main2.c and main3.c
    int target_frame = next_free_frame % FRAMES;

    if (fread(physical_memory[next_free_frame], sizeof(signed char), FRAMES, backing_store) != FRAMES) { // reads info into physical memory buffer, handles error reading
        perror("Error reading");
        return;
    }

    // update Page Table
    page_table[page_number].frame_num = next_free_frame;
    page_table[page_number].valid_bit = true;

    next_free_frame++;
}

// find logical addr
int addr_calc(uint32_t page_num, FILE *backing_store)
{
    if (page_table[page_num].valid_bit) { // checks if location in table is ok to read from
        frame = page_table[page_num].frame_num;
    } else {
        handle_page_fault(page_num, backing_store);
        // NOW update the global frame variable so main() can see it
        frame = page_table[page_num].frame_num;
    }
    return 0;
}

int div_addr(unsigned int l_addr) // mask address read in from file into partitions
{
    l_masked_address = l_addr & 0xFFFF; // masks 16 most bits
    l_offset = l_masked_address & 0xFF; // find offset, 8 bits
    page = (l_masked_address >> 8) & 0xFF; // find page number, leftover bits shifted to the front
    return 0;
}

void tlb_fifo_update(unsigned int page_num, unsigned int frame_num) { // update the fifo table, called when tlb miss occurs
    tlb[q].page_num = page_num;
    tlb[q].frame_num = frame_num;
    tlb[q].valid_bit = true;

    q = (q + 1) % TLB_SIZE; // increment pointer which iterates through tlb to implement queue
}

int access_memory(unsigned int page_num, FILE *backing_store) { // check tlb; if tlb fault, check page table; if page fault, check bin
    bool tlb_hit = false;
    unsigned int target_frame;

    // Search TLB
    for (int i = 0; i < TLB_SIZE; i++) {
        if (tlb[i].valid_bit && tlb[i].page_num == page_num) { // if entry found and the valid bit is true
            target_frame = tlb[i].frame_num; // target_frame is = to the frame it found
            tlb_hit = true; // note TLB hit
            tlb_hits++;
            break;
        }
    }

    if (!tlb_hit) { // if not hit after iterating through tlb
        if (page_table[page_num].valid_bit) { // if valid bit is true (page hit)
            target_frame = page_table[page_num].frame_num; // target_frame found in page table
        } else { // page fault
            handle_page_fault(page_num, backing_store); // load page into memory
            target_frame = page_table[page_num].frame_num; // target frame found in bin
            pg_faults++;
        }

        tlb_fifo_update(page_num, target_frame); // uptates tlb
    }

    return target_frame;
}

int main ()
{
    unsigned int line;

    FILE* fp = fopen("addresses.txt", "rt");
    if (fp == NULL) {
        perror("Error opening addresses.txt");
        return 1;
    }
    FILE *backing_store = fopen("BACKING_STORE.bin", "rb");
     if (backing_store == NULL) {
        perror("Error opening bin");
        return 1;
    }

    FILE* out1 = fopen("out1.txt", "wt");
    if (out1 == NULL) {
        perror("Error creating out1.txt");
        return 1;
    }
    FILE* out2 = fopen("out2.txt", "wt");
    if (out1 == NULL) {
        perror("Error creating out2.txt");
        return 1;
    }
    FILE* out3 = fopen("out3.txt", "wt");
    if (out3 == NULL) {
        perror("Error creating out3.txt");
        return 1;
    }

    // scan every line in the file
    while(fscanf(fp, "%u", &line) == 1){ // while there are still addresses to be scanned in
        div_addr(line);
        
        // look in TLB for address
        unsigned int current_frame = access_memory(page, backing_store);
        // translate frame number into address by adding offset
        int phys_addr = (current_frame * FRAMES) + l_offset;
        // shows value stored at location in bin
        signed char value = physical_memory[current_frame][l_offset];

        // printf("Virtual address: %u Physical address: %d Value: %d\n", line, phys_addr, value); <- print statement for error checking :]
        fprintf(out1, "%d\n", line);
        fprintf(out2, "%d\n", phys_addr);
        fprintf(out3, "%d\n", value);
    }

    fclose(fp);
    fclose(out1);
    fclose(out2);
    fclose(out3);
    return 0;
}