/**************************************************************
* Class:  CSC-415-01 Fall 2021
* Names: Jasmine Stapleton-Hart 
*	Arianna Yuan 
*	Nathaniel Miller 
* Student IDs:
* 	921356953
*	920898911
*	922024360
* GitHub Name: arianna-y
* 
* Group Name: Vile System
* Project: Basic File System
*
* File: fsInit.c
*
* Description: Main driver for file system assignment.
*
* This file is where you will start and initialize your system
*
**************************************************************/

#define	_GNU_SOURCE
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>

#include "fsLow.h"
#include "mfs.h"

#define DE_TYPE_UNUSED 		0
#define DE_TYPE_DIRECTORY 	1
#define DE_TYPE_FILE 		2

#define DE_NAME_MAXLEN 		20

#define DIRMAX_ENTRIES		64

typedef struct directoryEntry
{
	// string to hold name (limited to 20 characters)
	char name[DE_NAME_MAXLEN];

	// type of directory:
	// 0 is unused
	// 1 is directory
	// 2 is file
	int type;

	// large integers to hold to location (address) and size
	uint64_t location;
	uint64_t size;

	// c time type to hold dates of creation, latest modification,
	// latest viewing
	time_t lastModified;
	time_t lastOpened;
	time_t dateCreated;
} directoryEntry;

#define CWDMAX_LEN	4096

uint64_t allocateFreeBlocks(uint64_t numberOfBlocks);
int writeBlock(void *buffer, uint64_t blockPosition);

int initRootDirectory(uint64_t blockSize)
{
	/* compute size of initial root directory */
	const int initialNumberOfEntries = DIRMAX_ENTRIES;
	uint64_t sizeRootDirectory = initialNumberOfEntries * sizeof(directoryEntry);
	// fprintf(stderr, "Size of directoryEntry: %ld\n", sizeof(directoryEntry));

	/* allocate memory for initializing root directory */
	uint64_t numRootDirectoryBlocks = (sizeRootDirectory + blockSize - 1) / blockSize;
	sizeRootDirectory = numRootDirectoryBlocks * blockSize;
	directoryEntry * entries = calloc(numRootDirectoryBlocks, blockSize);


	/* fill in entries in root directory */
	for (int i = 0; i < initialNumberOfEntries; i++)
	{
		memset(entries[i].name, 0, sizeof(entries[i].name));
		entries[i].type = DE_TYPE_UNUSED;
		entries[i].location = 0;
		entries[i].size = 0;
		entries[i].lastModified = 0;
		entries[i].lastOpened = 0;
		entries[i].dateCreated = 0;
	}

	/* allocate blocks for storing root directory */
	uint64_t nbrBlkOfRootDirectory = (sizeRootDirectory + blockSize - 1) / blockSize;
	// fprintf(stderr, "Blocks allocated for root directory: %ld\n", nbrBlkOfRootDirectory);
	uint64_t startBlock = allocateFreeBlocks(nbrBlkOfRootDirectory);

	/**
	 * fill first and second entries
	 */

	/* 1st entry */
	entries[0].name[0] = '.';
	entries[0].type = DE_TYPE_DIRECTORY;
	entries[0].location = startBlock;
	entries[0].size = sizeRootDirectory;
	entries[0].dateCreated = time(NULL);
	entries[0].lastModified = entries[0].dateCreated;
	entries[0].lastOpened = entries[0].dateCreated;

	/* 2nd entry has the same content as 1st except name is '..' */
	memcpy(&entries[1], &entries[0], sizeof(directoryEntry));
	entries[1].name[1] = '.';

	/* write root directory to blocks */
	char * buffer = (char *) entries;
	for (int i = 0; i < nbrBlkOfRootDirectory; i++) {
		writeBlock(buffer, startBlock+ i);
		buffer += blockSize;
	}

	/* clean up */
	free(entries);

	/* return startBlock of root entry */
	return startBlock;
}

typedef struct vcb
{
	// signature to check if vcb has been formated yet
	long sig;
	// number of blocks in volume
	int numBlocks;
	// size of each block
	int blockSize;
	int numLBAPerBlock;
	// number of free blocks available
	int freeBlockCount;
	// block number of first free block available
	int nextFreeBlock;
	// where root dir starts
	int rootDirStart;
} vcb;

struct vcb fsVCB;

void writeVCB(void)
{
	if (sizeof(struct vcb) > MINBLOCKSIZE) {
		fprintf(stderr, "ERROR(%s): struct vcb is too big\n", __func__);
		return;
	}

	char *buffer = calloc(1, MINBLOCKSIZE);
	memcpy(buffer, &fsVCB, sizeof(struct vcb));
	LBAwrite(buffer, 1, 0);
	free(buffer);
}

