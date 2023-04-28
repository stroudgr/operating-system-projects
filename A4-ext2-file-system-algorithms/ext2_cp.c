#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "ext2.h"
#include "ext2_help.h"

/* ANNOTATION 1: ext2_cp:

large file/dir not copied properly (-1)
Failed to copy all files -3 */
unsigned char *disk;
/* END ANNOTATION 1 */
struct ext2_inode *root_entry;
struct ext2_group_desc *gd;
struct ext2_inode *table;
unsigned char *inode_bitmap;
unsigned char *block_bitmap;
struct ext2_super_block *sup;

FILE *input;

int copyData(unsigned int inode, unsigned int type);


/**
 * Locate file img_folder. Then create a file in that folder called file_name.
 * Then copy data from a file on local machine to the inode's data blocks.
 */
int copy(char *img_folder, char *file_name, unsigned int file_type){
  int parent_inode = getParentInode(root_entry, EXT2_ROOT_INO, img_folder + 1);
  if (!parent_inode){
    exit(ENOENT);
  }


  struct ext2_inode *parent = table + parent_inode - 1;
  struct ext2_dir_entry *inode = createDirectoryEntry(parent, parent_inode, file_name, file_type);
  inode->inode = newInode(file_type, parent_inode);

  copyData(inode->inode, inode->file_type);
  return 0;
}

/*
 * Given a place to store a block pointer, will acquire a block and
 *   copy the data into that block
 */
void copyHelper(struct ext2_inode *inode, unsigned int *direct_pointer, char *data, int amount){

  direct_pointer[0] = acquireBlock();
  inode->i_blocks += 2;
  unsigned int *block = (unsigned int *)(disk + EXT2_BLOCK_SIZE*direct_pointer[0]);
  //memset(disk + EXT2_BLOCK_SIZE*direct_pointer[0] - 1, 0, EXT2_BLOCK_SIZE);
  memcpy(block, data, amount);

}

// Given a place to store an indirect block pointer, will acquire a block and
// zero the block
void getIndirect(unsigned int *direct_pointer){
  direct_pointer[0] = acquireBlock();
  memset(disk + EXT2_BLOCK_SIZE*direct_pointer[0], 0, EXT2_BLOCK_SIZE);
}

/**
 * Copy the data from input to the inodes data blocks.
 */
int copyData(unsigned int inode, unsigned int type){

  char items[EXT2_BLOCK_SIZE];
  int index = 0;
  int single_index = 0;

  // Not used for this assignment.
  //int double_index = 0;
  //int triple_index = 0;

  int num;

  struct ext2_inode *file = (struct ext2_inode *) (table + inode -1);
  file->i_size = 0;

  unsigned int *first_direct_pointer = file->i_block;
  num = fread(items, sizeof(char), EXT2_BLOCK_SIZE, input);
  while (num){



    if (index <= 11){
      copyHelper(file, first_direct_pointer + index, items, num);
      file->i_size += num;
      index++;
    }
    else if (index == 12){

      // Sets up singly direct pointer.
      if (file->i_block[index] == 0){ //This should be true
          getIndirect(file->i_block + index);
      }

      // Pointer to block of direct pointers
      unsigned int *single_pointer_block = (unsigned int *)(disk + file->i_block[index]);
      if (single_index < EXT2_BLOCK_SIZE/4){
        copyHelper(file, single_pointer_block + single_index, items, num);
        if (++single_index >= EXT2_BLOCK_SIZE/4){
          single_index = 0;
          index++;
        }
      }
    }

    else {
      //ERROR: file too big
      exit(ENOMEM);
    }

    num = fread(items, sizeof(char), EXT2_BLOCK_SIZE, input);

  }

  return 0;
}

/**
 * Basically basename. For some reason behaviour wasn't as expected when I used
 * basename.
*/
char *base(char *path){
  int num = 0;
  for (int i = strlen(path); i >= 0; i--){
    if (path[i] == '/'){
      num = i;
      break;
    }
  }
  char *ret = (char *)malloc(strlen(path) - num + 1);
  strncpy(ret, path + num, strlen(path) - num + 1);
  ret[strlen(path) - num] = '\0';
  return ret;
}


int main(int argc, char **argv){

  if(argc != 4) {
      fprintf(stderr, "Usage: %s <image localfile diskloc>\n", argv[0]);
      exit(1);
  }

  // See ext2_help.c
  open_disk(argv[1]);

  input = fopen(argv[2], "rb");


  char *path = (char *)malloc((1 + strlen(argv[3]))*sizeof(char));
  validatePath(path, argv[3]);

  char *name = base(path);

  copy(path, name+1, EXT2_FT_REG_FILE);

  return 0;
}
