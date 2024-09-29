/*
 * This code is provided solely for the personal and private use of students
 * taking the CSC369H course at the University of Toronto. Copying for purposes
 * other than this use is expressly prohibited. All forms of distribution of
 * this code, including but not limited to public repositories on GitHub,
 * GitLab, Bitbucket, or any other online platform, whether as given or with
 * any changes, are expressly prohibited.
 *
 * Authors: Alexey Khrabrov, Karen Reid, Angela Demke Brown
 *
 * All of the files in this directory and all subdirectories are:
 * Copyright (c) 2022 Angela Demke Brown
 */

/**
 * CSC369 Assignment 4 - vsfs driver implementation.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

// Using 2.9.x FUSE API
#define FUSE_USE_VERSION 29
#include <fuse.h>

#include "vsfs.h"
#include "fs_ctx.h"
#include "options.h"
#include "util.h"
#include "bitmap.h"
#include "map.h"

//NOTE: All path arguments are absolute paths within the vsfs file system and
// start with a '/' that corresponds to the vsfs root directory.
//
// For example, if vsfs is mounted at "/tmp/my_userid", the path to a
// file at "/tmp/my_userid/dir/file" (as seen by the OS) will be
// passed to FUSE callbacks as "/dir/file".
//
// Paths to directories (except for the root directory - "/") do not end in a
// trailing '/'. For example, "/tmp/my_userid/dir/" will be passed to
// FUSE callbacks as "/dir".


/**
 * Initialize the file system.
 *
 * Called when the file system is mounted. NOTE: we are not using the FUSE
 * init() callback since it doesn't support returning errors. This function must
 * be called explicitly before fuse_main().
 *
 * @param fs    file system context to initialize.
 * @param opts  command line options.
 * @return      true on success; false on failure.
 */

/* 
	/u/csc369h/fall/pub/a4/images/vsfs-empty.disk
	/u/csc369h/fall/pub/a4/images/vsfs-1file.disk
	/u/csc369h/fall/pub/a4/images/vsfs-3file.disk
	/u/csc369h/fall/pub/a4/images/vsfs-42file.disk
	/u/csc369h/fall/pub/a4/images/vsfs-4file.disk
	/u/csc369h/fall/pub/a4/images/vsfs-deleted.disk
	/u/csc369h/fall/pub/a4/images/vsfs-empty2.disk
	/u/csc369h/fall/pub/a4/images/vsfs-many.disk
	/u/csc369h/fall/pub/a4/images/vsfs-manysizes.disk
	/u/csc369h/fall/pub/a4/images/vsfs-maxfs.disk
	
*/
static bool vsfs_init(fs_ctx *fs, vsfs_opts *opts)
{
	size_t size;
	void *image;
	
	// Nothing to initialize if only printing help
	if (opts->help) {
		return true;
	}

	// Map the disk image file into memory
	image = map_file(opts->img_path, VSFS_BLOCK_SIZE, &size);
	if (image == NULL) {
		return false;
	}

	return fs_ctx_init(fs, image, size);
}

/**
 * Cleanup the file system.
 *
 * Called when the file system is unmounted. Must cleanup all the resources
 * created in vsfs_init().
 */
static void vsfs_destroy(void *ctx)
{
	fs_ctx *fs = (fs_ctx*)ctx;
	if (fs->image) {
		munmap(fs->image, fs->size);
		fs_ctx_destroy(fs);
	}
}

/** Get file system context. */
static fs_ctx *get_fs(void)
{
	return (fs_ctx*)fuse_get_context()->private_data;
}


int traverse_block(vsfs_dentry* de, const char* path){
	for (long unsigned int i=0; i<VSFS_BLOCK_SIZE/sizeof(vsfs_dentry); i++){
		if (strcmp(de[i].name, path+1) == 0){
			return de[i].ino;
		} 
	}
	return -1;
}


/* Returns the inode number for the element at the end of the path
 * if it exists.  If there is any error, return -1.
 * Possible errors include:
 *   - The path is not an absolute path
 *   - An element on the path cannot be found
 */
