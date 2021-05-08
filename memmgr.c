//
//  memmgr.c
//  memmgr
//

// AJ Albrecht

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define ARGC_ERROR 1
#define FILE_ERROR 2
#define BUFLEN 256
#define FRAME_SIZE  256

char main_mem[65536];
char main_mem_fifo[32768]; // 128 physical frames
int page_queue[128];
int qhead = 0, qtail = 0;
int tlb[16][2];
int current_tlb_entry = 0;
int page_table[256];
int current_frame = 0;
FILE* fstore;

// data for statistics
int pfc[5], pfc2[5]; // page fault count
int tlbh[5], tlbh2[5]; // tlb hit count
int count[5], count2[5]; // access count

// added
//int pfc_prev_hit = 0;  // new global variable — put at top of file, and initialize to 0
//int tlb_prev_hit = 0;                         // new global variable — put at top of file, and initialize to 0 (edited) 

#define PAGES 256
#define FRAMES_PART1 256
#define FRAMES_PART2 128

//-------------------------------------------------------------------
unsigned getpage(unsigned x) { return (0xff00 & x) >> 8; }

unsigned getoffset(unsigned x) { return (0xff & x); }

void getpage_offset(unsigned x) {
  unsigned  page   = getpage(x);
  unsigned  offset = getoffset(x);
  printf("x is: %u, page: %u, offset: %u, address: %u, paddress: %u\n", x, page, offset,
         (page << 8) | getoffset(x), page * 256 + offset);
}

int tlb_contains(unsigned x) {  // TODO:
  for (int i = 0; i < 16; i++) {
    if (tlb[i][0] == x) {
      return i;
    }
  }
  return -1;
}

void update_tlb(unsigned page) {  // TODO:
  tlb[current_tlb_entry][0] = page;
  tlb[current_tlb_entry][1] = page_table[page];
  current_tlb_entry++;
  current_tlb_entry = current_tlb_entry % 16;
}

unsigned getframe(FILE* fstore, unsigned logic_add, unsigned page, int *page_fault_count, int *tlb_hit_count) {              // TODO
  // tlb hit
  if (logic_add == 3674) {
    int x = 7;
  }
  int tlb_index = tlb_contains(page);
  if (tlb_index != -1) {
    (*tlb_hit_count)++;
    return tlb[tlb_index][1];
  }
  
  // tlb miss
  // if page table hit
  if (page_table[page] != -1) {
    update_tlb(page);
    return page_table[page];
  }
 
  // page table miss -> page fault
  // find page location in backing_store
  int offset = (logic_add / FRAME_SIZE) * FRAME_SIZE;
  fseek(fstore, offset, 0);

  // bring data into memory, update tlb and page table
  page_table[page] = current_frame++;
  current_frame = current_frame % 256;
  (*page_fault_count)++;
  fread(&main_mem[page_table[page] * FRAME_SIZE], sizeof(char), 256, fstore);
  update_tlb(page);
  return page_table[page];
}

int get_available_frame(unsigned page) {    // TODO
  // empty queue
  if (qhead == 0 && qtail == 0 && page_queue[qhead] == -1) {
    ++qtail;
    page_queue[qhead] = page;
    return qhead;
  }
  
  // queue not full
  if (page_queue[qtail] == -1) {
    page_queue[qtail] = page;
    int temp = qtail++;
    qtail = qtail % 128;
    return temp; 
  }
  
  // queue full
  if (qhead == qtail && page_queue[qtail] != -1) {
    page_queue[qhead] = page;
    int temp = qhead++;
    qhead = qhead % 128;
    qtail++;
    qtail = qtail % 128;
    return temp;
  }
  return -1;   // failed to find a value
}

