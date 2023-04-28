#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
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

int getInodeBitmap(int inode){
  // Should have inode <= 4*8
  //(33-1)/8 = 4 but there are only bytes 0,1,2,3
  return (inode_bitmap[(inode-1)/8] >> (inode - 1) % 8) & 1;
}

int getBlockBitmap(int block){
  // Should have block <= 16*8
  return (block_bitmap[(block-1)/8] >> (block-1) % 8) & 1;
}

void setInodeBitmap(int inode){
  inode_bitmap[(inode-1)/8] |= 1 << (inode - 1) % 8;
}

void setBlockBitmap(int block){
  block_bitmap[(block-1)/8] |= 1 << (block-1) % 8;
}

void unsetInodeBitmap(int inode){
  inode_bitmap[(inode-1)/8] &= ~(1 << (inode - 1) % 8);
}

void unsetBlockBitmap(int block){
  block_bitmap[(block-1)/8] &= ~(1 << (block-1) % 8);
}


// Inital function called by all the programs. Takes a disk with local path name
// and opens in. Sets up common variables likes root entry, inode table, etc ...
void open_disk(char *name){

  int fd = open(name, O_RDWR);
  disk = mmap(NULL, 128 * EXT2_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if(disk == MAP_FAILED) {
      perror("mmap");
      exit(1);
  }

  gd = (struct ext2_group_desc *)(disk + EXT2_ROOT_INO*EXT2_BLOCK_SIZE);
  table = (struct ext2_inode *)(disk + gd->bg_inode_table*EXT2_BLOCK_SIZE);
  root_entry = table + EXT2_ROOT_INO - 1;
  inode_bitmap = (unsigned char *)(disk + EXT2_BLOCK_SIZE*gd->bg_inode_bitmap);
  block_bitmap = (unsigned char *)(disk + EXT2_BLOCK_SIZE*gd->bg_block_bitmap);
  sup = (struct ext2_super_block *) (disk + 1024);
}

// A function that, given a directory path for the disk (arg), will return a
//  valid version of the path be removing unneccessary slashes. Forces program
// that called this to exit if the path is not valid.
void validatePath(char *path, char *arg){

  int index = 0;
  short slashBool = 0;
  for (int i = 0; i < strlen(arg) + 1; i++){
    char c = arg[i];

    // Find a slash
    if (c == '/' && !slashBool){
      slashBool = 1;
      path[index++] = c;

    // After finding a slash, remove all slashes that immediately follow
    } else if (c != '/'){
      slashBool = 0;
      path[index++] = c;
    }
  }

  for (int i = index-2; i >= 0; i--){
    char c = path[i];
    if (c == '/' && i != 0)
      path[i] = '\0';
    else
      break;
  }

  if (strlen(path) < 1 || path[0] != '/'){
    exit(ENOENT);
  }

}

// Given x, return the next largest multiple of 4.
int ROUNDUP4(int num){ // Alternate: return 4 * ((num+3)/4)
  while(num % 4)
    num++;
  return num;
}


// Assume path = a1/a2/a3/..." is a string where len = strlen(path)
// Then this function seperates the path into twos strings :
// first will receive the top directory      (eg above: "a1/")
// rest will receive the rest of the string  (eg above: "a2/a3/...")

// If path = filename, then first = "" and rest = filename
// This is essentially basename and dirname, but backwards.
void slash(const char *path, int len, char *first, char *rest){

  int s = 0;
  for (int i = 1; i < len; i++){
    if (path[i] == '/'){
      s = i;
      break;
    }
  }
  // If no slashes were found.
  if (!s){
    first[0] = '\0';
    strncpy(rest, path, strlen(path) + 1);
    return;
  }

  // Copy newdest + null terminator
  strncpy(first, path, s);
  first[s] = '\0';
  strncpy(rest, path + s, strlen(path) - s + 1);

}

// strncmp two char arrays using len1 and len2 to figure out how long to
//  compare for.
int compareNames(char *file1, int len1, char *file2, int len2){
  char first[len1 + 1];
  char second[len2 + 1];
  strncpy(first, file1, len1);
  first[len1] = '\0';
  strncpy(second, file2, len2);
  second[len2] = '\0';

  return strcmp(first, second);
}

/*
 * Given a pointer to the inode entry of a directory, search that directory
 *  for the file named filename in that folder.
 *
 * eg for getDirectoryEntry(root/folder (inode ptr), subfolder )
 *         root
 *        /    \
 *     folder   lost+found
 *      / | \
 *     a  b  subfolder
 */

struct ext2_dir_entry *getDirectoryEntry(struct ext2_inode *src_ptr, unsigned int src_inode, char *filename){

  for (int block = 0; block < 12; block++){
    // Gets the block number of the files in the directory
    unsigned int start = src_ptr->i_block[block];

    if (!start)
      return NULL;

    // Gets the start memory location from the block number
    unsigned char *dir_entry_index = disk + start*EXT2_BLOCK_SIZE;

    // Need to know when to end search
    unsigned char *end = dir_entry_index + EXT2_BLOCK_SIZE;

    while (dir_entry_index < end){
      struct ext2_dir_entry *dir = (struct ext2_dir_entry *)dir_entry_index;

      //TODO right name?
      if (dir->inode != 0 && !compareNames(filename, strlen(filename), dir->name, dir->name_len)){
          return dir;
      }
      dir_entry_index += dir->rec_len;
    }
  }
  return NULL;
}

// Given a parent directory and it's inode, create a directory entry for
// the file filename. Indicate the type with file_type,
// d - directory, f - regular file, h - hard link, s - soft link
// NOTE: inodes will not be created! Have to call getInode!
struct ext2_dir_entry *createDirectoryEntry(struct ext2_inode *parent, unsigned int parent_inode, char *file_name, unsigned char file_type){

  unsigned char *prev = NULL;
  unsigned int space = NAME_BYTES + strlen(file_name);
  space = ROUNDUP4(space);

  unsigned int num_blocks = parent->i_blocks/2 < 12 ? parent->i_blocks/2 : 12;

  int block_index;
  for (block_index = 0; block_index < num_blocks; block_index++){
    prev = NULL;
    unsigned int start = parent->i_block[block_index];
    if (!start)
      break;

    // The current item of the parent being looked at.
    unsigned char *curr_dir = disk + start*EXT2_BLOCK_SIZE;

    // The ptr to the end of the block.
    unsigned char *end = curr_dir + EXT2_BLOCK_SIZE;
    struct ext2_dir_entry *curr_dir_entry = (struct ext2_dir_entry *)curr_dir;

    while (curr_dir < end){

      // Found the file already. Since this method is only called when trying
      // to create a file, the program will exit. NOTE: that may change depending on
      // other programs I implement

      if (curr_dir_entry->inode != 0 && !compareNames(curr_dir_entry->name, curr_dir_entry->name_len, file_name, strlen(file_name))) {

        exit(EEXIST);
      }

      prev = curr_dir;
      curr_dir += curr_dir_entry->rec_len;
      curr_dir_entry = (struct ext2_dir_entry *)curr_dir;
    }
  }

  struct ext2_dir_entry *new_dir;

  // No empty space found
  if (prev == NULL){
    if (block_index >= 12){
      //fprintf(stderr, "No space left in first 12 directory blocks\n");
      exit(EFBIG);
    }
    unsigned int new_block = acquireBlock();
    new_dir = (struct ext2_dir_entry *) (disk + EXT2_BLOCK_SIZE*new_block);
    new_dir->rec_len = space;

    parent->i_block[block_index] = new_block;
    parent->i_blocks += 2;
    parent->i_size += EXT2_BLOCK_SIZE;

  } else{
    struct ext2_dir_entry *prev_entry = (struct ext2_dir_entry *)prev;

    // The new rec len of prev
    unsigned int prev_new_rec_len = NAME_BYTES + ROUNDUP4(prev_entry->name_len);

    new_dir = (struct ext2_dir_entry *) (prev + prev_new_rec_len);
    new_dir->rec_len = prev_entry->rec_len - prev_new_rec_len; //end of the block
    prev_entry->rec_len = prev_new_rec_len; // Where to find the next file.
  }

  new_dir->name_len = strlen(file_name);
  strncpy(new_dir->name, file_name, strlen(file_name));
  new_dir->file_type = file_type;
  new_dir->inode = 0;

  return new_dir;
}

/**
 * Given a the inode of a some directory (src_ptr) and its inode number, look
 *  for and remove the file entry for the corresponding filename. Return 1 on
 *  success, 0 on fail. Bonus flag for removing directories (unimplemented).
 */
void removeDirectoryEntry(struct ext2_inode *src_ptr, unsigned int src_inode, char *filename, int bonus){

  unsigned char *prev_entry_index = NULL;
  int prev_block = -1;

  for (int block = 0; block < 12; block++){
    // Gets the block number of the files in the directory
    unsigned int start = src_ptr->i_block[block];

    if (start == 0){
      break;
    }

    // Gets the start memory location from the block number
    unsigned char *dir_entry_index = disk + start*EXT2_BLOCK_SIZE;

    // Need to know when to end search
    unsigned char *end = dir_entry_index + EXT2_BLOCK_SIZE;

    while (dir_entry_index < end){
      struct ext2_dir_entry *dir = (struct ext2_dir_entry *)dir_entry_index;

      if (!compareNames(filename, strlen(filename), dir->name, dir->name_len)){
          // Helper function that deletes this entry
          deleteEntry((struct ext2_dir_entry *)prev_entry_index, prev_block, (struct ext2_dir_entry *)dir_entry_index, block);
          return;
      }
      prev_entry_index = dir_entry_index;
      prev_block = block;
      dir_entry_index += dir->rec_len;
    }
  }
  exit(ENOENT);
}


/*
 * A function to help locate files. Will return the parent's inode information.
 */
unsigned int getParentInode(struct ext2_inode *root_ptr, unsigned int root_inode, char *pathName){

  // root_ptr must a valid directory in recursion
  // It also must be a valid inode
  if (!((root_ptr->i_mode & typemask) == EXT2_S_IFDIR) || (root_inode == EXT2_ROOT_INO && !strcmp("", pathName))){
    return 0;
  }

  int len = strlen(pathName);
  char front[len];
  char back[len];

  //If path was /a/b/c/d, the front contains /a and back contains /b/c/d
  slash(pathName, len, front, back);

  //Found the parent folder
  if (!strcmp("", front)){
    return root_inode;
  }

  struct ext2_dir_entry *subfolder_entry;
  subfolder_entry = getDirectoryEntry(root_ptr, root_inode, front);


  if (subfolder_entry == NULL){
    return 0;
  }
  else{
    struct ext2_inode *subfolder = table + subfolder_entry->inode - 1;
    return getParentInode(subfolder, subfolder_entry->inode, back+1);
  }

}

/**
  * Given a pathname and the filename = basename(pathname), find the inode of
  * the file in the image given by pathname. Return 1/0 on success/fail.
  */
unsigned int getInode(char *pathName, char *fileName){

  if (!strcmp(pathName, "/")){
    return EXT2_ROOT_INO;
  }
  unsigned int parent_inode = getParentInode(root_entry, EXT2_ROOT_INO, pathName+1);
  if (parent_inode == 0){
    return 0;

  }
  struct ext2_inode *parent = table + parent_inode - 1;
  struct ext2_dir_entry *file_entry = getDirectoryEntry(parent, parent_inode, fileName);

  if (file_entry == NULL)
    return 0;

  return file_entry->inode;

}


/*
 * Looks in the inode bitmap for a file inode to create. SEtup the inode entry
 * in the inode table.
 * For directories: create a block entry with the directory map (with . and ..)
 * For symlinks/files: Just make the inode.
 */
unsigned int newInode(unsigned int file_type, unsigned int parent_inode){
  int inode = 1;
  for (int byte = 0; byte < 4; byte++){
    for (int i = 0; i < 8; i++){

      if (inode <= 11){
        inode++;
        continue;
      }

      // Finds an invalid inode
      if (!getInodeBitmap(inode)){

        struct ext2_inode *new_inode = table + inode - 1;

        if (file_type == EXT2_FT_SYMLINK)
          new_inode->i_mode = EXT2_S_IFLNK;
        else if (file_type == EXT2_FT_REG_FILE)
          new_inode->i_mode = EXT2_S_IFREG;
        else if (file_type)
          new_inode->i_mode = EXT2_S_IFDIR;

        // Permissions. Set so I can see when mounting. (Didn't work)
        //new_inode->i_mode |= 0x0100 | 0x0080 | 0x0040;

        new_inode->i_uid = 0;
        new_inode->i_size = 0;
        new_inode->i_dtime = 0;
        new_inode->i_gid = 0;
        new_inode->i_blocks = 0;
        new_inode->osd1 = 0;
        new_inode->i_generation = 0;
        new_inode->i_file_acl = 0;
        new_inode->i_dir_acl = 0;
        new_inode->i_faddr = 0;
        //new_inode->extra (unsigned int [3])

        // Sets all the data blocks to empty
        for (int j = 0; j < 15; j++)
          new_inode->i_block[j] = 0;

        new_inode->i_links_count++;

        if (file_type == EXT2_FT_DIR){
          new_inode->i_block[0] = acquireBlock();

          struct ext2_dir_entry *dot = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE*new_inode->i_block[0]);
          dot->inode = inode;
          dot->rec_len = 12; //8 for inode/rec_len/file_type, 1 for name, round up to 12
          dot->name_len = 1;
          dot->file_type = 0 | EXT2_FT_DIR;
          strncpy(dot->name, ".", 1);

          struct ext2_dir_entry *dotDot;
          dotDot = (struct ext2_dir_entry *)(((unsigned char *)dot) + dot->rec_len);

          dotDot->inode = parent_inode;
          new_inode->i_links_count++;
          struct ext2_inode *parent = table + parent_inode - 1;
          parent->i_links_count++;

          dotDot->rec_len = EXT2_BLOCK_SIZE - 12; // End of block
          dotDot->name_len = 2;
          dotDot->file_type = 0 | EXT2_FT_DIR;
          strncpy(dotDot->name, "..", 2);

          new_inode->i_size = EXT2_BLOCK_SIZE;
          new_inode->i_blocks += 2;

          gd->bg_used_dirs_count++;

        }
        else if (file_type == EXT2_FT_REG_FILE){
          //
        }
        else if (file_type == EXT2_FT_SYMLINK){

        }

        // Set the inode bit to valid
        sup->s_free_inodes_count--;
        gd->bg_free_inodes_count--;

        setInodeBitmap(inode);
        return inode;
      }
      inode++;
    }
  }
  //fprintf(stderr, "No memory left!!!\n");
  exit(ENOSPC);
}



