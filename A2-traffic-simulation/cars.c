#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "traffic.h"

extern struct intersection isection;

/**
 * Populate the car lists by parsing a file where each line has
 * the following structure:
 *
 * <id> <in_direction> <out_direction>
 *
 * Each car is added to the list that corresponds with
 * its in_direction
 *
 * Note: this also updates 'inc' on each of the lanes
 */
void parse_schedule(char *file_name) {
    int id;
    struct car *cur_car;
    struct lane *cur_lane;
    enum direction in_dir, out_dir;
    FILE *f = fopen(file_name, "r");

    /* parse file */
    while (fscanf(f, "%d %d %d", &id, (int*)&in_dir, (int*)&out_dir) == 3) {

        /* construct car */
        cur_car = malloc(sizeof(struct car));
        cur_car->id = id;
        cur_car->in_dir = in_dir;
        cur_car->out_dir = out_dir;

        /* append new car to head of corresponding list */
        cur_lane = &isection.lanes[in_dir];
        cur_car->next = cur_lane->in_cars;
        cur_lane->in_cars = cur_car;
        cur_lane->inc++;

    }

    fclose(f);

}

/**
 * TODO: Fill in this function
 *
 * Do all of the work required to prepare the intersection
 * before any cars start coming
 *
 */
void init_intersection() {
  int i;
  //intersection isection;
  for (i = 0; i < 4; i++){
    pthread_mutex_init(&isection.quad[i], NULL);
    pthread_mutex_init(&isection.lanes[i].lock, NULL);
    pthread_cond_init(&isection.lanes[i].producer_cv, NULL);
    pthread_cond_init(&isection.lanes[i].consumer_cv, NULL);

    isection.lanes[i].in_cars = NULL;
    isection.lanes[i].out_cars = NULL;

    isection.lanes[i].inc = 0;
    isection.lanes[i].passed = 0;
    isection.lanes[i].buffer = (struct car **)malloc(LANE_LENGTH * sizeof(struct car *));
    isection.lanes[i].head = 0;
    isection.lanes[i].tail = 0;
    isection.lanes[i].capacity = LANE_LENGTH;
    isection.lanes[i].in_buf = 0;
  }

}

/**
 * TODO: Fill in this function
 *
 * Populates the corresponding lane with cars as room becomes
 * available. Ensure to notify the cross thread as new cars are
 * added to the lane.
 *
 */
void *car_arrive(void *arg) {
    struct lane *l = arg;

    for (int i = 0; i < l->inc; i++){

      pthread_mutex_lock(&(l->lock));

      // Wait until the buffer is not full.
      if (l->in_buf == l->capacity){
        pthread_cond_wait(&(l->producer_cv), &(l->lock));
      }

      // Adds first car from l->in_cars to the buffer
      struct car *adding = l->in_cars;

      l->in_cars = adding->next;
      l->buffer[l->tail] = adding;

      // Sets the new tail and size.
      l->tail = (l->tail + 1) % l->capacity;
      l->in_buf++;

      // Notify crossing threads that the buffer is not empty.
      pthread_cond_signal(&(l->consumer_cv));
      pthread_mutex_unlock(&(l->lock));
    }

    return NULL;
}

/**
 * TODO: Fill in this function
 *
 * Moves cars from a single lane across the intersection. Cars
 * crossing the intersection must abide the rules of the road
 * and cross along the correct path. Ensure to notify the
 * arrival thread as room becomes available in the lane.
 *
 * Note: After crossing the intersection the car should be added
 * to the out_cars list of the lane that corresponds to the car's
 * out_dir. Do not free the cars!
 *
 *
 * Note: For testing purposes, each car which gets to cross the
 * intersection should print the following three numbers on a
 * new line, separated by spaces:
 *  - the car's 'in' direction, 'out' direction, and id.
 *
 * You may add other print statements, but in the end, please
 * make sure to clear any prints other than the one specified above,
 * before submitting your final code.
 */
void *car_cross(void *arg) {
    struct lane *l = arg;

    for (int i = 0; i < l->inc; i++){
        pthread_mutex_lock(&(l->lock));

      //Wait until the buffer is nonempty.
      if (l->in_buf == 0){
        pthread_cond_wait(&(l->consumer_cv), &(l->lock));
      }

      // Remove from buffer
      struct car *crossing = l->buffer[l->head];

      //Sets the new head and buffer size.
      l->head = (l->head + 1) % l->capacity;
      l->in_buf--;

      //Signals arriving cars that the lane buffer is not full.
      pthread_cond_signal(&(l->producer_cv));


      int *path = compute_path(crossing->in_dir, crossing->out_dir);

      for (int i = 0; i < 4; i++){

        //Grab the locks
        if (path[i] == 1){
  		      pthread_mutex_lock(&(isection.quad[i]));
        }

      }

      // Drive! Gets to other lane.
      // NOTE: No need for isection.lanes[crossing->out_dir].lock,
      //       have quad[crossing->out_dir]
      crossing->next = isection.lanes[crossing->out_dir].out_cars;
      isection.lanes[crossing->out_dir].out_cars = crossing;

      //As requested, the car's 'in' direction, 'out' direction, and id.
      fprintf(stdout, "%d %d %d\n", crossing->in_dir, crossing->out_dir, crossing->id);

      pthread_mutex_unlock(&(l->lock));

      for (int i = 3; i >= 0; i--){

        if (path[i] == 1){
          pthread_mutex_unlock(&(isection.quad[i]));
        }

      }

      free(path);

  }

  return NULL;
}

/**
 * TODO: Fill in this function
 *
 * Given a car's in_dir and out_dir return a sorted
 * list of the quadrants the car will pass through.
 *
 */
int *compute_path(enum direction in_dir, enum direction out_dir) {

    int *result;
    result = (int*)malloc(3*sizeof(int));

/* ANNOTATION 1: tip: use calloc in the future! :) */
    for (int i = 0; i < 4; i++)
      result[i] = 0;
/* END ANNOTATION 1 */

    int start = (int)in_dir;
    int out = (int)out_dir;

/* ANNOTATION 3: Don't rely on the numeric values of an enum.

-2 style */
/* ANNOTATION 2: it's tempting to come up with a general formula that works for dealing with all possible IN/OUT direction pairs, but it makes the code very unreadable and has a very high maintenance cost: if you were ever asked to change this code somehow, it would take time. Even worse: if some other developer had to change it... */
    result[(start+1)%4] = 1;

    if (out == (start + 2)%4){
    	result[(start+2)%4] = 1;
    }
    else if (out == (start + 3) %4){
	     result[(start+2)%4] = 1;
	      result[(start+3)%4] = 1;
    }
    else if (out == start){
      result[(start+2)%4] = 1;
	    result[(start+3)%4] = 1;
	    result[start%4] = 1;
    }
/* END ANNOTATION 3 */
/* END ANNOTATION 2 */

    return result;
}
