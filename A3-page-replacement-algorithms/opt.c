#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include "pagetable.h"
#include "sim.h"

#define NUMPAGES (PTRS_PER_PGDIR*PTRS_PER_PGTBL)

//extern int memsize;
extern int debug;
extern struct frame *coremap;

/* A node entry in a linked list. Each one represents a memory access gotten
from the trace file.*/
typedef struct node {

	unsigned long number; // Contains the line number in the trace file of this
												//  memory access (eg number = 0 for first mem access)
												// NOTE: for large enough trace files, number could
												//			overflow.

	struct node *next_same_vaddr; // Pointer to the next node with the same page
																// (i.e. same vaddr, perhaps with different offset)
} Node;

/* A linked list structure. Contains pointers to the front and end nodes.
 * Early memory accesses go the front of the list, later ones to the back. */
typedef struct list {
	Node *front;
	Node *end;
} List;

// ============   Data structures   ============

// A list for every page. For a given list array[i], each node in that list
// represents a memory access at location vaddr, with i = vaddr >> PAGE_SHIFT.
List *array[NUMPAGES];

// Represents whether the list at index i in array is set.
// i.e. array[i] is set iff set[i] = 1.
char set[NUMPAGES] = {0}; // Should be zero by default, but just in case.

//==============================================

/*
/* ANNOTATION 9: well done */
 * A helpful debug method.
/* END ANNOTATION 9 */
 */
void printMem(){

	for(int i = 0; i < memsize; i++) {
		char *mem_ptr = &physmem[i*SIMPAGESIZE];
		addr_t *vaddr_ptr = (addr_t *)(mem_ptr + sizeof(int));
		addr_t vaddr_index = (*vaddr_ptr) >> PAGE_SHIFT;

		// Empty list, meaning there will be no calls to this page in the future.
		if(!set[vaddr_index]) {
			printf("[%d]: vaddr = %lu, is empty\n", i, vaddr_index);

		} else{
			printf("[%d]: vaddr = %lu, front = %lu \n",i , vaddr_index, array[vaddr_index]->front->number);
		}

	}
	printf("\n");

}

/*
 * Helper for printArray. Debug method.
 */
void printList(int index){
	Node * curr = array[index]->front;

	printf("%d : ", index);
	while (curr != NULL){
		printf("%lu -> ", curr->number);
		curr = curr->next_same_vaddr;
	}
	printf("\n");
}

/*
 * Another helpful debug method.
 */
void printArray(){
	for (int i = 0; i < NUMPAGES; i++){
		if (set[i]){
			printList(i);
		}
	}
}

/*
 * Insert curr into the list[vaddr_index]. curr represents the line number of
 * this memory access.
 */
void insert(unsigned vaddr_index, unsigned long curr){

	// Initialize the list for this vaddr_index if it hasn't been initialized yet.
	if (!set[vaddr_index]){
		set[vaddr_index] = 1;

/* ANNOTATION 5: not error check'd ( code -2 ) */
		array[vaddr_index] = (List *)malloc(sizeof(List));
/* END ANNOTATION 5 */
		array[vaddr_index]->front = NULL;
		array[vaddr_index]->end = NULL;
	}

	List *entry = array[vaddr_index];

	// Create a node for this line of the trace file.
	Node *temp = (Node *)malloc(sizeof(Node));
	temp->number = curr;
	temp->next_same_vaddr = NULL;

	// Add to the end of the list.
	if (entry->front == NULL){
		entry->front = temp;
		entry->end = temp;
	} else {
		entry->end->next_same_vaddr = temp;
		entry->end = temp;
	}

	// This (likely) won't happen, unless the trace file does 4 billion + memory
	//  accesses.
	if (++curr == 0){
/* ANNOTATION 6: probably want to terminate here - ignoring the overflow seems unsafe */
		fprintf(stderr, "Overflow!\n");
/* END ANNOTATION 6 */
		// As is, evitions wouldn't work correctly if there's overflow.
		//  Could handle the case here, but it is unlikely so I didn't bother.
	}

}

/* Page to evict is chosen using the optimal (aka MIN) algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */
int opt_evict() {

	int frame = 0;
	unsigned long latest = 0;

	for(int i = 0; i < memsize; i++) {
		char *mem_ptr = &physmem[i*SIMPAGESIZE];
		addr_t *vaddr_ptr = (addr_t *)(mem_ptr + sizeof(int));
		addr_t vaddr_index = (*vaddr_ptr) >> PAGE_SHIFT;

		// Found a page has an empty list, meaning there will be no calls to this
		//  page in the future.
		if(!set[vaddr_index]) {
			frame = i;
			return frame;
		}
		// Non empty list. Find which page in coremap has the largest number value.
		// That will be the page that won't be used for the longest period of time.
		if (array[vaddr_index]->front->number > latest){
			latest = array[vaddr_index]->front->number;
			frame = i;
		}

	}
	return frame;
}

/* This function is called on each access to a page to update any information
 * needed by the opt algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void opt_ref(pgtbl_entry_t *p) {

	// From the pte, gets vaddr (which is stored in "physical memory").
	// Then get the List entry from the vaddr.
/* ANNOTATION 7: could hide this boilerplate */
	int frameNumber = p->frame >> PAGE_SHIFT;
	char *mem_ptr = &physmem[frameNumber*SIMPAGESIZE];
	addr_t *vaddr_ptr = (addr_t *)(mem_ptr + sizeof(int));
	addr_t vaddr_index = (*vaddr_ptr) >> PAGE_SHIFT;
/* END ANNOTATION 7 */
	List *entry = array[vaddr_index];

/* ANNOTATION 8: use dedicated remove operation */
	// Remove the first element from the list.
	Node *temp = entry->front;
	entry->front = entry->front->next_same_vaddr;
	free(temp);

	// Free the list if it's empty.
	if (entry->front == NULL){
		free(entry);
		set[vaddr_index] = 0;
	}
	return;
/* END ANNOTATION 8 */
}

/* Initializes any data structures needed for this
 * replacement algorithm.
 */
void opt_init() {

	// The current line number of the file. eviciton algorithm may not work if
	//  there are too many lines (unsigned long can go up to four billion).
	unsigned long curr = 0;

	FILE *infp = NULL;
	if(tracefile != NULL) {
		if((infp = fopen(tracefile, "r")) == NULL) {
/* ANNOTATION 2: consider annotating diagnostics with standard predefined macros such as {double underscore}FILE{double underscore}, {double underscore}LINE{double underscore} etc. */
			perror("Error opening tracefile:");
/* ANNOTATION 3: ~ man 3 exit.  C standard provides portable macros to signal exit status - EXIT\_{SUCCESS,FAILURE} 
 */
/* END ANNOTATION 2 */
			exit(1);
/* END ANNOTATION 3 */
		}
	}

	char buf[MAXLINE];
	addr_t vaddr = 0;
	char type;

	while(fgets(buf, MAXLINE, infp) != NULL) {
		if(buf[0] != '=') {
			sscanf(buf, "%c %lx", &type, &vaddr);

			// Remove the offset into the pagetable.
			unsigned page = vaddr >> PAGE_SHIFT;

			// Insert curr to this pages list.
/* ANNOTATION 4: good abstraction */
			insert(page, curr);
/* END ANNOTATION 4 */
			curr++;

/* ANNOTATION 1: unnecessary branch */
		} else {
			continue;
		}
/* END ANNOTATION 1 */
	}
	fclose(infp);

}
