#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#define SIZE 1024

int A[SIZE];

/*
 * Returns the element in the array that occurs > SIZE/2 times in the array.
 * Behaviour undefined if no such element exists.
 */
int maj(int f, int l){
  if (f == l)
    return A[f];
  int m = (f+l)/2;
  int x = maj(f,m);
  int c = 0;
  int i = f;
  while (i <= l){
    if (A[i] == x)
      c++;
    i++;
  }
  if (c > (l-f+1)/2)
    return x;
  else
    return maj(m+1, l);

}

int main(int argc, char ** argv) {

  for (int i = 0; i < SIZE; i++){
    A[i] = (int)random() % 3;
  }

  int result = maj(0, SIZE - 1);
  printf("%d\n", result);

	return 0;
}