int readBlock(void *buffer, uint blockPosition)
{
	return LBAread(buffer, fsVCB.numLBAPerBlock, blockPosition * fsVCB.numLBAPerBlock);
}

int writeBlock(void *buffer, uint64_t blockPosition)
{
	return LBAwrite(buffer, fsVCB.numLBAPerBlock, blockPosition * fsVCB.numLBAPerBlock);
}

uint32_t *bufFAT = NULL;
int posBufFAT = -1;

uint32_t getFATEntry(int blockNumber)
{
	// compute byte offset of FAT entry
	int offsetEntry = blockNumber * 4;

	// FAT starts from the second block
	int position = offsetEntry / fsVCB.blockSize + 1;

	// allocate buffer (if not allocated yet)
	if (bufFAT == NULL) {
		bufFAT = malloc(fsVCB.blockSize);
		memset(bufFAT, 0, fsVCB.blockSize);
	}

	// read LBA if not buffered
	if (posBufFAT != position) {
		readBlock(bufFAT, position);
		posBufFAT = position;
	}

	offsetEntry -= (position - 1) * fsVCB.blockSize;
	return bufFAT[offsetEntry / 4];
}

void setFATEntry(int blockNumber, uint32_t val)
{
	// compute byte offset of FAT ENTRY
	int offsetEntry = blockNumber * 4;

	// FAT starts from the second block
	int position = offsetEntry / fsVCB.blockSize + 1;

	// allocate buffer (if not allocated yet)
	if (bufFAT == NULL) {
		bufFAT = malloc(fsVCB.blockSize);
		memset(bufFAT, 0, fsVCB.blockSize);
	}
	// read LBA if not buffered
	if (posBufFAT != position) {
		readBlock(bufFAT, position);
		posBufFAT = position;
	}

	offsetEntry -= (position - 1) * fsVCB.blockSize;
	bufFAT[offsetEntry / 4] = val;
	writeBlock(bufFAT, position);
}

uint64_t allocateFreeBlocks(uint64_t numberOfBlock)
{
	uint64_t allocatedBlocks = 0;

	uint64_t startBlock = fsVCB.nextFreeBlock;
	allocatedBlocks++;
	if (getFATEntry(startBlock) != 0) {
		fprintf(stderr, "ERROR(%s): incorrect nextFreeBlock\n", __func__);
		exit(1);
	}

	uint64_t currBlock = startBlock;
	uint64_t nextBlock = currBlock + 1;
	while (allocatedBlocks < numberOfBlock) {
		// find next free block
		while (getFATEntry(nextBlock) != 0) {
			nextBlock++;
		}
		if (nextBlock == 0xFFFFFFFF) {
			fprintf(stderr, "ERROR(%s): no free space\n", __func__);
			exit(1);
		}

		// chain next block to current block
		setFATEntry(currBlock, nextBlock);

		// update for further finding
		currBlock = nextBlock;
		nextBlock = currBlock + 1;
		allocatedBlocks++;
	}

	// mark end of chain
	setFATEntry(currBlock, 0xFFFFFFFF);

	// find next free block
	nextBlock = currBlock + 1;
	while (getFATEntry(nextBlock) != 0) {
		nextBlock++;
	}
	if (nextBlock == 0xFFFFFFFF) {
		fprintf(stderr, "ERROR(%s): no free space\n", __func__);
		exit(1);
	}
	fsVCB.nextFreeBlock = nextBlock;
	writeVCB();

	if (startBlock == 0) {
		fprintf(stderr, "ERROR: allocated blocks shall not start from position 0\n");
		exit(1);
	}
	return startBlock;
}

void freeAllocatedBlocks(uint64_t startBlock)
{
	if (startBlock == 0) {
		fprintf(stderr, "ERROR(%s): startBlock shall not be 0\n", __func__);
		return;
	}

	uint64_t currentBlock = startBlock;
	uint64_t nextBlock = getFATEntry(currentBlock);
	while (nextBlock != 0xFFFFFFFF) {
		setFATEntry(currentBlock, 0);
		currentBlock = nextBlock;
		nextBlock = getFATEntry(currentBlock);
	}
	setFATEntry(currentBlock, 0);

	// update nextFreeBlock in VCB
	if (startBlock < fsVCB.nextFreeBlock) {
		fsVCB.nextFreeBlock = startBlock;
		writeVCB();
	}
}

