#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <libgen.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "ext2.h"
#include "ext2_help.h"

int counter = 0;

// Unused
/*
void check1help(unsigned int *a, unsigned int b, unsigned int c, char *word1, char *word2){
  if (*a !=  b - c){
    int diff = *a - (b - c);
    if (diff < 0)
      diff = -diff;
    *a = b - c;
    printf("Fixed: %s's %s counter was off by %d compared to the bitmap\n", word1, word2, diff);
    counter++;
  }
}*/

void check2(unsigned int inode, unsigned short mode, unsigned char *type){

  int message = 0;

  if ((mode & typemask) == EXT2_S_IFLNK){
      if (*type != EXT2_FT_SYMLINK){
        *type = EXT2_FT_SYMLINK;
        message = 1;
      }
  }
  else if ((mode & typemask) == EXT2_S_IFREG){
      if (*type != EXT2_FT_REG_FILE){
        *type = EXT2_FT_REG_FILE;
        message = 1;
      }
  }

  else if ((mode & typemask) == EXT2_S_IFDIR){
      if (*type != EXT2_FT_DIR){
        *type = EXT2_FT_DIR;
        message = 1;
      }
  }

  if (message){
    counter++;
    printf("Fixed: Entry type vs inode mismatch: inode [%u]\n", inode);
  }

}

void check1(){

  // Check block bit map
  unsigned int block_count = 0;
  for (int byte = 0; byte < 16; byte++){
    for (int i = 0; i < 8; i++){
      // Gets an invalid valid inode
      if ((block_bitmap[byte] >> i & 1)){
        block_count++;
      }

    }
  }

  // Check inode bitmap
  unsigned int inode_count = 0;
  for (int byte = 0; byte < 4; byte++){
    for (int i = 0; i < 8; i++){
      if (inode_bitmap[byte] >> i & 1){
        inode_count++;
      }
    }
  }

  if (sup->s_free_inodes_count != sup->s_inodes_count - inode_count){
    int diff = sup->s_free_inodes_count - (sup->s_inodes_count - inode_count);
    if (diff < 0)
      diff = -diff;
    sup->s_free_inodes_count = sup->s_inodes_count - inode_count;
    printf("Fixed: superblock's free inodes counter was off by %d compared to the bitmap\n", diff);
    counter += diff;

  }

  if (sup->s_free_blocks_count != sup->s_blocks_count - block_count){
    int diff = sup->s_free_blocks_count - (sup->s_blocks_count - block_count);
    if (diff < 0)
      diff = -diff;
    sup->s_free_blocks_count = sup->s_blocks_count - block_count;
    printf("Fixed: superblock's free blocks counter was off by %d compared to the bitmap\n", diff);
    counter += diff;

  }

  if (gd->bg_free_inodes_count != sup->s_inodes_count - inode_count){
    int diff = gd->bg_free_inodes_count - (sup->s_inodes_count - inode_count);
    if (diff < 0)
      diff = -diff;
    gd->bg_free_inodes_count = sup->s_inodes_count - inode_count;
    printf("Fixed: block group's free inodes counter was off by %d compared to the bitmap\n", diff);
    counter += diff;
  }

  if (gd->bg_free_blocks_count != sup->s_blocks_count - block_count){
    int diff = gd->bg_free_blocks_count - (sup->s_blocks_count - block_count);
    if (diff < 0)
      diff = -diff;
    gd->bg_free_blocks_count = sup->s_blocks_count - block_count;
    printf("Fixed: block group's free blocks counter was off by %d compared to the bitmap\n", diff);
    counter += diff;
  }

}


void check3(unsigned int inode){

  int i = getInodeBitmap(inode);

  if (!i){
    setInodeBitmap(inode);
    sup->s_free_inodes_count--;
    gd->bg_free_inodes_count--;

    counter++;
    printf("Fixed: inode [%u] not marked as in-use\n", inode);
  }


}
void check4(unsigned int inode){
  struct ext2_inode *file = table + inode - 1;
  if (file->i_dtime){
    file->i_dtime = 0;
    printf("Fixed: valid inode marked for deletion: [%u]\n", inode);
    counter++;
  }


}

void checkBlock(unsigned int inode, unsigned int block){

  if (block <= 0)
    return;

  int i = getBlockBitmap(block);
  if (!i){
    setBlockBitmap(block);
    sup->s_free_blocks_count--;
    gd->bg_free_blocks_count--;
/* ANNOTATION 3: This should be printed just one time for each inode indicating the total number of inconsistencies of this inode (-1) */
    printf("Fixed: %u in-use data blocks not marked in data bitmap for inode: [%u]\n", block, inode);
/* END ANNOTATION 3 */
    counter++;
  }

}


void check5(unsigned int inode){
  struct ext2_inode *file = table + inode - 1;

  for (int i = 0; i < file->i_blocks/2; i++){

    if (i < 12){
      unsigned int block = file->i_block[i];
      if (block == 0){
        break;
      }
      checkBlock(inode, block);
    }
    else { // Just single indirect
      unsigned int check = file->i_block[12];
      if (check == 0)
        break;
      unsigned int block = (disk + check*EXT2_BLOCK_SIZE)[i - 12];

      checkBlock(inode, block);
      break;
    }

  }
}

void check2345(struct ext2_inode *parent){

  for (int block = 0; block < 12; block++){
    // Gets the block number of the files in the directory
    unsigned int start = parent->i_block[block];

    if (!start)
      break;

    // Gets the start memory location from the block number
    unsigned char *entry_index = disk + start*EXT2_BLOCK_SIZE;
    unsigned char *end = entry_index + EXT2_BLOCK_SIZE;

    while (entry_index < end){

      struct ext2_dir_entry *curr_file = (struct ext2_dir_entry *)entry_index;

      if (curr_file->rec_len == 0){
        break;
      }

      if (curr_file->inode == 0){
        entry_index += curr_file->rec_len;
        continue;
      }

      struct ext2_inode *curr_file_inode = table + curr_file->inode - 1;

      check2(curr_file->inode, curr_file_inode->i_mode, &curr_file->file_type);

      check3(curr_file->inode);
      check4(curr_file->inode);
      check5(curr_file->inode);

      if (curr_file->file_type == EXT2_FT_DIR
                                        && compareNames(".", 1, curr_file->name, curr_file->name_len)
                                        && compareNames("..", 2, curr_file->name, curr_file->name_len)) {

        check2345(curr_file_inode);

      }

      entry_index += curr_file->rec_len;

    }
  }
}



void checker(){
  check1();
  check2345(root_entry);

  if (counter)
    printf("%d file system inconsistencies repaired!\n", counter);
  else
    printf("No file system inconsistencies detected!\n");

}


int main(int argc, char **argv){

  if(argc != 2) {
      fprintf(stderr, "Usage: %s <image>\n", argv[0]);
      exit(1);
  }

  // See ext2_help.c
  open_disk(argv[1]);

  checker();

  return 0;
}
