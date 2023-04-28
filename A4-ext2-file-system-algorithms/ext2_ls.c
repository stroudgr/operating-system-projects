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
struct ext2_inode *root_entry;
struct ext2_group_desc *gd;
struct ext2_inode *table;
unsigned char *inode_bitmap;
unsigned char *block_bitmap;
struct ext2_super_block *sup;

void ls(char *relative_path){
  char *base = basename(relative_path);
  printf("%s\n", base);
  unsigned int entry_inode = getInode(relative_path, base);

  if (entry_inode == 0){
    printf("%s\n", "No such file");
    return;
  }

  struct ext2_inode *entry = table + entry_inode - 1;

  if (!(entry->i_mode & EXT2_S_IFDIR)){
    printf("%s\n", base);
    return;
  }

  unsigned int num_blocks = entry->i_blocks/2 < 12 ? entry->i_blocks/2 : 12;
  //printf("%s\n", );
  int block_index;
  printf("Inode \t Type \t Name \t Dir entry pointer\n");

  for (block_index = 0; block_index < num_blocks; block_index++){
    // The first block of the entry directory.
    unsigned int start = entry->i_block[block_index];

    if (!start){
      break;
    }

    // The current item of the entry being looked at.
    unsigned char *curr_dir = disk + start*EXT2_BLOCK_SIZE;

    // The ptr to the end of the block.
    unsigned char *end = curr_dir + EXT2_BLOCK_SIZE;
    struct ext2_dir_entry *curr_dir_entry = (struct ext2_dir_entry *)curr_dir;

    // Get an empty element of the linkedlist. Note that there is always one
    //   even if the directory is empty
    unsigned int count = 0;

    while (curr_dir < end){

      // Found the file already. Since this method is only called when trying
      // to create a file, the program will exit. NOTE: may change depending on
      // other programs I implement

      char name[EXT2_NAME_LEN + 1];
      strncpy(name,curr_dir_entry->name, curr_dir_entry->name_len);
      name[curr_dir_entry->name_len] = '\0';


      char type = 'u';
      if (curr_dir_entry->file_type & EXT2_FT_REG_FILE)
        type = 'f';
      else if (curr_dir_entry->file_type & EXT2_FT_DIR)
        type = 'd';
      else if (curr_dir_entry->file_type & EXT2_FT_SYMLINK)
        type = 'l';

      printf(" %u\t %c \t %s \t %p\n", curr_dir_entry->inode, type, name, curr_dir_entry);


      curr_dir += curr_dir_entry->rec_len; //TODO right?
      count += curr_dir_entry->rec_len;
      curr_dir_entry = (struct ext2_dir_entry *)curr_dir;

    }

  }

}


int main(int argc, char **argv){

  if(argc != 3) {
      fprintf(stderr, "Usage: %s <image file name>\n", argv[0]);
      exit(1);
  }

  // See ext2_help.c
  open_disk(argv[1]);

  char *path = (char *)malloc((1 + strlen(argv[2]))*sizeof(char));

  validatePath(path, argv[2]);
  ls(path);

  return 0;
}