/*
 * Acquire a free block from the block bitmap. Returns the block number upon
 * success. Exits the program upon failure.
 */
unsigned int acquireBlock(){

  unsigned int block = 0;

  for (int byte = 0; byte < 16; byte++){
    for (int i = 0; i < 8; i++){
      // Gets an invalid valid inode
      if (block != 0 && !getBlockBitmap(block)){

        setBlockBitmap(block);
        sup->s_free_blocks_count--;
        gd->bg_free_blocks_count--;
	      return block;
      }
      block++;
    }
  }
  //fprintf(stderr, "No memory left\n");
  exit(ENOSPC);
}

/*
 * Given an inode TODO and a type?, set the inode from the inode table as removed and
 *  set it as free in the inode bitmap.
 */
void removeInode(unsigned int inode){

  if (inode <= 11){
    return;
  }

  struct ext2_inode *removed_inode = table + inode - 1;

  if (--removed_inode->i_links_count > 0){
    return;
  }

  removed_inode->i_dtime = 1;

  // Remove the direct/indirect blocks of the data file.
  for (int block = 0; block < 13; block++){

    if (removed_inode->i_block[block] == 0)
      break;

    if (block < 12){
      removeBlock(removed_inode->i_block[block]);
    }
    else {
      unsigned int *indirect = (unsigned int *)(disk + EXT2_BLOCK_SIZE*removed_inode->i_block[block]);
      for (int j = 0; j < EXT2_BLOCK_SIZE/4; j++){
        if (indirect[j] == 0)
          break;
        removeBlock(indirect[j]);
      }
    }
  }

  //TODO set inode bitmap
  //setInodeBitmap(inode);
  //inode_bitmap[(inode-1)/8] &= ~(1 << (inode - 1) % 8);
  unsetInodeBitmap(inode);
  sup->s_free_inodes_count++;
  gd->bg_free_inodes_count++;

}

