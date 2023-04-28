#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <libgen.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "ext2.h"
#include "ext2_help.h"

#define NOBONUS 0

void rm(char *path){

  char *base = basename(path);
  unsigned int parent_file_inode = getParentInode(root_entry, EXT2_ROOT_INO, path+1);

  if (parent_file_inode == 0){
    exit(ENOENT);
  }

  struct ext2_inode *parent_file = table + parent_file_inode - 1;
  removeDirectoryEntry(parent_file, parent_file_inode, base, NOBONUS);

}


/* ANNOTATION 4: Failed to correctly update free block count and block bitset when removing a large file. Seems you forgot to account for the indirect block itself (-1 REMOVE). */

int main(int argc, char **argv){
/* END ANNOTATION 4 */

  if(argc != 3) {
      fprintf(stderr, "Usage: %s <image file>\n", argv[0]);
      exit(1);
  }

  // See ext2_help.c
  open_disk(argv[1]);

  char *path = (char *)malloc((1 + strlen(argv[2]))*sizeof(char));

  validatePath(path, argv[2]);

  rm(path);

  return 0;
}