static int path_lookup(const char *path,  vsfs_ino_t *ino) {
	if(path[0] != '/') {
		fprintf(stderr, "Not an absolute path\n");
		return -ENOSYS;
	} 
	// TODO: complete this function and any helper functions
	if (strcmp(path, "/") == 0) {
		*ino = VSFS_ROOT_INO;
		return 0;
	} 
	fs_ctx* fs = get_fs();
	vsfs_inode* root_ino = &fs->itable[VSFS_ROOT_INO];
	
	vsfs_blk_t iterate;
	if (root_ino->i_blocks <= 5){iterate = root_ino->i_blocks;}
	else {iterate = 5;}
	for (vsfs_blk_t i = 0; i < iterate; i++){
		vsfs_dentry* d_entries = (vsfs_dentry*)(fs->image+(VSFS_BLOCK_SIZE*root_ino->i_direct[i]));
		int num = traverse_block(d_entries,path);
		if (num != -1){				
			*ino = num;
			return 0;}
	}
	

	int num_indirects = root_ino->i_blocks - 5;
	if (num_indirects <=0){return -1;}
	vsfs_blk_t* indirect_table= (vsfs_blk_t*)(fs->image+(root_ino->i_indirect*VSFS_BLOCK_SIZE));
	for (int i = 0; i<num_indirects; ++i){
		vsfs_dentry* d_entries = (vsfs_dentry*)(fs->image+(VSFS_BLOCK_SIZE*indirect_table[i]));
		int num = traverse_block(d_entries,path);
		if (num != -1){
			*ino = num;
			return 0;}
	}
	
	return -1;
}

/**
 * Get file system statistics.
 *
 * Implements the statvfs() system call. See "man 2 statvfs" for details.
 * The f_bfree and f_bavail fields should be set to the same value.
 * The f_ffree and f_favail fields should be set to the same value.
 * The following fields can be ignored: f_fsid, f_flag.
 * All remaining fields are required.
 *
 * Errors: none
 *
 * @param path  path to any file in the file system. Can be ignored.
 * @param st    pointer to the struct statvfs that receives the result.
 * @return      0 on success; -errno on error.
 */
static int vsfs_statfs(const char *path, struct statvfs *st)
{
	(void)path;// unused
	fs_ctx *fs = get_fs();
	vsfs_superblock *sb = fs->sb; /* Get ptr to superblock from context */
	
	memset(st, 0, sizeof(*st));
	st->f_bsize   = VSFS_BLOCK_SIZE;   /* Filesystem block size */
	st->f_frsize  = VSFS_BLOCK_SIZE;   /* Fragment size */
	// The rest of required fields are filled based on the information 
	// stored in the superblock.
        st->f_blocks = sb->sb_num_blocks;     /* Size of fs in f_frsize units */
        st->f_bfree  = sb->sb_free_blocks;    /* Number of free blocks */
        st->f_bavail = sb->sb_free_blocks;    /* Free blocks for unpriv users */
	st->f_files  = sb->sb_num_inodes;     /* Number of inodes */
        st->f_ffree  = sb->sb_free_inodes;    /* Number of free inodes */
        st->f_favail = sb->sb_free_inodes;    /* Free inodes for unpriv users */

	st->f_namemax = VSFS_NAME_MAX;     /* Maximum filename length */

	return 0;
}
vsfs_inode* get_root_inode(fs_ctx* fs){
	return &fs->itable[VSFS_ROOT_INO];
}
vsfs_inode* get_inode_obj(fs_ctx* fs, int inode_num){
	if (inode_num == 0){return get_root_inode(fs);}
	return &fs->itable[inode_num];
}


/**
 * Get file or directory attributes.
 *
 * Implements the lstat() system call. See "man 2 lstat" for details.
 * The following fields can be ignored: st_dev, st_ino, st_uid, st_gid, st_rdev,
 *                                      st_blksize, st_atim, st_ctim.
 * All remaining fields are required.
 *
 * NOTE: the st_blocks field is measured in 512-byte units (disk sectors);
 *       it should include any metadata blocks that are allocated to the 
 *       inode (for vsfs, that is the indirect block). 
 *
 * NOTE2: the st_mode field must be set correctly for files and directories.
 *
 * Errors:
 *   ENAMETOOLONG  the path or one of its components is too long.
 *   ENOENT        a component of the path does not exist.
 *   ENOTDIR       a component of the path prefix is not a directory.
 *
 * @param path  path to a file or directory.
 * @param st    pointer to the struct stat that receives the result.
 * @return      0 on success; -errno on error;
 */