fat_file_blockinfo * fat_get_file_blockinfo(int startBlockNumber)
{
	uint32_t nextBlock = getFATEntry(startBlockNumber);
	if (nextBlock == 0) {
		fprintf(stderr, "ERROR(%s): not an allocated FAT entry\n", __func__);
		return NULL;
	}

	fat_file_blockinfo * bi = calloc(1, sizeof(fat_file_blockinfo));
	bi->block_size = fsVCB.blockSize;
	bi->table_blocknumbers = calloc(1, sizeof(int));
	bi->table_blocknumbers[0] = startBlockNumber;
	bi->total_blocks = 1;

	while (nextBlock != 0xFFFFFFFF) {
		bi->table_blocknumbers = reallocarray(bi->table_blocknumbers, 
											  bi->total_blocks + 1,
											  sizeof(int));
		bi->table_blocknumbers[bi->total_blocks] = nextBlock;
        bi->total_blocks++;

		nextBlock = getFATEntry(nextBlock);
	}

	return bi;
}

int fat_add_block(fat_file_blockinfo *bi)
{
	uint32_t newBlock = allocateFreeBlocks(1);
	bi->table_blocknumbers = reallocarray(bi->table_blocknumbers,
	                                      bi->total_blocks + 1,
										  sizeof(int));
	bi->table_blocknumbers[bi->total_blocks] = newBlock;
	if (bi->total_blocks > 0) {
		setFATEntry(bi->table_blocknumbers[bi->total_blocks - 1], newBlock);
	}
	bi->total_blocks++;
	return 0;
}

// int returned is block number where dir starts
int initDirectory(directoryEntry *parent)
{
	// if NULL is passed in then its init the root dir
	// malloc mem to hold DE arr
	// allocate blocks to hold DE arr
	// init each DE to a known state (avail)
	// init . dir
	// init .. dir
	// LBAwrite all blocks of root dir
	// free alloc'd mem
	// return block num for start of dir
	return 1;
}

int initFileSystem(uint64_t numberOfBlocks, uint64_t blockSize)
{
	printf("Initializing File System with %ld blocks with a block size of %ld\n", numberOfBlocks, blockSize);
	/* TODO: Add any code you need to initialize your file system. */

	struct vcb *buffer = malloc(MINBLOCKSIZE);

	// read the first block to check the signature.
	LBAread(buffer, 1, 0);

	// if it does not match, vcb needs to be formatted
	if (buffer->sig != 0x4E415445)
	{
		printf("No valid VCB, need to format filesystem\n");

		// initialize VCB volume data
		fsVCB.numBlocks = numberOfBlocks;
		fsVCB.blockSize = blockSize;
		fsVCB.numLBAPerBlock = blockSize / MINBLOCKSIZE;
		fsVCB.sig = 0x4E415445;

		// initialize the FAT
		// number of blocks required for size of table
		uint64_t numBlocksFAT = ((numberOfBlocks * 4) + (blockSize - 1)) / blockSize;
		char * FATBuffer = malloc(blockSize);
		memset(FATBuffer, 0x00, blockSize);

		for (int i = 1; i < numBlocksFAT; i++) {
			// at initialization, FAT is completely free
			writeBlock(FATBuffer, i);
		}
		free(FATBuffer);
		FATBuffer = NULL;

		// Mark the already  used blocks
		// 		block 0: VCB
		// 		block 1 ~ (numBlocksFAT - 1): FAT
		for (int i = 0; i < numBlocksFAT; i++) {
			setFATEntry(i, 0xFFFFFFFF);
		}

		fsVCB.freeBlockCount = fsVCB.numBlocks;
		fsVCB.freeBlockCount -= 1; // VCB
		fsVCB.freeBlockCount -= numBlocksFAT; // FAT

		fsVCB.nextFreeBlock = numBlocksFAT;

		// initialize the root directory
		fsVCB.rootDirStart = initRootDirectory(blockSize);
		
		// finish formatting by writing VCB to block 0
		memset(buffer, 0, MINBLOCKSIZE);
		memcpy(buffer, &fsVCB, sizeof(struct vcb));
		LBAwrite(buffer, 1, 0);
	}
	else 
	{
		memcpy(&fsVCB, buffer, sizeof(struct vcb));
	}
	free(buffer);

	fs_setcwd("/");
	return 0;
}

void exitFileSystem()
{
	printf("System exiting\n");

	// free buffer for FAT
	if (bufFAT != NULL) {
		free(bufFAT);
		bufFAT = NULL;
		posBufFAT = -1;
	}
}

//
// Filesystem interfaces
//

char fsCurrWorkDir[CWDMAX_LEN];
fdDir *fsFdDirOpened = NULL;

typedef struct {
	char *path;
	char *name;
} pathnameInfo;

fdDir * _fs_opendir(const char *pathname);
int _fs_closedir(fdDir *dirp);