unsigned getframe_fifo(FILE* fstore, unsigned logic_add, unsigned page, int *page_fault_count, int *tlb_hit_count) {
  // tlb hit
  int tlb_index = tlb_contains(page);
  if (tlb_index != -1 && page_queue[tlb[tlb_index][1]] == page) {
    (*tlb_hit_count)++;
    return tlb[tlb_index][1];
  }

  // tlb miss, page table hit
  if (page_table[page] != -1 && page_queue[page_table[page]] == page) {
    update_tlb(page);
    return page_table[page];
  } 
  int offset = (logic_add / FRAME_SIZE) * FRAME_SIZE;
  fseek(fstore, offset, 0);
  
  // page table miss -> page fault
  // find location in backing_store
  int temp = get_available_frame(page);
  page_table[page] = temp;
  (*page_fault_count)++;  

  // bring data into memory, update tlb and page table
  fread(&main_mem_fifo[temp * FRAME_SIZE], sizeof(char), 256, fstore);
  update_tlb(page);
  return page_table[page];
}

void open_files(FILE** fadd, FILE** fcorr, FILE** fstore) {
  *fadd = fopen("addresses.txt", "r");    // open file addresses.txt  (contains the logical addresses)
  if (*fadd ==  NULL) { fprintf(stderr, "Could not open file: 'addresses.txt'\n");  exit(FILE_ERROR);  }

  *fcorr = fopen("correct.txt", "r");     // contains the logical and physical address, and its value
  if (*fcorr ==  NULL) { fprintf(stderr, "Could not open file: 'correct.txt'\n");  exit(FILE_ERROR);  }

  *fstore = fopen("BACKING_STORE.bin", "rb");
  if (*fstore ==  NULL) { fprintf(stderr, "Could not open file: 'BACKING_STORE.bin'\n");  exit(FILE_ERROR);  }
}


void close_files(FILE* fadd, FILE* fcorr, FILE* fstore) {
  fclose(fcorr);
  fclose(fadd);
  fclose(fstore);
}


void simulate_pages_frames_equal(void) {
  char buf[BUFLEN];
  unsigned   page, offset, physical_add, frame = 0;
  unsigned   logic_add;                  // read from file address.txt
  unsigned   virt_add, phys_add, value;  // read from file correct.txt


  FILE *fadd, *fcorr, *fstore;
  open_files(&fadd, &fcorr, &fstore);
  
  // Initialize page table, tlb
  memset(page_table, -1, sizeof(page_table));
  for (int i = 0; i < 16;  ++i) { tlb[i][0] = -1; }
  
  int access_count = 0, page_fault_count = 0, tlb_hit_count = 0;
  current_frame = 0;
  current_tlb_entry = 0;
  
  printf("\n Starting nPages == nFrames memory simulation...\n");

  while (fscanf(fadd, "%d", &logic_add) != EOF) {
    ++access_count;

    fscanf(fcorr, "%s %s %d %s %s %d %s %d", buf, buf, &virt_add,
           buf, buf, &phys_add, buf, &value);  // read from file correct.txt

    // fscanf(fadd, "%d", &logic_add);  // read from file address.txt
    page   = getpage(  logic_add);
    offset = getoffset(logic_add);
    frame = getframe(fstore, logic_add, page, &page_fault_count, &tlb_hit_count);

    physical_add = frame * FRAME_SIZE + offset;
    int val = (int)(main_mem[physical_add]);

    // update tlb hit count and page fault count every 200 accesses
    if (access_count > 0 && access_count % 200 == 0){
      tlbh[(access_count / 200) - 1] = tlb_hit_count;
      pfc[(access_count / 200) - 1] = page_fault_count;
      count[(access_count / 200) - 1] = access_count;
    }
    
    printf("logical: %5u (page: %3u, offset: %3u) ---> physical: %5u -> value: %4d  ok\n", logic_add, page, offset, physical_add, val);
    if (access_count % 5 ==  0) { printf("\n"); }

    // revison two of profesor print code
   // printf("logical: %5u (page: %3u, offset: %3u) ---> physical: %5u -> value: %4d ok #hits: %d %d %s %s\n", logic_add, page, offset, physical_add, val, page_fault_count, tlb_hit_count,
   //   (page_fault_count > pfc_prev_hit ? "PgFAULT" : ""),
   //   (tlb_hit_count > tlb_prev_hit)  ? "   TLB HIT" : "");
    //if (access_count % 5 == 0) { printf("\n"); }
    //if (access_count % 50 == 0) { printf("========================================================================================================== %d\n\n", access_count); }
     //pfc_prev_hit = page_fault_count;                  // new global variable — put at top of file, and initialize to 0
     //tlb_prev_hit = tlb_hit_count;                         // new global variable — put at top of file, and initialize to 0 (edited) 

    assert(physical_add ==  phys_add);
    assert(value ==  val);
  }
  fclose(fcorr);
  fclose(fadd);
  fclose(fstore);
  
  printf("ALL logical ---> physical assertions PASSED!\n");
  printf("ALL read memory value assertions PASSED!\n");

  printf("\n\t\t... nPages == nFrames memory simulation done.\n");
}