static int vsfs_getattr(const char *path, struct stat *st)
{
	if (strlen(path) >= VSFS_PATH_MAX) return -ENAMETOOLONG;
	fs_ctx *fs = get_fs();

	memset(st, 0, sizeof(*st));
	//TODO: lookup the inode for given path and, if it exists, fill in the
	// required fields based on the information stored in the inode
	vsfs_ino_t ino;
	vsfs_inode* inode;
	path_lookup(path, &ino);
	if (path_lookup(path, &ino) == -1) {
		return -ENOENT;}
	
	inode = &(fs->itable[ino]);
	
	st->st_ino = ino;
	st->st_mode = inode->i_mode;
	st->st_nlink = inode->i_nlink;
	st->st_size = inode->i_size;
	int i = 0;
	if (inode->i_blocks>5){i = (VSFS_BLOCK_SIZE/512);}
	st->st_blocks = inode->i_blocks * (VSFS_BLOCK_SIZE/512)+i;
	st->st_mtim = inode->i_mtime;
	return 0;
}

/**
 * Read a directory.
 *
 * Implements the readdir() system call. Should call filler(buf, name, NULL, 0)
 * for each directory entry. See fuse.h in libfuse source code for details.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a filler() call failed).
 *
 * @param path    path to the directory.
 * @param buf     buffer that receives the result.
 * @param filler  function that needs to be called for each directory entry.
 *                Pass 0 as offset (4th argument). 3rd argument can be NULL.
 * @param offset  unused.
 * @param fi      unused.
 * @return        0 on success; -errno on error.
 */
static int vsfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi)
{
	(void)offset;// unused
	(void)fi;// unused
	(void)path;
	fs_ctx *fs = get_fs();

	//TODO: lookup the directory inode for the given path and iterate 
	//      through its directory entries
	vsfs_inode* inode = &fs->itable[0];
	
	vsfs_blk_t iterate = inode->i_blocks;
	if(inode->i_blocks > 5){iterate = 5;}
	for (vsfs_blk_t i = 0; i < iterate; i++){
		vsfs_dentry* d_entry_table = (vsfs_dentry*)(fs->image+(VSFS_BLOCK_SIZE*inode->i_direct[i]));
		for (long unsigned int j = 0; j<VSFS_BLOCK_SIZE/sizeof(vsfs_dentry);++j){
			if (d_entry_table[j].ino == VSFS_INO_MAX){continue;}
			vsfs_dentry* check = (vsfs_dentry*)(fs->image+(VSFS_BLOCK_SIZE*inode->i_direct[0]));
			assert (check[0].ino == 0);
			if (filler(buf, d_entry_table[j].name, NULL,0) != 0){
				return -ENOMEM;}}}
		
	int num_indirects = inode->i_blocks - 5;
	if (num_indirects<=0){return 0;}
	vsfs_blk_t* indirect_table= (vsfs_blk_t*)(fs->image+(inode->i_indirect*VSFS_BLOCK_SIZE));
	for (int i = 0; i<num_indirects; ++i){
		vsfs_dentry* d_entry_table = (vsfs_dentry*)(fs->image+(VSFS_BLOCK_SIZE*indirect_table[i]));
		for (long unsigned int j = 0; j<VSFS_BLOCK_SIZE/sizeof(vsfs_dentry);++j){
			if (d_entry_table[j].ino == VSFS_INO_MAX){continue;}
			if (filler(buf, d_entry_table[j].name, NULL,0) != 0){
				return -ENOMEM;}
		}
	}
	return 0;
}

bool find_new_dentry(const char* path, vsfs_dentry* de, vsfs_ino_t inode_num){

	for (long unsigned int i=0; i<VSFS_BLOCK_SIZE/sizeof(vsfs_dentry); i++){
		if (de[i].name[0]=='\0'){
			de[i].ino = inode_num;
			strcpy(de[i].name, path+1);
			return true;
		} 
	}
	return false;
}