void fs_free_pathname_info(pathnameInfo *info)
{
	if (info->path != NULL) {
		free(info->path);
		info->path = NULL;
	}
	if (info->name != NULL) {
		free(info->name);
		info->name = NULL;
	}
}

pathnameInfo fs_parse_pathname(char *pathname)
{
	// printf("DBG(%s) pathname=%s\n", __func__, pathname);
	pathnameInfo info;
	info.path = strdup(pathname);
	info.name = NULL;

	// root directory
	if (strcmp(pathname, "/") == 0) {
		return info;
	}

	// remove trailing '/'
	if (info.path[strlen(info.path) - 1] == '/') {
		info.path[strlen(info.path) - 1] = 0;
	}

	// split pathname by right-most '/'
	char *p = strrchr(info.path, '/');
	if (p != NULL) {
		p[0] = 0;
		info.name = strdup(p+1);
	}
	else {
		// filename only
		info.name = info.path;
		info.path = NULL;
	}

	// printf("DBG(%s) path=%s name=%s\n", __func__, info.path, info.name);
	return info;
}

fdDir * fs_load_dirdata(uint64_t startLocationLBA)
{
	fdDir *dirData = calloc(1, sizeof(fdDir));
	dirData->directoryStartLocation = startLocationLBA;
	dirData->dirEntryPosition = 0;
	dirData->d_reclen = sizeof(directoryEntry);

	// read directory entries
	size_t sizeDirectory = sizeof(directoryEntry) * DIRMAX_ENTRIES;
	int numDirectoryBlocks = (sizeDirectory + fsVCB.blockSize - 1) / fsVCB.blockSize;
	dirData->entries = calloc(numDirectoryBlocks, fsVCB.blockSize);
	LBAread(dirData->entries, numDirectoryBlocks * fsVCB.numLBAPerBlock,
			dirData->directoryStartLocation);

	return dirData;
}

void fs_store_dirdata(fdDir *dir)
{
	uint64_t sizeDirectory = DIRMAX_ENTRIES * sizeof(directoryEntry);
	uint64_t numDirectoryBlocks = (sizeDirectory + fsVCB.blockSize - 1) / fsVCB.blockSize;
	LBAwrite(dir->entries, numDirectoryBlocks * fsVCB.numLBAPerBlock,
			 dir->directoryStartLocation);
}

// Key directory functions

int fs_mkdir(const char *pathname, mode_t mode)
{
	fdDir *parentDir;
	if (strchr(pathname, '/') == NULL) {
		parentDir = _fs_opendir(fsCurrWorkDir);
	}
	else {
		return -1;
	}

	directoryEntry *parentEntries = parentDir->entries;

	// find duplicate name in parent directory
	for (int i = 0; i < DIRMAX_ENTRIES; i++) {
		if (strcmp(parentEntries[i].name, pathname) == 0) {
			fprintf(stderr, "ERROR(%s): %s already exists\n", __func__, pathname);
			return -1;
		}
	}

	// find unused entries in parent directory
	directoryEntry *newEntry = NULL;
	for (int i = 0; i < DIRMAX_ENTRIES; i++) {
		if (parentEntries[i].type == DE_TYPE_UNUSED) {
			newEntry = &parentEntries[i];
			break;
		}
	}
	if (newEntry == NULL) {
		return -1;
	}

	// compute size of directory
	uint64_t sizeDirectory = DIRMAX_ENTRIES * sizeof(directoryEntry);

	// allocated memory for directory
	uint64_t numDirectoryBlocks = (sizeDirectory + fsVCB.blockSize - 1) / fsVCB.blockSize;
	sizeDirectory = numDirectoryBlocks * fsVCB.blockSize;
	directoryEntry *entries = calloc(numDirectoryBlocks, fsVCB.blockSize);

	// fill entries in directory
	for (int i = 0; i < DIRMAX_ENTRIES; i++) {
		memset(entries[i].name, 0, sizeof(entries[i].name));
		entries[i].type = DE_TYPE_UNUSED;
		entries[i].location = 0;
		entries[i].size = 0;
		entries[i].lastModified = 0;
		entries[i].lastOpened = 0;
		entries[i].dateCreated = 0;
	}

	// allocate blocks for storing directory
	uint64_t startBlock = allocateFreeBlocks(numDirectoryBlocks);

	//
	// fill first and second entries
	//

	// 1st entry
	entries[0].name[0] = '.';
	entries[0].type = DE_TYPE_DIRECTORY;
	entries[0].location = startBlock;
	entries[0].size = sizeDirectory;
	entries[0].dateCreated = time(NULL);
	entries[0].lastModified = entries[0].dateCreated;
	entries[0].lastOpened = entries[0].dateCreated;

	// 2nd entry
	memcpy(&entries[1], &entries[0], sizeof(directoryEntry));
	entries[1].name[1] = '.';
	// point to parent location
	entries[1].location = parentEntries[0].location;

	// write directory
	char *buffer = (char *) entries;
	for (int i = 0; i < numDirectoryBlocks; i++) {
		writeBlock(buffer, startBlock + i);
		buffer += fsVCB.blockSize;
	}
	free(entries);

	//
	// update parent directory
	//

	strncpy(newEntry->name, pathname, DE_NAME_MAXLEN);
	newEntry->type = DE_TYPE_DIRECTORY;
	newEntry->location = startBlock;
	newEntry->size = sizeDirectory;
	newEntry->dateCreated = time(NULL);
	newEntry->lastModified = newEntry->dateCreated;
	newEntry->lastOpened = newEntry->dateCreated;

	LBAwrite(parentDir->entries, numDirectoryBlocks * fsVCB.numLBAPerBlock,
			parentDir->directoryStartLocation);

	fs_closedir(parentDir);

	return(0);
}