void simulate_pages_frames_not_equal(void) {
  char buf[BUFLEN];
  unsigned   page, offset, physical_add, frame = 0;
  unsigned   logic_add;                  // read from file address.txt
  unsigned   virt_add, phys_add, value;  // read from file correct.txt

  printf("\n Starting nPages != nFrames memory simulation...\n");

  // Initialize page table, tlb, page queue
  memset(page_table, -1, sizeof(page_table));
  memset(page_queue, -1, sizeof(page_queue));
  for (int i = 0; i < 16;  ++i) { tlb[i][0] = -1; }
  
  int access_count = 0, page_fault_count = 0, tlb_hit_count = 0;
  qhead = 0; qtail = 0;

  FILE *fadd, *fcorr, *fstore;
  open_files(&fadd, &fcorr, &fstore);

  while (fscanf(fadd, "%d", &logic_add) != EOF) {
    ++access_count;

    fscanf(fcorr, "%s %s %d %s %s %d %s %d", buf, buf, &virt_add,
           buf, buf, &phys_add, buf, &value);  // read from file correct.txt

    // fscanf(fadd, "%d", &logic_add);  // read from file address.txt
    page   = getpage(  logic_add);
    offset = getoffset(logic_add);
    frame = getframe_fifo(fstore, logic_add, page, &page_fault_count, &tlb_hit_count);

    physical_add = frame * FRAME_SIZE + offset;
    int val = (int)(main_mem_fifo[physical_add]);

    // update tlb hit count and page fault count every 200 accesses
    if (access_count > 0 && access_count%200 == 0){
      tlbh2[(access_count / 200) - 1] = tlb_hit_count;
      pfc2[(access_count / 200) - 1] = page_fault_count;
      count2[(access_count / 200) - 1] = access_count;
    }
    
    printf("logical: %5u (page: %3u, offset: %3u) ---> physical: %5u -> value: %4d  ok\n", logic_add, page, offset, physical_add, val);
    if (access_count % 5 ==  0) { printf("\n"); }

    assert(value ==  val);
  }
  close_files(fadd, fcorr, fstore);

  printf("ALL read memory value assertions PASSED!\n");
  printf("\n\t\t... nPages != nFrames memory simulation done.\n");
}


int main(int argc, const char* argv[]) {
  // initialize statistics data
  for (int i = 0; i < 5; ++i){
    pfc[i] = pfc2[i] = tlbh[i]  = tlbh2[i] = count[i] = count2[i] = 0;
  }

  simulate_pages_frames_equal(); // 256 physical frames
  simulate_pages_frames_not_equal(); // 128 physical frames

  // Statistics
  printf("\n\nnPages == nFrames Statistics (256 frames):\n");
  printf("Access count   Tlb hit count   Page fault count   Tlb hit rate   Page fault rate\n");
  for (int i = 0; i < 5; ++i) {
    printf("%9d %12d %18d %18.4f %14.4f\n",
           count[i], tlbh[i], pfc[i],
           1.0f * tlbh[i] / count[i], 1.0f * pfc[i] / count[i]);
  }

  printf("\nnPages != nFrames Statistics (128 frames):\n");
  printf("Access count   Tlb hit count   Page fault count   Tlb hit rate   Page fault rate\n");
  for (int i = 0; i < 5; ++i) {
    printf("%9d %12d %18d %18.4f %14.4f\n",
           count2[i], tlbh2[i], pfc2[i],
           1.0f * tlbh2[i] / count2[i], 1.0f * pfc2[i] / count2[i]);
  }
  printf("\n\t\t...memory management simulation completed!\n");

  return 0;
}