void create_new_dentry_table(vsfs_ino_t ino, const char* path, int block_table){
	fs_ctx* fs = get_fs();
	vsfs_inode* root_ino = &fs->itable[VSFS_ROOT_INO];

	// allocate new block in databitmap
	vsfs_blk_t blk;
	bitmap_alloc(fs->dbmap, fs->sb->sb_num_blocks,&blk);
	
	// assign new dir_entry block in root_inode block pointers
	// !! assume inode has no indirect blocks !!
	if (block_table == 1){root_ino->i_direct[root_ino->i_blocks] = blk;}
	else {
		vsfs_blk_t* indirect_tbl = (vsfs_blk_t*)(fs->image + ((root_ino -> i_indirect)*VSFS_BLOCK_SIZE));
		int index = root_ino->i_blocks - VSFS_NUM_DIRECT;
		indirect_tbl[index] = blk;
		
	}

	root_ino->i_blocks++;
	root_ino->i_size += VSFS_BLOCK_SIZE;
	fs->sb->sb_free_blocks --;

	vsfs_dentry* root_entries;
	// set first entry in direntry to new file

	root_entries = (vsfs_dentry*)(fs->image+(VSFS_BLOCK_SIZE*blk));
	root_entries[0].ino = ino;
	strcpy(root_entries[0].name, path+1);


	// pre-set all other entries in dir_entry
	int x = VSFS_BLOCK_SIZE/sizeof(vsfs_dentry);
	for (int i = 1; i<x; i++){
		root_entries[i].ino = VSFS_INO_MAX;
		root_entries[i].name[0] = '\0';
	}

}

int set_dir_entry(vsfs_ino_t inode_num, const char* path){
	fs_ctx* fs = get_fs();
	vsfs_inode* inode = &fs->itable[VSFS_ROOT_INO];

	// check if dir_entry table exists before entering the loop

	int iterate = VSFS_NUM_DIRECT;
	if(inode->i_blocks<VSFS_NUM_DIRECT){iterate = inode->i_blocks;}
	for (int i = 0; i < iterate; i++){
		vsfs_dentry* d_entries = (vsfs_dentry*)(fs->image + (VSFS_BLOCK_SIZE*inode->i_direct[i]));
		bool ret = find_new_dentry(path, d_entries, inode_num);
		if (ret){return 0;}
	}

	if (inode->i_blocks < VSFS_NUM_DIRECT){return 1;}

	// allocate a direct table and return 
	if (inode->i_blocks == VSFS_NUM_DIRECT){
		vsfs_blk_t blk;
		bitmap_alloc(fs->dbmap, fs->sb->sb_num_blocks, &blk);
		inode -> i_indirect = blk;

		fs->sb->sb_free_blocks--;
		return 2;
	}

	int num_indir = inode->i_blocks - VSFS_NUM_DIRECT;
	if (num_indir < 0){num_indir = 0;}
	vsfs_blk_t* indir_tbl = (vsfs_blk_t*)(fs->image + (VSFS_BLOCK_SIZE* inode->i_indirect));
	for (int i = 0; i < num_indir; ++i){
		vsfs_dentry* d_entries = (vsfs_dentry*)(fs->image+ (VSFS_BLOCK_SIZE*indir_tbl[i]));
		bool ret = find_new_dentry(path, d_entries, inode_num);
		if (ret){return 0;}
	}

	//
	return 2;
}

void create_one_empty(const char *path, mode_t mode){
	fs_ctx *fs = get_fs();
	
	vsfs_ino_t ino;
	bitmap_alloc(fs->ibmap, fs->sb->sb_num_inodes, &ino);
	int set_dentry = set_dir_entry(ino, path);
	if (set_dentry != 0){
		create_new_dentry_table(ino, path, set_dentry);
	}


	vsfs_inode* inode = &fs->itable[ino];
	if (clock_gettime(CLOCK_REALTIME, &(fs->itable[ino].i_mtime)) != 0) {
		perror("clock_gettime");
		return ;
	}

	inode -> i_nlink = 1;
	inode -> i_blocks = 0;
	inode -> i_mode = mode;
	inode -> i_size = 0;

	fs->sb->sb_free_inodes --;
	
}

/**
 * Create a file.
 *
 * Implements the open()/creat() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the file to create.
 * @param mode  file mode bits.
 * @param fi    unused.
 * @return      0 on success; -errno on error.
 */
static int vsfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	(void)fi;// unused
	assert(S_ISREG(mode));
	fs_ctx *fs = get_fs();

	if (clock_gettime(CLOCK_REALTIME, &(fs->itable[VSFS_ROOT_INO].i_mtime)) != 0) {
		perror("clock_gettime");
		return -1;}	


	create_one_empty(path, mode);
	return 0;
	//TODO: create a file at given path with given mode
	(void)path;
	(void)mode;
	(void)fs;
	return -ENOSYS;
}

