# Fuse File System
Implements a version of the Very Simple File System **VSFS**, whilst using FUSE to interact with the file system. FUSE allows us to implement a file system in user space by implementing the callback functions that the libfuse library will call. VSFS file system will operate on a disk image file. A disk image is simply an ordinary file that holds the content of a disk partition or storage device. 

`mkfs.c` formats an empty disk into a VSFS file system (compiled into the `mkfs.vsfs` executable). This part of the assignment does not need FUSE at all. 

`vsfs.c` implements FUSE functions to list the `root directory`, and to `create`, `remove`, `read`, `write`, and `resize` files, as well as get `status` information about files, directories, or the overall file system.


* VSFS file systems are always small enough that they can be entirely mmap'd into the vsfs process's virtual address space. The underlying operating system will handle all write-back of dirty pages to the vsfs disk image. 
* If the file system crashes, the disk image may be inconsistent. In this case our enforces no crash, but does not make any special effort to maintain crash consistency.
* There is a flat namespace. All files are located in the root directory and there are no subdirectories. All paths are absolute (they all start with '/'). If you see a path that is not absolute, or that has more than one component, you can return an error.

<br/>`mkfs.c` - contains the program to format your disk image. Accesses parts of the file system after using mmap() to map the entire disk image into the process virtual address space. Initializes the superblock and create an empty root directory and

`vsfs.h` - contains the data structure definitions and constants needed for the file system. 

`vsfs.c` - contains the program used to mount the file system. This includes the callback functions that will implement the underlying file system operations. The FUSE library, the kernel, and the user-space tools used to access the file system all rely on these return codes for correctness of operation.

<br/>`vsfs` has simple layout
```
 *   Block 0: superblock
 *   Block 1: inode bitmap
 *   Block 2: data bitmap
 *   Block 3: start of inode table
 *   First data block after inode table
```


## mkfs.c
Initializes the superblock and create an empty root directory. Which holds the following information to track the initialized filesystem in `vsfs.h`:
```c
typedef struct vsfs_superblock {	
	uint64_t   sb_magic;       /* Must match VSFS_MAGIC. */
	uint64_t   sb_size;        /* File system size in bytes. */
	uint32_t   sb_num_inodes;  /* Total number of inodes (set by mkfs) */
	uint32_t   sb_free_inodes; /* Number of available inodes */ 
	vsfs_blk_t sb_num_blocks;  /* File system size in blocks */
	vsfs_blk_t sb_free_blocks; /* Number of available blocks in file sys */
	vsfs_blk_t sb_data_region; /* First block after inode table */ 
} vsfs_superblock;
```

Initializes inode bitmap `ib_map` in memory (write to disk happens at munmap) by setting all bits to 1, then using `bitmap_init` function to clear the bits for the given number of inodes in the file system.

Initializes data bitmap in memory (write to disk happens at munmap). First set all bits to 1, then use bitmap_init to clear the bits for the given number of blocks in the file system and marks the first 3 blocks (superblock, inode bitmap, data bitmap) allocated. Then it calculates size of inode table and mark inode table blocks allocated.

Once this is done the root dir is initialized by marking root dir inode allocated in inode bitmap and initializing the fields in the root dir inode. Then proceeds to create '.' and '..' entries in root dir data block.
```c
root_entries[0].ino = 0;
strcpy(root_entries[0].name,".");
root_entries[0].name[1] = '\0';
root_entries[1].ino = 0;
strcpy(root_entries[1].name,"..");
root_entries[1].name[2] = '\0';
```
	
Finally initializes other dir entries in block to invalid / unused state. Since 0 is a valid inode, use `VSFS_INO_MAX` to indicate invalid.
```c
int x = VSFS_BLOCK_SIZE/sizeof(vsfs_dentry);
for (int i = 2; i<x; i++){
  root_entries[i].ino = VSFS_INO_MAX;
  root_entries[i].name[0] = '\0';
```

## vsfs.c
Implements the following system calls:

<br/>`truncate()` Supporting both extending and shrinking. If the file is extended, the new uninitialized range at the end must be filled with zeros. 
<br/> `pread()` returns exactly the number of bytes requested except on EOF (end of file). Reads from file ranges that have not been written to and return ranges filled with zeros. Assumes byte range from offset to offset + size is contained within a single block.
<br/>`pwrite()` returns exactly the number of bytes requested except on error. If the offset is beyond EOF (end of file), the file is extended. If the write creates a "hole" of uninitialized data, the new uninitialized range must filled with zeros. Assumes that the byte range from offset to offset + size is contained within a single block.
<br/>`getaddr()`
<br/>`readdir()`
<br/>`open()`
<br/>`create()`
<br/>`unlink()`
<br/>`utimensat()`
<br/>`statvfs()`
<br/>`lstat()`


## Additional File Info

`fs_ctx.h` and `fs_ctx.c` - The `fs_ctx` struct contains runtime state of your mounted file system. All global variables go in this struct instead. We have cached some useful global state in this structure already (e.g. pointers to superblock, bitmaps, and inode table).

<br/>`map.h` and `map.c` - contain the `map_file()` function used by `vsfs` and `mkfs.vsfs` to map the image file into memory and determine its size. 

<br/>`options.h` and `options.c` - contain the code to parse command line arguments for the `vsfs` program. 

<br/>`util.h` - contains some handy functions:
```
is_powerof2(x) - returns true if x is a power of two.
is_aligned(x, alignment) - returns true if x is a multiple of alignment (which must be a power of 2).
align_up(x, alignment) - returns the next multiple of alignment that is greater than or equal to x.
div_round_up(x, y) - returns the integer ceiling of x divided by y.
```
<br/>`bitmap.h` and `bitmap.c` - contain code to initialize bitmaps, and to allocate or free items tracked by the bitmaps. Used to allocate and free inodes and data blocks. The `bitmap_alloc` function can be slow, since it always starts the search for a 0 bit from the start of the bitmap. 
