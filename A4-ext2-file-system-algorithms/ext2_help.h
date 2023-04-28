#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "ext2.h"

// Size of a dir_entry without the name.
#define NAME_BYTES 8

#define typemask 0xF000

extern unsigned char *disk;
extern struct ext2_inode *root_entry;
extern struct ext2_group_desc *gd;
extern struct ext2_inode *table;
extern unsigned char *inode_bitmap;
extern unsigned char *block_bitmap;
extern struct ext2_super_block *sup;

int getInodeBitmap(int inode);
int getBlockBitmap(int block);
void setInodeBitmap(int inode);
void setBlockBitmap(int block);

void open_disk(char *name);
void validatePath(char *path, char *arg);
int compareNames(char *file1, int len1, char *file2, int len2);
int ROUNDUP4(int);

struct ext2_dir_entry *createDirectoryEntry(struct ext2_inode *parent, unsigned int parent_inode, char *file_name, unsigned char file_type);
struct ext2_dir_entry *getDirectoryEntry(struct ext2_inode *src_ptr, unsigned int src_inode, char *filename);
void removeDirectoryEntry(struct ext2_inode *src_ptr, unsigned int src_inode, char *filename, int bonus);
void deleteEntry(struct ext2_dir_entry *prev_entry, int prev_block, struct ext2_dir_entry *entry, int entry_block);
unsigned int getParentInode(struct ext2_inode *root_ptr, unsigned int root_inode, char *pathName);
unsigned int getInode(char *pathName, char *fileName);

unsigned int newInode(unsigned int file_type, unsigned int parent_inode);
unsigned int acquireBlock();
void removeBlock(int block);