int unlink_block(const char* path){
	fs_ctx* fs = get_fs();
	vsfs_inode* inode = &fs->itable[VSFS_ROOT_INO];
	vsfs_blk_t iterate = inode->i_blocks;
	if(inode->i_blocks > 5){iterate = 5;}
	for (vsfs_blk_t i = 0; i < iterate; i++){
		vsfs_dentry* d_entry_table = (vsfs_dentry*)(fs->image+(VSFS_BLOCK_SIZE*inode->i_direct[i]));

		for (long unsigned int j = 0; j<VSFS_BLOCK_SIZE/sizeof(vsfs_dentry);++j){
			if (strcmp(d_entry_table[j].name,path+1)==0){
				vsfs_ino_t ret = d_entry_table[j].ino;
				d_entry_table[j].ino = VSFS_INO_MAX;
				d_entry_table[j].name[0] = '\0';
					
				return ret;}}}
		
	int num_indirects = inode->i_blocks - 5;
	if (num_indirects<=0){return 0;}
	vsfs_blk_t* indirect_table= (vsfs_blk_t*)(fs->image+(inode->i_indirect*VSFS_BLOCK_SIZE));
	for (int i = 0; i<num_indirects; ++i){
		vsfs_dentry* d_entry_table = (vsfs_dentry*)(fs->image+(VSFS_BLOCK_SIZE*indirect_table[i]));
		
		for (long unsigned int j = 0; j<VSFS_BLOCK_SIZE/sizeof(vsfs_dentry);++j){
			if (strcmp(d_entry_table[j].name, path+1)==0){
				vsfs_ino_t ret = d_entry_table[j].ino;
				d_entry_table[j].ino = VSFS_INO_MAX;
				d_entry_table[j].name[0] = '\0';
				return ret;}
		}
	}

	return -1;
}
/**
 * Remove a file.
 *
 * Implements the unlink() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path  path to the file to remove.
 * @return      0 on success; -errno on error.
 */
static int vsfs_unlink(const char *path)
{

	// iblock and i_size is updated!
	fs_ctx *fs = get_fs();
	vsfs_ino_t inode_num = unlink_block(path);
	
	if ((int)inode_num == -1){return -1;}
	
	vsfs_inode* inode = &fs->itable[inode_num];
	int iterate = inode->i_blocks;
	if (inode->i_blocks>5){iterate =5;}
	for (int i = 0; i<iterate; ++i){
		bitmap_free(fs->dbmap,fs->sb->sb_num_blocks,inode->i_direct[i]);
		inode->i_direct[i] = VSFS_BLK_UNASSIGNED;
		fs->sb->sb_free_blocks++;
	}
	vsfs_blk_t* indirect_table= (vsfs_blk_t*)(fs->image+(inode->i_indirect*VSFS_BLOCK_SIZE));
	iterate = inode->i_blocks - VSFS_NUM_DIRECT;
	if (iterate <= 0){
		indirect_table = NULL;
		iterate = 0;}
	for (int i = 0; i < iterate; i++){
		if (indirect_table[i] == VSFS_BLK_UNASSIGNED){
			iterate++;
			continue;}
		bitmap_free(fs->dbmap, fs->sb->sb_num_blocks, indirect_table[i]);
		indirect_table[i] = VSFS_BLK_UNASSIGNED;
		fs->sb->sb_free_blocks++;
		
	}
	
	if (indirect_table){
		bitmap_free(fs->dbmap, fs->sb->sb_num_blocks, inode->i_indirect);
		fs->sb->sb_free_blocks++;
	}
	 
	bitmap_free(fs->ibmap, fs->sb->sb_num_inodes,inode_num);
	fs->sb->sb_free_inodes++;
	fs->itable[inode_num].i_nlink--;
	
	
	if (clock_gettime(CLOCK_REALTIME, &(fs->itable[VSFS_ROOT_INO].i_mtime)) != 0) {
		perror("clock_gettime");
		return -1;}	
	return 0;
}


/**
 * Change the modification time of a file or directory.
 *
 * Implements the utimensat() system call. See "man 2 utimensat" for details.
 *
 * NOTE: You only need to implement the setting of modification time (mtime).
 *       Timestamp modifications are not recursive. 
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists.
 *
 * Errors: none
 *
 * @param path   path to the file or directory.
 * @param times  timestamps array. See "man 2 utimensat" for details.
 * @return       0 on success; -errno on failure.
 */
