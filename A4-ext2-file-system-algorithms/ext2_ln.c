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

/**
 * Takes the file from filepath on the disk and creates a link at desk on the
 *  disk. link_type says whether it is a hard/soft link with 'h'/'l'.
 */
/* ANNOTATION 2: incorrect inode content for symbolic link(-1) */
void ln(char *filepath, char *dest, char link_type){
/* END ANNOTATION 2 */

  char *base = basename(filepath);
  unsigned int file_inode = getInode(filepath, base);

  if (file_inode == 0){
    exit(ENOENT);
  }

  struct ext2_inode *file = table + file_inode - 1;

  if (file->i_mode & EXT2_S_IFDIR){
    exit(EISDIR);
  }

  unsigned int parent_link_inode = getParentInode(root_entry, EXT2_ROOT_INO, dest+1);

  if (parent_link_inode == 0){
    exit(ENOENT);
  }

  struct ext2_inode *parent_of_link = table + parent_link_inode - 1;

  // file <- inode entry for the file we're linking.
  // parent_of_link <- inode entry of parent of the link.

  if (link_type == 'h'){ //hard_link

    unsigned char file_type = EXT2_FT_UNKNOWN;
    if (file->i_mode & EXT2_S_IFREG)
      file_type = EXT2_FT_REG_FILE;
    else if (file->i_mode & EXT2_S_IFLNK)
      file_type = EXT2_FT_SYMLINK;

    struct ext2_dir_entry *link;
    link = createDirectoryEntry(parent_of_link, parent_link_inode, basename(dest), file_type);

    // Hard link: just set to the original file's inode number.
    link->inode = file_inode;
    file->i_links_count++;

  } else { //'s'

    //TODO handle symbolic links. Anything?
    struct ext2_dir_entry *link;

    link = createDirectoryEntry(parent_of_link, parent_link_inode, basename(dest), EXT2_FT_SYMLINK);

    //Create a new inode
    link->inode = newInode(EXT2_FT_SYMLINK, parent_link_inode);

    struct ext2_inode *sym_link = table + link->inode - 1;

    int str_left = strlen(filepath);
    int index = 0;

    // Copy the path name of the original file into the data blocks.
    while (index < 12 && str_left > 0) {

      sym_link->i_block[index] = acquireBlock();
      sym_link->i_blocks += 2;
      unsigned char *block = disk + sym_link->i_block[index]*EXT2_BLOCK_SIZE;
      size_t to_copy = EXT2_BLOCK_SIZE;

      if (str_left < EXT2_BLOCK_SIZE)
        to_copy = str_left;


      memcpy(block, filepath + strlen(filepath) - str_left, to_copy);

      index++;
      str_left -= to_copy;
    }
    if (str_left > 0){
      //printf("Path name TOO LONG!\n");
      //exit(ENOSPC);
    }




  }

}

int main(int argc, char **argv){

  if(argc != 4 && !(argc == 5 && !strcmp("-s", argv[2])) ) {
      fprintf(stderr, "Usage: %s <image file name>\n", argv[0]);
      fprintf(stderr, "\t %s <image -s file name>\n", argv[0]);
      exit(1);
  }

  // See ext2_help.c
  open_disk(argv[1]);

  char *path1 = (char *)malloc((1 + strlen(argv[2]))*sizeof(char));
  char *path2 = (char *)malloc((1 + strlen(argv[3]))*sizeof(char));

  char link_type = 'h';

  // Could use getopt, simple enough just to check like this.
  if (!strcmp("-s", argv[2])){
    link_type = 's';
    validatePath(path1, argv[3]);
    validatePath(path2, argv[4]);
  }
  else{
    validatePath(path1, argv[2]);
    validatePath(path2, argv[3]);
  }

  ln(path1, path2, link_type);

  return 0;
}