int fs_rmdir(const char *pathname)
{
	// basically the same as fs_delete()
	// needs to check if empty
	// it is empty if it only has . and ..
	if (strchr(pathname, '/') != NULL) {
		return -1;
	}

	// open current working directory
	fdDir *parentDir = _fs_opendir(fsCurrWorkDir);
	if (parentDir == NULL) {
		return -1;
	}

	// find directory to remove
	directoryEntry *entries = parentDir->entries;
	directoryEntry *entryToRemove = NULL;
	for (int i = 2; i < DIRMAX_ENTRIES; i++) {
		if (entries[i].type == DE_TYPE_UNUSED) {
			continue;
		}
		if (strncmp(entries[i].name, pathname, sizeof(entries[i].name)) == 0) {
			entryToRemove = &entries[i];
			break;
		}
	}
	if (entryToRemove == NULL) {
		fprintf(stderr, "ERROR: cannot find \"%s\"\n", pathname);
		fs_closedir(parentDir);
		return -1;
	}
	
	// check whether directory is empty
	fdDir *dirData = fs_load_dirdata(entryToRemove->location * fsVCB.numLBAPerBlock);
	if (dirData == NULL) {
		fs_closedir(parentDir);
	}
	entries = dirData->entries;
	for (int i = 2; i < DIRMAX_ENTRIES; i++) {
		if (entries[i].type != DE_TYPE_UNUSED) {
			// return -1 if any entry (except . and ..) is used
			fprintf(stderr, "ERROR:\"%s\" is not empty\n", pathname);
			fs_closedir(parentDir);
			fs_closedir(dirData);
			return -1;
		}
	}

	// free allocated blocks and clear entry
	freeAllocatedBlocks(entryToRemove->location);
	memset(entryToRemove, 0, sizeof(directoryEntry));

	// store directory data
	fs_store_dirdata(parentDir);

	fs_closedir(parentDir);
	fs_closedir(dirData);
	return 0;
}

// Directory iteration functions

directoryEntry *fs_find_entry_atdir(fdDir *dirData, const char *name)
{
	directoryEntry *entries = dirData->entries;
	directoryEntry *subEntry = NULL;
	for (int i = 0; i < DIRMAX_ENTRIES; i++) {
		if (strncmp(entries[i].name, name, DE_NAME_MAXLEN) == 0) {
			subEntry = &entries[i];
			break;
		}
	}
	return subEntry;
}

fdDir * fs_opendirat(fdDir *dirData, const char *pathname)
{
	// directoryEntry *entries = dirData->entries;
	// directoryEntry *subEntry = NULL;
	// for (int i = 0; i < DIRMAX_ENTRIES; i++) {
	// 	if (strncmp(entries[i].name, pathname, DE_NAME_MAXLEN) == 0) {
	// 		if (entries[i].type == DE_TYPE_DIRECTORY) {
	// 			subEntry = &entries[i];
	// 			break;
	// 		}
	// 	}
	// }
	directoryEntry *entry = fs_find_entry_atdir(dirData, pathname);
	if (entry == NULL) {
		return NULL;
	}
	if (entry->type != DE_TYPE_DIRECTORY) {
		return NULL;
	}
	return fs_load_dirdata(entry->location * fsVCB.numLBAPerBlock);
}

