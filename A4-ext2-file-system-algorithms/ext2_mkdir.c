#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <libgen.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "ext2.h"
#include "ext2_help.h"

unsigned char *disk;
struct ext2_group_desc *gd;
struct ext2_inode *table;
struct ext2_inode *root_entry;
unsigned char *inode_bitmap;

int mkdir2 (char *relative_path) {

  char *base = basename(relative_path);
  unsigned int parent_file_inode = getParentInode(root_entry, EXT2_ROOT_INO, relative_path+1);

  if (parent_file_inode == 0){
    exit(ENOENT);
  }

  struct ext2_inode *parent_file = table + parent_file_inode - 1;
  struct ext2_dir_entry *file = createDirectoryEntry(parent_file, parent_file_inode, base, EXT2_FT_DIR);

  file->inode = newInode(EXT2_FT_DIR, parent_file_inode);

  return 0;
}



// NOTE: Can ignore this function.
// Older version of the code. Not as clean as other version.
int mkdir(struct ext2_inode *curr_dir, unsigned int curr_dir_inode, char *relative_path){

  // Assert the curr_dir is actually a valid directory.
  if (!(curr_dir->i_mode & EXT2_S_IFDIR)) {
    exit(ENOENT);
  }

  int len = strlen(relative_path);
  char front[len]; char back[len];

  //
  //slash(relative_path, len, front, back);

  // That means curr_dir is the parent directory and relative_path has no slashes (name of a file)!
  if (!strcmp("", front)){

    // Places a new directory entry in the firectory for the file.
    // Helper function will also create the directory for me.
    //NOTE 'd' is proabably not right anymore ....
    createDirectoryEntry(curr_dir, curr_dir_inode, relative_path, 'd');
    exit(0);
  }

  // Otherwise, find the directory entry for the front of the path
  //
  struct ext2_dir_entry *subfolder_entry;
  subfolder_entry = getDirectoryEntry(curr_dir, curr_dir_inode, front);

  // Assert that it is a file that exists.
  if (subfolder_entry == NULL){
    exit(ENOENT);
  }

  // Recurse on the subfolder
  struct ext2_inode *subfolder = table + subfolder_entry->inode - 1;

  return mkdir(subfolder, subfolder_entry->inode, back);

}


int main(int argc, char **argv){

  if(argc != 3) {
      fprintf(stderr, "Usage: %s <image file name>\n", argv[0]);
      exit(1);
  }

  // See ext2_help.c
  open_disk(argv[1]);

  char *path = (char *)malloc((1 + strlen(argv[1]))*sizeof(char));
  validatePath(path, argv[2]);

  //mkdir(root_entry, EXT2_ROOT_INO, path + 1);
  mkdir2(path);
  return 0;
}
