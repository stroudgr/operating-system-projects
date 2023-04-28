#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include "pagetable.h"


extern int memsize;

extern int debug;

extern struct frame *coremap;

typedef struct node {
	pgtbl_entry_t *value;
	struct node *next;
} Node;

// Front of FIFO queue.
Node *start;
//Node *end; not needed

/* Page to evict is chosen using the fifo algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */
int fifo_evict() {

	int retVal = 0;

	// Remove from front of queue.
	if (start != NULL){
		Node *temp = start;
		start = start->next;
		retVal = temp->value->frame >> PAGE_SHIFT;
		free(temp);
	}

	return retVal;
}

/* This function is called on each access to a page to update any information
 * needed by the fifo algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void fifo_ref(pgtbl_entry_t *p) {

	Node *prev = NULL;
	Node *curr = start;

	// Try to find page in queue.
/* ANNOTATION 10: Your implementation has a higher time complexity and code complexity than the solution (-1 FIFO). */
	while (curr != NULL){
		if (curr->value == p)
			return;

		prev = curr;
		curr = curr->next;
	}
/* END ANNOTATION 10 */

	// Not in linked list

	// Empty list? Add to back (and front)
	if (prev == NULL){
		start = (Node*)malloc(sizeof(Node));
		start->value = p;
		start->next = NULL;
	}

	// Non-empty list? Add to back.
	else{
		// Prev is the last element of the list
		prev->next = (Node*)malloc(sizeof(Node));
		prev->next->value = p;
		prev->next->next = NULL;
	}

	return;
}

/* Initialize any data structures needed for this
 * replacement algorithm
 */
void fifo_init() {
	start = NULL;
}