fdDir * _fs_opendir(const char *pathname)
{
	// printf("DBG(%s): pathname=%s\n", __func__, pathname);

	// remove trailing '/'
	char *dir_path = strdup(pathname);
	if (dir_path[strlen(dir_path) - 1] == '/') {
		dir_path[strlen(dir_path) - 1] = 0;
	}
	if (dir_path[0] == 0) {
		dir_path[0] = '/';
		dir_path[1] = 0;
	}
	// printf("dir_path=%s\n", dir_path);

	// root directory
	if (strcmp(dir_path, "/") == 0) {
		// root directory
		free(dir_path);
		return fs_load_dirdata(fsVCB.rootDirStart * fsVCB.numLBAPerBlock);
	}

	fdDir *dirData = NULL;
	if (dir_path[0] == '/') {
		// absolute path
		dirData = _fs_opendir("/");
	}
	else {
		// relative path
		dirData = _fs_opendir(fsCurrWorkDir);
	}

	char *saveptr;
	char *s = strtok_r(dir_path, "/", &saveptr);
	// printf("s=%s\n", s);
	while (s != NULL) {
		fdDir *newDirData = fs_opendirat(dirData, s);
		_fs_closedir(dirData);
		if (newDirData == NULL) {
			free(dir_path);
			return NULL;
		}
		dirData = newDirData;
		s = strtok_r(NULL, "/", &saveptr);
	}
	free(dir_path);
	return dirData;
}

fdDir * fs_opendir(const char *pathname)
{
	fdDir *dirData = _fs_opendir(pathname);
	fsFdDirOpened = dirData;
	return dirData;
}

struct fs_diriteminfo *fs_readdir(fdDir *dirp)
{
	struct fs_diriteminfo *item = &dirp->itemInfo;
    memset(item, 0, sizeof(struct fs_diriteminfo));

	directoryEntry *entries = dirp->entries;
	for (int i = dirp->dirEntryPosition; i < DIRMAX_ENTRIES; i++) {
		// continue if unused
		if (entries[i].type == DE_TYPE_UNUSED) {
			// next position for next fs_readdir
			dirp->dirEntryPosition++;
			continue;
		}

		// copy information
		strncpy(item->d_name, entries[i].name, DE_NAME_MAXLEN);
		switch (entries[i].type) {
			case DE_TYPE_DIRECTORY:
				item->fileType = FT_DIRECTORY;
				break;
			case DE_TYPE_FILE:
				item->fileType = FT_REGFILE;
				break;
			default:
				fprintf(stderr, "ERROR(%s): unknown file type %d\n", __func__, entries[i].type);
				break;
		}
		item->startLocationLBA = entries[i].location * fsVCB.numLBAPerBlock;
		item->size = entries[i].size;

		// next position for next fs_readdir
		dirp->dirEntryPosition++;
		return item;
	}

	return NULL;
}

int _fs_closedir(fdDir *dirp)
{
	// free all the stuff from open
	free(dirp->entries);
	free(dirp);
	return(0);
}

int fs_closedir(fdDir *dirp)
{
	fsFdDirOpened = NULL;
	return _fs_closedir(dirp);
}

// Misc directory functions

char * fs_getcwd(char *pathname, size_t size)
{
	if ((strlen(fsCurrWorkDir) + 1) >= size) {
		return NULL;
	}
	strcpy(pathname, fsCurrWorkDir);
	return pathname;
}

int fs_setcwd(char *pathname)
{
	int ret;

	// printf("%s: pathname=%s\n", __func__, pathname);
	if (pathname == NULL) {
		return -1;
	}

	// set to root directory
	if (strcmp(pathname, "/") == 0) {
		strncpy(fsCurrWorkDir, "/", CWDMAX_LEN - 1);
		return 0;
	}

	// remove trailing '/'
	char *dir_path = strdup(pathname);
	if (dir_path[strlen(dir_path) - 1] == '/') {
		dir_path[strlen(dir_path) - 1] = 0;
	}

	// do nothing since '.' means current directory
	if (strcmp(dir_path, ".") == 0) {
		free(dir_path);
		return 0;
	}

	// change to parent directory of current directory
	if (strcmp(dir_path, "..") == 0) {
		if (strcmp(fsCurrWorkDir, "/") == 0) {
			// parent of root is still root
			free(dir_path);
			return 0;
		}
		else {
			char *p = strrchr(fsCurrWorkDir, '/');
			if (p != fsCurrWorkDir) {
				p[0] = 0;
			} else {
				p[1] = 0;
			}
			free(dir_path);
			return 0;
		}
	}

	char *savedCWD = strdup(fsCurrWorkDir);

	// 1-level related to current directory
	if (strchr(dir_path, '/') == NULL) {
		if ((strlen(fsCurrWorkDir) + strlen(dir_path) + 2) > CWDMAX_LEN) {
			fprintf(stderr, "ERROR(%s): path is too long\n", __func__);
		}
		if (strcmp(fsCurrWorkDir, "/") != 0) {
			strcat(fsCurrWorkDir, "/");
		}
		strcat(fsCurrWorkDir, dir_path);
		fdDir *dirData = _fs_opendir(fsCurrWorkDir);
		if (dirData == NULL) {
			strcpy(fsCurrWorkDir, savedCWD);
			free(savedCWD);
			free(dir_path);
			return -1;
		} 
		else {
			fs_closedir(dirData);
			free(savedCWD);
			free(dir_path);
			return 0;
		}
	}

	// multi-level related to current directory
	if (dir_path[0] == '/') {
		fs_setcwd("/");
	}
	char *saveptr;
	char *s = strtok_r(dir_path, "/", &saveptr);
	while (s != NULL) {
		// printf("DBG(%s) s=%s\n", __func__, s);
		ret = fs_setcwd(s);
		if (ret != 0) {
			strcpy(fsCurrWorkDir, savedCWD);
			break;
		}
		s = strtok_r(NULL, "/", &saveptr);
	}
	free(savedCWD);
	free(dir_path);
	return ret;
}