static int vsfs_utimens(const char *path, const struct timespec times[2])
{
	fs_ctx *fs = get_fs();
	vsfs_inode *ino = NULL;
	
	//TODO: update the modification timestamp (mtime) in the inode for given
	// path with either the time passed as argument or the current time,
	// according to the utimensat man page
	(void)path;
	(void)fs;
	(void)ino;
	
	// 0. Check if there is actually anything to be done.
	if (times[1].tv_nsec == UTIME_OMIT) {
		// Nothing to do.
		return 0;
	}

	// 1. TODO: Find the inode for the final component in path
	vsfs_ino_t inode;
	path_lookup(path, &inode);
	ino = &fs->itable[inode];
	
	// 2. Update the mtime for that inode.
	//    This code is commented out to avoid failure until you have set
	//    'ino' to point to the inode structure for the inode to update.
	if (times[1].tv_nsec == UTIME_NOW) {
		if (clock_gettime(CLOCK_REALTIME, &(ino->i_mtime)) != 0) {
			//clock_gettime should not fail, unless you give it a
			//bad pointer to a timespec.
			assert(false);
		}
	} else {
		ino->i_mtime = times[1];
	}

	return 0;
	//return -ENOSYS;
}

void truncate_up_indir(const char* path, int alloc, bool from_dir){
	fs_ctx *fs = get_fs();
	vsfs_ino_t ino;
	path_lookup(path, &ino);
	vsfs_inode* inode = &fs->itable[ino];

	if (from_dir){
		// allocate indirect table block
		vsfs_blk_t block_num;
		bitmap_alloc(fs->dbmap,fs->sb->sb_num_blocks,&block_num);
		inode->i_indirect = block_num;
		fs->sb->sb_free_blocks--;
	}

	vsfs_blk_t* indirect_table = (vsfs_blk_t*)(fs->image+(inode->i_indirect*VSFS_BLOCK_SIZE));


	int num_blocks_curr = inode->i_blocks;

	int starting_index = num_blocks_curr - VSFS_NUM_DIRECT;
	for (int i = starting_index; i<starting_index+alloc;i++){
		vsfs_blk_t block_num;
		bitmap_alloc(fs->dbmap,fs->sb->sb_num_blocks,&block_num);
		indirect_table[i] = block_num;
		inode->i_blocks++;
		fs->sb->sb_free_blocks--;

		vsfs_blk_t* blk = (vsfs_blk_t*)(fs->image+(block_num*VSFS_BLOCK_SIZE));
		memset(blk, 0, VSFS_BLOCK_SIZE);
	}

    return ;
}

void truncate_up_dir(const char* path, int alloc){
	fs_ctx *fs = get_fs();
	vsfs_ino_t ino;
	path_lookup(path, &ino);

	vsfs_inode* inode = &fs->itable[ino];

	
	int num_blocks_curr = inode->i_blocks;
	
	for (int i = num_blocks_curr; i < num_blocks_curr + alloc; i++){
		// allocate block from bitmap
		uint32_t block_num;
		bitmap_alloc(fs->dbmap, fs->sb->sb_num_blocks, &block_num);	
		inode->i_direct[i] = block_num;	

		inode->i_blocks++;
		fs->sb->sb_free_blocks --;

		vsfs_blk_t* block_location = (vsfs_blk_t*)(fs->image+(block_num*VSFS_BLOCK_SIZE));
		memset(block_location, 0, VSFS_BLOCK_SIZE);

	}
	
	return ;
}

void truncate_down_dir(const char* path, off_t size){
	fs_ctx *fs = get_fs();
	vsfs_ino_t ino;
	path_lookup(path, &ino);

	vsfs_inode* inode = &fs->itable[ino];

	int curr_blocks = inode->i_blocks;
	int wanted_blocks = div_round_up(size, VSFS_BLOCK_SIZE);

	int start = curr_blocks - 1;
	int end = wanted_blocks - 1;
	for (int i = start; i > end; i--){
		uint32_t blk_num = inode->i_direct[i];
		bitmap_free(fs->dbmap, fs->sb->sb_num_blocks, blk_num);	
		inode->i_blocks--;
		fs->sb->sb_free_blocks++;
	}
	return;
}