// Mark a block as free
void removeBlock(int block){

  if (!block)
    return;

  if (getBlockBitmap(block)){
    //block_bitmap[(block)/8] &= ~(1 << (block % 8));
    unsetBlockBitmap(block);
    sup->s_free_blocks_count++;
    gd->bg_free_blocks_count++;
    return;
  }

  //fprintf(stderr, "Not in use!\n");

}

// Given an entry and block, and it's previous entry and block, remove the entry
// By setting the pointer of the previous to point to entry's next directory entry.
void deleteEntry(struct ext2_dir_entry *prev_entry, int prev_block, struct ext2_dir_entry *entry, int entry_block){

  if (entry->file_type & EXT2_FT_DIR)
    exit(EISDIR);

  unsigned int inode = 0;
  // This means the very first directory entry was the file
  if (prev_entry ==  NULL){
    inode = entry->inode;
    entry->inode = 0;
    removeInode(inode);
  } else {

    // Crosses blocks?
    if (prev_block != entry_block){  //OR prev_entry + prev_entry->rec_len >= end
      inode = entry->inode;
      entry->inode = 0;
      removeInode(inode);

    } else { // Same block
      inode = entry->inode;
      prev_entry->rec_len += entry->rec_len; //NOTE don't/do? do this for restore
      removeInode(inode);
    }

  }

}