int fs_isFile(char * filename)
{
	// printf("DBG(%s): path=%s\n", __func__, filename);
	if (filename == NULL) {
		return 0;
	}
	if (strlen(filename) == 0) {
		return 0;
	}

	pathnameInfo info = fs_parse_pathname(filename);
	if (info.name == NULL) {
		fs_free_pathname_info(&info);
		return 0;
	}

	fdDir *dirData;
	if (info.path == NULL) {
		dirData = _fs_opendir(fsCurrWorkDir);
	}
	else {
		dirData = _fs_opendir(info.path);
	}
	if (dirData == NULL) {
		fs_free_pathname_info(&info);
		return 0;
	}

	directoryEntry *entry = fs_find_entry_atdir(dirData, info.name);
	if (entry == NULL) {
		fs_closedir(dirData);
		fs_free_pathname_info(&info);
		return 0;
	}
	fs_free_pathname_info(&info);

	int ret;
	if (entry->type == DE_TYPE_FILE) {
		ret = 1;
	}
	else {
		ret = 0;
	}

	fs_closedir(dirData);
	return ret;
}

int fs_isDir(char *pathname)
{
	// printf("DBG(%s): path=%s\n", __func__, pathname);
	if (strcmp(pathname, ".") == 0) {
		return 1;
	}
	if (strcmp(pathname, "..") == 0) {
		return 1;
	}

	fdDir *dirData = _fs_opendir(pathname);
	if (dirData == NULL) {
		return 0;
	}
	else {
		_fs_closedir(dirData);
		return 1;
	}
}

int fs_delete(char *filename)
{
	//removes a file
    //free the block
    //write it back to disk

	// fprintf(stderr, "ERROR(%s): unsupported\n", __func__);
	// return -1;
	fdDir *dir = _fs_opendir(".");
	if (dir == NULL) {
		return -1;
	}

	directoryEntry *entry = fs_find_entry_atdir(dir, filename);
	if (entry == NULL) {
		fs_closedir(dir);
		return -1;
	}

	entry->type = DE_TYPE_UNUSED;
	freeAllocatedBlocks(entry->location);

	// store directory data
	fs_store_dirdata(dir);

	fs_closedir(dir);
	return 0;
}

struct fs_diriteminfo *fs_create(fdDir *dir, char * filename)
{
	directoryEntry *entries = dir->entries;

	// find duplicate name in directory
	for (int i = 0; i < DIRMAX_ENTRIES; i++) {
		if (strcmp(entries[i].name, filename) == 0
			&& entries[i].type != DE_TYPE_UNUSED) {
			fprintf(stderr, "ERROR(%s): %s already exists\n", __func__, filename);
			return NULL;
		}
	}

	// find unused entries in directory
	directoryEntry * newEntry = NULL;
	for (int i = 0; i < DIRMAX_ENTRIES; i++) {
		if (entries[i].type == DE_TYPE_UNUSED) {
			newEntry = &entries[i];
			break;
		}
	}
	if (newEntry == NULL) {
		return NULL;
	}

	// fill entry
	memset(newEntry->name, 0, sizeof(newEntry->name));
	strncpy(newEntry->name, filename, sizeof(newEntry->name));
	newEntry->type = DE_TYPE_FILE;
	newEntry->location = allocateFreeBlocks(1);
	newEntry->size = 0;
	newEntry->dateCreated = time(NULL);
	newEntry->lastModified = newEntry->dateCreated;
	newEntry->lastOpened = newEntry->dateCreated;

	// write directory
	fs_store_dirdata(dir);