void truncate_down_indir(const char* path, off_t size){
	fs_ctx *fs = get_fs();
	vsfs_ino_t ino;
	path_lookup(path, &ino);

	vsfs_inode* inode = &fs->itable[ino];

	int curr_blocks = inode->i_blocks;
	int wanted_blocks = div_round_up(size, VSFS_BLOCK_SIZE);
	if (wanted_blocks < VSFS_NUM_DIRECT){wanted_blocks = VSFS_NUM_DIRECT;}

	int start = curr_blocks - 1;
	int end = wanted_blocks - 1;
	
	for (int i = start; i > end; i--){
		vsfs_blk_t* indirect_table = (vsfs_blk_t*)(fs->image+(inode->i_indirect*VSFS_BLOCK_SIZE));
		vsfs_blk_t blk_num = indirect_table[i-VSFS_NUM_DIRECT];
		bitmap_free(fs->dbmap, fs->sb->sb_num_blocks, blk_num);	
		inode->i_blocks--;
		fs->sb->sb_free_blocks++;
	}
	if (wanted_blocks <= VSFS_NUM_DIRECT){
		bitmap_free(fs->dbmap, fs->sb->sb_num_blocks, inode->i_indirect);	
		fs->sb->sb_free_blocks++;
	}
	if (wanted_blocks < VSFS_NUM_DIRECT){
		truncate_down_dir(path,size);
	}

	return ;
}

int minimum(int x, int y){
	if (x > y){return y;}
	else {return x;}
}

/**
 * Change the size of a file.
 *
 * Implements the truncate() system call. Supports both extending and shrinking.
 * If the file is extended, the new uninitialized range at the end must be
 * filled with zeros.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *   EFBIG   write would exceed the maximum file size. 
 *
 * @param path  path to the file to set the size.
 * @param size  new file size in bytes.
 * @return      0 on success; -errno on error.
 */
static int vsfs_truncate(const char *path, off_t size)
{
	fs_ctx *fs = get_fs();

	vsfs_ino_t ino;
	path_lookup(path, &ino);
	if (clock_gettime(CLOCK_REALTIME, &(fs->itable[ino].i_mtime)) != 0) {
		perror("clock_gettime");
		return -1;
	}

	int wanted_blocks = div_round_up(size, VSFS_BLOCK_SIZE);
    int curr_blocks = fs->itable[ino].i_blocks;
    if (wanted_blocks > 1029){return -EFBIG;}
	if ((int)fs->sb->sb_free_blocks < (wanted_blocks-curr_blocks)){return -ENOSPC;}

	if (wanted_blocks > curr_blocks){
		bool first_indirect = false;
		(void) first_indirect;

		if (curr_blocks <= VSFS_NUM_DIRECT) {
			int add = wanted_blocks - curr_blocks;
			int alloc_dir = minimum(VSFS_NUM_DIRECT - curr_blocks, add);
			first_indirect = true;

			truncate_up_dir(path, alloc_dir);
		}
		int alloc_indir = wanted_blocks - (int)fs->itable[ino].i_blocks;
		if (alloc_indir > 0){
			truncate_up_indir(path, alloc_indir, first_indirect);
		}
		fs->itable[ino].i_size = size;
	} else if (wanted_blocks > curr_blocks){
		fs->itable[ino].i_size = size;
		return 0;}
	else {
		if (wanted_blocks < VSFS_NUM_DIRECT){
			if (curr_blocks > VSFS_NUM_DIRECT){ 
				truncate_down_indir(path,size);	
			}
			truncate_down_dir(path,size);
			fs->itable[ino].i_size = size;
		} else {
			truncate_down_indir(path, size);
			fs->itable[ino].i_size = size;
		}
	}
	// submitted!!
	return 0;
}


/**
 * Read data from a file.
 *
 * Implements the pread() system call. Must return exactly the number of bytes
 * requested except on EOF (end of file). Reads from file ranges that have not
 * been written to must return ranges filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path    path to the file to read from.
 * @param buf     pointer to the buffer that receives the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to read from.
 * @param fi      unused.
 * @return        number of bytes read on success; 0 if offset is beyond EOF;
 *                -errno on error.
 */
