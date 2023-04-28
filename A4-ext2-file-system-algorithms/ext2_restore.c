#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <libgen.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "ext2.h"
#include "ext2_help.h"

int checkGap(unsigned char *curr, unsigned char *next, char *filename){

  struct ext2_dir_entry *curr_entry = (struct ext2_dir_entry *)curr;
  unsigned char *deleted = curr + NAME_BYTES + ROUNDUP4(curr_entry->name_len);
  struct ext2_dir_entry *deleted_entry = (struct ext2_dir_entry *)deleted;
  int count = 0;

  while (deleted != NULL && deleted + deleted_entry->rec_len <= next && count++ < 10){

    // This entry cannot be valid if this is the case.
    // More importantly, we don't want an infinite loop!
    if (deleted_entry->rec_len % 4 || deleted_entry->rec_len == 0)
      return 0;

    if (!compareNames(deleted_entry->name, deleted_entry->name_len, filename, strlen(filename))){


      unsigned int inode = deleted_entry->inode;
      // Gets the data from the inode
      struct ext2_inode *removed_inode = table + inode - 1;

      if ((inode & typemask) == EXT2_S_IFDIR)
        exit(EISDIR);

      // Checks that the inode was not claimed by someone else after deletion.
      if (inode == 0 || getInodeBitmap(inode)) {
        exit(EACCES);
      }

      int num = removed_inode->i_blocks/2 < 12 + EXT2_BLOCK_SIZE/4 ? removed_inode->i_blocks/2 : 12 + EXT2_BLOCK_SIZE/4;
      int index;

      // Checks that all of the data blocks haven't been reclaimed.
      for (index = 0; index < num; index++){
        unsigned int block = 0;
        if (index < 12)
          block = removed_inode->i_block[index];
        else {
          unsigned int *single = (unsigned int *)(disk + EXT2_BLOCK_SIZE*removed_inode->i_block[12]);
          block = single[index-12];
        }

        // This shouldn't happen if i_blocks is set correctly
        if (block == 0){
          break;
        }

        if (getBlockBitmap(block)){
          exit(EACCES);
        }
      }

      // Every block and the inode is a-ok.
      setInodeBitmap(inode);
      removed_inode->i_links_count++;
      sup->s_free_inodes_count--;
      gd->bg_free_inodes_count--;

      for (int index = 0; index < 12; index++){
        unsigned int block = 0;
        if (index < 12)
          block = removed_inode->i_block[index];
        else {
          unsigned int *single = (unsigned int *)(disk + EXT2_BLOCK_SIZE*removed_inode->i_block[12]);
          block = single[index-12];
        }

        if (block == 0)
          break;

        setBlockBitmap(block);
        sup->s_free_blocks_count--;
        gd->bg_free_blocks_count--;

      }

      curr_entry->rec_len = deleted - curr;
      deleted_entry->rec_len = next - deleted;

      return 1;
    }
    deleted = deleted + deleted_entry->rec_len;
    deleted_entry = (struct ext2_dir_entry *)deleted;

  }
  return 0;
}


void restoreDirectoryEntry(struct ext2_inode *parent_file, unsigned int parent_file_inode, char *name){

  // Gets the block number of the files in the directory
  int block = 0;

  // Iterate through all the blocks for this directory.
  for (block = 0; block < 12; block++){

    unsigned int start = parent_file->i_block[block];

    if (start == 0)
      break;

    unsigned char *dir_entry_index = disk + start*EXT2_BLOCK_SIZE;
    unsigned char *end = dir_entry_index + EXT2_BLOCK_SIZE;

    int count = 0;
    while (dir_entry_index < end && count++ < 25){
      struct ext2_dir_entry *dir = (struct ext2_dir_entry *)dir_entry_index;

      if (dir->inode != 0 && !compareNames(name, strlen(name), dir->name, dir->name_len)){
          exit(EEXIST);
      }

      unsigned char *next = dir_entry_index + dir->rec_len;

      //if (next < end){
        // A helper that checks whether the file was left in the gap.
        if (checkGap(dir_entry_index, next, name)){
          return;
        }
      //}

      dir_entry_index += dir->rec_len;
    }
  }
}

void restore(char *path){

  unsigned int parent_file_inode = getParentInode(root_entry, EXT2_ROOT_INO, path + 1);
  char *base = basename(path);

  if (parent_file_inode == 0){
    exit(1);
  }

  // Restore the directory entry
  struct ext2_inode *parent_file = table + parent_file_inode - 1;
  restoreDirectoryEntry(parent_file, parent_file_inode, base);
  exit(ENOENT);
}



/* ANNOTATION 5: Fails to correctly set dtime (-1 RESTORE). Also fails to correctly set block bitmap and free block went removing a large file (-1 RESTORE). */
int main(int argc, char **argv){
/* END ANNOTATION 5 */

  if(argc != 3) {
      fprintf(stderr, "Usage: %s <image file>\n", argv[0]);
      exit(1);
  }

  // See ext2_help.c
  open_disk(argv[1]);

  char *path = (char *)malloc((1 + strlen(argv[2]))*sizeof(char));

  validatePath(path, argv[2]);

  restore(path);

  return 0;
}