	// file diriteminfo
	struct fs_diriteminfo * di = &(dir->itemInfo);
	memset(di, 0, sizeof(struct fs_diriteminfo));
	strncpy(di->d_name, newEntry->name, sizeof(di->d_name));
	di->d_name[sizeof(newEntry->name)] = 0;
	di->fileType = FT_REGFILE;
	di->startLocationLBA = newEntry->location * fsVCB.numLBAPerBlock;
	di->size = newEntry->size;

	return di;
}

int fs_set_fileSize(fdDir * dir, char * filename, int size)
{
	directoryEntry *entry = fs_find_entry_atdir(dir, filename);
	if (entry == NULL) {
		return -1;
	}

	entry->size = size;

	// write directory
	fs_store_dirdata(dir);
}

int fs_rename(char * src, char * dest)
{
	// sanity check
	if (strcmp(src, dest) == 0) {
		fprintf(stderr, "%s: '%s' and '%s' are the same file\n", __func__, src, dest);
		return -1;
	}

	// open current directory
	fdDir *dir = _fs_opendir(".");
	if (dir == NULL) {
		return -1;
	}

	// find entry of source
	directoryEntry *srcEntry = fs_find_entry_atdir(dir, src);
	if (srcEntry == NULL) {
		fprintf(stderr, "%s: src '%s' does not exist\n", __func__, src);
		_fs_closedir(dir);
		return -1;
	}

	// check entry of destination
	directoryEntry *destEntry = fs_find_entry_atdir(dir, dest);
	if (destEntry != NULL) {
		fprintf(stderr, "%s: dest '%s' already exists\n", __func__, src);
		_fs_closedir(dir);
		return -1;
	}

	// change name of source entry
	strncpy(srcEntry->name, dest, sizeof(srcEntry->name));

	// write directory
	fs_store_dirdata(dir);

	_fs_closedir(dir);
}

int fs_stat(const char *path, struct fs_stat *buf)
{
	// printf("%s: path=%s\n", __func__, path);
	char *new_path = strdup(path);

	if (fsFdDirOpened != NULL) {
		// fdDir recently opened
		directoryEntry *entry = fs_find_entry_atdir(fsFdDirOpened, path);
		if (entry == NULL) {
			// not found
			fprintf(stderr, "ERROR(%s): cannot find \"%s\"\n", __func__, path);
			return -1;
		}
		if (entry->type == DE_TYPE_FILE) {
			// fill status of file
			buf->st_size = entry->size;
			buf->st_blksize = fsVCB.blockSize;
			buf->st_blocks = (entry->size + 512 - 1)/512;
			buf->st_accesstime = entry->lastOpened;
			buf->st_modtime = entry->lastModified;
			buf->st_createtime = entry->dateCreated;
			free(new_path);
			return 0;
		}
		else if (entry->type == DE_TYPE_DIRECTORY) {
			// fill status of directory
			buf->st_size = entry->size;
			buf->st_blksize = fsVCB.blockSize;
			buf->st_blocks = (entry->size + 512 - 1)/512;
			buf->st_accesstime = entry->lastOpened;
			buf->st_modtime = entry->lastModified;
			buf->st_createtime = entry->dateCreated;
			free(new_path);
			return 0;
		}
		else {
			fprintf(stderr, "ERROR(%s): unknown type\n", __func__);
			free(new_path);
			return -1;
		}
	}

	if (fs_isFile(new_path)) {
		// try to open directory
		fdDir *dirData = _fs_opendir(path);
		if (dirData == NULL) {
			free(new_path);
			return -1;
		}

		// fill status from opened directory
		directoryEntry *entries = dirData->entries;
		buf->st_size = entries[0].size;
		buf->st_blksize = fsVCB.blockSize;
		buf->st_blocks = (entries[0].size + 512 - 1)/512;
		buf->st_accesstime = entries[0].lastOpened;
		buf->st_modtime = entries[0].lastModified;
		buf->st_createtime = entries[0].dateCreated;

		fs_closedir(dirData);
		free(new_path);
		return 0;
	}
	if (fs_isDir(new_path)) {
		// try to open directory
		fdDir *dirData = _fs_opendir(path);
		if (dirData == NULL) {
			free(new_path);
			return -1;
		}

		// fill status from opened directory
		directoryEntry *entries = dirData->entries;
		buf->st_size = entries[0].size;
		buf->st_blksize = fsVCB.blockSize;
		buf->st_blocks = (entries[0].size + 512 - 1)/512;
		buf->st_accesstime = entries[0].lastOpened;
		buf->st_modtime = entries[0].lastModified;
		buf->st_createtime = entries[0].dateCreated;

		fs_closedir(dirData);
		free(new_path);
		return 0;
	}

	free(new_path);
	return -1;
}