static int vsfs_read(const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
	(void)fi;// unused
	fs_ctx *fs = get_fs();
	vsfs_ino_t ino;
	path_lookup(path, &ino);

	vsfs_blk_t block;
	int offset_block = offset/VSFS_BLOCK_SIZE;
	long int end_of_read = offset+size;
	long int filesize = fs->itable[ino].i_size;
	if (offset>filesize){return 0;}
	if (end_of_read>filesize){
		size = filesize-offset;
	}
	if (offset_block<VSFS_NUM_DIRECT) {block = fs->itable[ino].i_direct[offset_block];}
	else {
		vsfs_blk_t* indirects= (vsfs_blk_t*)(fs->image+(fs->itable[ino].i_indirect*VSFS_BLOCK_SIZE));
		block = indirects[offset_block-VSFS_NUM_DIRECT];
	}
	char* written = (char*)(fs->image + (VSFS_BLOCK_SIZE*block));
	memcpy(buf,written+(offset%VSFS_BLOCK_SIZE),size);
	if (clock_gettime(CLOCK_REALTIME, &(fs->itable[VSFS_ROOT_INO].i_mtime)) != 0) {
		perror("clock_gettime");
		return -1;
	}
	return size;
	
	(void)buf;
	(void)size;
	(void)offset;
	(void)fs;
	return 0;
}


/**
 * Write data to a file.
 *
 * Implements the pwrite() system call. Must return exactly the number of bytes
 * requested except on error. If the offset is beyond EOF (end of file), the
 * file must be extended. If the write creates a "hole" of uninitialized data,
 * the new uninitialized range must filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *   EFBIG   write would exceed the maximum file size 
 *
 * @param path    path to the file to write to.
 * @param buf     pointer to the buffer containing the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to write to.
 * @param fi      unused.
 * @return        number of bytes written on success; -errno on error.
 */
static int vsfs_write(const char *path, const char *buf, size_t size,
                      off_t offset, struct fuse_file_info *fi)
{
	(void)fi;// unused
	(void)buf;
	(void)offset;
	(void)size;
	
	fs_ctx *fs = get_fs();
	vsfs_ino_t ino;
	path_lookup(path, &ino);

	if ((1029 * VSFS_BLOCK_SIZE)<offset+size){return -EFBIG;}

	// int wanted_blocks = div_round_up(size+offset, VSFS_BLOCK_SIZE);
    // int curr_blocks = fs->itable[ino].i_blocks;
	if ((offset + size)>fs->itable[ino].i_size){vsfs_truncate(path, offset+size);}
	if (offset == (off_t)(fs->itable[ino].i_size)){
		fs->itable[ino].i_size += size;
		}
	// offset+size/4096-> get block num from direct or indirect
	
	//vsfs_blk_t last_blk = div_round_up(offset,VSFS_BLOCK_SIZE);
	vsfs_blk_t last_blk = offset/VSFS_BLOCK_SIZE;
	vsfs_blk_t blk_num;
	char* blk;

	if (last_blk<VSFS_NUM_DIRECT){blk_num = fs->itable[ino].i_direct[last_blk];}
	else {
		vsfs_blk_t* indirect_table = (vsfs_blk_t*)(fs->image+(fs->itable[ino].i_indirect*VSFS_BLOCK_SIZE));
		blk_num = indirect_table[last_blk-VSFS_NUM_DIRECT];
	}
	blk = (char*)(fs->image+(blk_num*VSFS_BLOCK_SIZE));
	memcpy(blk + (offset % VSFS_BLOCK_SIZE), buf, size);
	if (clock_gettime(CLOCK_REALTIME, &(fs->itable[ino].i_mtime)) != 0) {
		perror("clock_gettime");
		return -1;}
	return size;
	//SUBMITTED! 
}


static struct fuse_operations vsfs_ops = {
	.destroy  = vsfs_destroy,
	.statfs   = vsfs_statfs,
	.getattr  = vsfs_getattr,
	.readdir  = vsfs_readdir,
	.create   = vsfs_create,
	.unlink   = vsfs_unlink,
	.utimens  = vsfs_utimens,
	.truncate = vsfs_truncate,
	.read     = vsfs_read,
	.write    = vsfs_write,
};

int main(int argc, char *argv[])
{
	vsfs_opts opts = {0};// defaults are all 0
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	if (!vsfs_opt_parse(&args, &opts)) return 1;

	fs_ctx fs = {0};
	if (!vsfs_init(&fs, &opts)) {
		fprintf(stderr, "Failed to mount the file system\n");
		return 1;
	}

	return fuse_main(args.argc, args.argv, &vsfs_ops, &fs);
}
