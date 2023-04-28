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

// Start represents the least recently used page. End represents most recently used.
Node *start;
Node *end;

/* Page to evict is chosen using the accurate LRU algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */

int lru_evict() {

	int retVal = 0;

	// Evict LRU
	if (start != NULL){
		Node *temp = start;
		start = start->next;
		retVal = temp->value->frame >> PAGE_SHIFT;
		free(temp);
	}

	return retVal;
}

/* This function is called on each access to a page to update any information
 * needed by the lru algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void lru_ref(pgtbl_entry_t *p) {

	Node *prev = NULL;
	Node *curr = start;
/* ANNOTATION 13: Your implementation has a higher time complexity and higher code complexity than the solution (-2 LRU). */

	while (curr != NULL){

		if (curr->value == p){

			// Put to back (mru).
			if (prev != NULL){
				// Special case : if at the front (lru).
				end->next = curr;
				prev->next = curr->next;
				curr->next = NULL;
				end = curr;
			}
			// Otherwise.
			else if (curr->next != NULL){
				start = curr->next;
				end->next = curr;
				curr->next = NULL;
				end = curr;
			}
			return;
		}
		prev = curr;
		curr = curr->next;
	}
/* END ANNOTATION 13 */

	// Page is not in linked list

	// Empty list case
	if (prev == NULL){
		start = (Node*)malloc(sizeof(Node));
		start->value = p;
		start->next = NULL;
		end = start;
	}

	// Non-empty list
	else{
		//prev is the last element
		prev->next = (Node*)malloc(sizeof(Node));
		prev->next->value = p;
		prev->next->next = NULL;
		end = prev->next;
	}

	return;
}


/* Initialize any data structures needed for this
 * replacement algorithm
 */
void lru_init() {
	start = NULL;
	end = NULL;
}
