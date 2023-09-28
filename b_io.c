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
* File: b_io.c
*
* Description: Basic File System - Key File I/O Operations
*
**************************************************************/
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>			// for malloc
#include <string.h>			// for memcpy
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "b_io.h"
#include "fsLow.h"
#include "mfs.h"

#define MAXFCBS 20
#define B_CHUNK_SIZE 512

// This is the form of the structure returned by GetFileInfo
typedef struct fileInfo {
    char fileName[64];      // filename
    int fileSize;           // file size in bytes
    int location;           // starting lba (block number) for the file data
    int blockSize;
    fat_file_blockinfo *blockInfo;

    fdDir *dir;
} fileInfo;

typedef struct fileBlockInfo {
    int blockNumber;
    int blockOffset;
    char blockData[B_CHUNK_SIZE];
} fileBlockInfo;

typedef struct b_fcb
	{
	/** TODO add all the information you need in the file control block **/
    fileInfo * fi;	//holds the low level systems file info
    fileBlockInfo * blockInfo;
    char * buf;		//holds the open file buffer
	int index;		//holds the current position in the buffer
	int buflen;		//holds how many valid bytes are in the buffer

    uint64_t currPosition;      // current position

    char *blockBuffer;
    int bufferedBlockNumber;
	} b_fcb;
	
b_fcb fcbArray[MAXFCBS];

int startup = 0;	//Indicates that this has not been initialized

fileInfo * GetFileInfo (char * filename, int flags)
{
    // open current directory
    fdDir * curDir = fs_opendir(".");
    if (curDir == NULL) {
        fprintf(stderr, "ERROR: cannot open current directory\n");
        exit(1);
        return NULL;
    }

    // find file and open it
    struct fs_diriteminfo * di;
    di = fs_readdir(curDir);
    while (di != NULL) {
        /* skips entries for current and parent directories */
        if ((strcmp(di->d_name, ".") == 0)
            || (strcmp(di->d_name, "..") == 0)) {
            di = fs_readdir(curDir);
            continue;
        }
        if (strcmp(di->d_name, filename) == 0) {
            break;
        }
        di = fs_readdir(curDir);
    }

    // create if cannot find
    if (di == NULL && (flags & O_CREAT)) {
        di = fs_create(curDir, filename);
    }

    fileInfo * fi = NULL;
    if (di != NULL) {
        fi = calloc(1, sizeof(fileInfo));
        strncpy(fi->fileName, di->d_name, sizeof(fi->fileName) - 1);
        fi->fileSize = di->size;
        fi->location = di->startLocationLBA;
        fi->blockInfo = fat_get_file_blockinfo(fi->location);
        fi->dir = curDir;
    }

    return fi;
}

//Method to initialize our file system
void b_init ()
{
    if (startup)
        return;			//already initialized

    //init fcbArray to all free
    for (int i = 0; i < MAXFCBS; i++)
    {
        fcbArray[i].fi = NULL; //indicates a free fcbArray
    }

    startup = 1;
}

//Method to get a free File Control Block FCB element
b_io_fd b_getFCB ()
{
    for (int i = 0; i < MAXFCBS; i++)
    {
        if (fcbArray[i].fi == NULL)
        {
            fcbArray[i].fi = (fileInfo *)-2; // used but not assigned
            return i;		//Not thread safe but okay for this project
        }
    }
    return (-1);  //all in use
}
	
// Interface to open a buffered file
// Modification of interface for this assignment, flags match the Linux flags for open
// O_RDONLY, O_WRONLY, or O_RDWR
b_io_fd b_open (char * filename, int flags)
// flags ignored
{
    if (startup == 0) b_init();                                   //Initialize our system

    fileInfo * info = GetFileInfo(filename, flags);        // get file info, return distinct negative numbers on errors

    if(info == NULL) return -2;                                  // check that this file exists before allocating fd
    b_io_fd result = b_getFCB(filename);
    if (result < 0) return -3;

    // fprintf(stderr, "DEBUG(%s) fileSize=%d\n", __func__, info->fileSize);

    fcbArray[result].fi = info;
    fcbArray[result].currPosition = 0;
    fileBlockInfo * tempBlockInfo = malloc(sizeof(fileBlockInfo));
    fcbArray[result].blockInfo = tempBlockInfo;
    tempBlockInfo -> blockNumber = -1;
    tempBlockInfo -> blockOffset = B_CHUNK_SIZE;

    fcbArray[result].blockBuffer = malloc(info->blockInfo->block_size);
    fcbArray[result].bufferedBlockNumber = -1;

    return result;
}

// Interface to seek function	
int b_seek (b_io_fd fd, off_t offset, int whence)
	{
	if (startup == 0) b_init();  //Initialize our system

	// check that fd is between 0 and (MAXFCBS-1)
	if ((fd < 0) || (fd >= MAXFCBS))
		{
		return (-1); 					//invalid file descriptor
		}
		
		
	return (0); //Change this
	}



// Interface to write function	
int b_write (b_io_fd fd, char * buffer, int count)
{
	if (startup == 0) b_init();  //Initialize our system

	// check that fd is between 0 and (MAXFCBS-1)
	if ((fd < 0) || (fd >= MAXFCBS))
	{
		return (-1); 					//invalid file descriptor
	}

    b_fcb *fcb = &(fcbArray[fd]);
    fat_file_blockinfo *blockInfo = fcb->fi->blockInfo;
    int blockSize = fcb->fi->blockInfo->block_size;

    // compute sizes of part 1/2/3
    int offsetPart1, offsetPart2, offsetPart3;      // offsets (of Part 1, 2, 3) aligned to block boundary
    int sizePart1, sizePart2, sizePart3;

    offsetPart1 = (fcb->currPosition / blockSize) * blockSize;
    if (offsetPart1 == fcb->currPosition) {
        offsetPart2 = offsetPart1;
        sizePart1 = 0;
    }
    else {
        offsetPart2 = offsetPart1 + blockSize;
        sizePart1 = blockSize - (fcb->currPosition - offsetPart1);
        if (sizePart1 > count) {
            sizePart1 = count;
        }
    }
    count -= sizePart1;

    sizePart2 = (count / blockSize) * blockSize;
    count -= sizePart2;

    offsetPart3 = offsetPart2 + sizePart2;
    sizePart3 = count;
    count -= sizePart3;

    // printf("Offset/Size: [0x%x/%d] [0x%x/%d] [0x%x/%d]\n",
    //       offsetPart1, sizePart1,
    //       offsetPart2, sizePart2,
    //       offsetPart3, sizePart3);

    // allocate block buffer
    char *blockBuffer = fcb->blockBuffer;

    // Part 1: first block
    if (sizePart1 > 0) {
        while (blockInfo->total_blocks < (offsetPart1 / blockSize) + 1) {
            fat_add_block(blockInfo);
        }
        int blockNumber = blockInfo->table_blocknumbers[offsetPart1 / blockSize];
        if (fcb->bufferedBlockNumber != blockNumber) {
            LBAread(blockBuffer, 1, blockNumber);
        }
        memcpy(blockBuffer + (fcb->currPosition - offsetPart1), buffer, sizePart1);
        LBAwrite(blockBuffer, 1, blockNumber);

        // record block number of buffered block data
        fcb->bufferedBlockNumber = blockNumber;
    }

    // Part 2: multiple of blocks
    for (int i = 0; i < (offsetPart3 - offsetPart2) / blockSize; i++) {
        while (blockInfo->total_blocks < (offsetPart2 / blockSize) + i + 1) {
            fat_add_block(blockInfo);
        }
        int blockNumber = blockInfo->table_blocknumbers[(offsetPart2 / blockSize) + i];
        memcpy(blockBuffer, buffer + sizePart1 + i * blockSize, blockSize);
        LBAwrite(blockBuffer, 1, blockNumber);

        // record block number of buffered block data
        fcb->bufferedBlockNumber = blockNumber;
    }

    // Part 3: last block
    if (sizePart3 > 0) {
        while (blockInfo->total_blocks < (offsetPart3 / blockSize) + 1) {
            fat_add_block(blockInfo);
        }
        int blockNumber = blockInfo->table_blocknumbers[(offsetPart3 / blockSize)];
        memset(blockBuffer, 0, blockSize);
        memcpy(blockBuffer, buffer + sizePart1 + sizePart2, sizePart3);
        LBAwrite(blockBuffer, 1, blockNumber);

        // record block number of buffered block data
        fcb->bufferedBlockNumber = blockNumber;
    }

    fcb->currPosition += sizePart1 + sizePart2 + sizePart3;
    if (fcb->currPosition > fcb->fi->fileSize) {
        fcb->fi->fileSize = fcb->currPosition;
    }

    return (sizePart1 + sizePart2 + sizePart3);
}


// Interface to read a buffer

// Filling the callers request is broken into three parts
// Part 1 is what can be filled from the current buffer, which may or may not be enough
// Part 2 is after using what was left in our buffer there is still 1 or more block
//        size chunks needed to fill the callers request.  This represents the number of
//        bytes in multiples of the blocksize.
// Part 3 is a value less than blocksize which is what remains to copy to the callers buffer
//        after fulfilling part 1 and part 2.  This would always be filled from a refill 
//        of our buffer.
//  +-------------+------------------------------------------------+--------+
//  |             |                                                |        |
//  | filled from |  filled direct in multiples of the block size  | filled |
//  | existing    |                                                | from   |
//  | buffer      |                                                |refilled|
//  |             |                                                | buffer |
//  |             |                                                |        |
//  | Part1       |  Part 2                                        | Part3  |
//  +-------------+------------------------------------------------+--------+


int readNextBlock(b_io_fd fd)
{
    // check that file descriptor is valid
    if ((fd < 0) || (fd >= MAXFCBS) || (fcbArray[fd].fi == NULL))
    {
        return (-1);
    }
    fileBlockInfo * tempBlockInfo = fcbArray[fd].blockInfo;
    fileInfo * tempInfo = fcbArray[fd].fi;

    int calcFileSize = B_CHUNK_SIZE * (tempBlockInfo->blockNumber + 1);
    if ( calcFileSize >= tempInfo->fileSize )
    {
        // attempted read past end of file
        return 0;
    }
    tempBlockInfo -> blockNumber ++;
    tempBlockInfo->blockOffset = 0;
    LBAread(tempBlockInfo->blockData, 1,tempBlockInfo->blockNumber);
}

int b_read (b_io_fd fd, char * buffer, int count)
{
    // Write buffered read function to return the data and # bytes read
    // You must use LBAread and you must buffer the data in B_CHUNK_SIZE byte chunks.

    if (startup == 0) b_init();  //Initialize our system

    // check that fd is between 0 and (MAXFCBS-1) and that specified FCB is actually in use
    if ((fd < 0) || (fd >= MAXFCBS) || (fcbArray[fd].fi == NULL)) { return (-1); } 			//invalid file descriptor

    b_fcb *fcb = &(fcbArray[fd]);
    fat_file_blockinfo *blockInfo = fcb->fi->blockInfo;
    int blockSize = fcb->fi->blockInfo->block_size;

    // ensure not exceed end of file
    if ((fcb->currPosition + count) > fcb->fi->fileSize) {
        count = fcb->fi->fileSize - fcb->currPosition;
    }
    if (count < 0) {
        fprintf(stderr, "ERROR(%s): incorrect position and filesize\n", __func__);
        exit(1);
    }

    // compute sizes of Part 1/2/3
    int offsetPart1, offsetPart2, offsetPart3;     // offsets (of Part 1, 2, 3) aligned to block boundary
    int sizePart1, sizePart2, sizePart3;

    offsetPart1 = (fcb->currPosition / blockSize) * blockSize;
    if (offsetPart1 == fcb->currPosition) {
        offsetPart2 = offsetPart1;
        sizePart1 = 0;
    }
    else {
        offsetPart2 = offsetPart1 + blockSize;
        sizePart1 = blockSize - (fcb->currPosition - offsetPart1);
        if (sizePart1 > count) {
            sizePart1 = count;
        }
    }
    count -= sizePart1;

    sizePart2 = (count / blockSize) * blockSize;
    count -= sizePart2;

    offsetPart3 = offsetPart2 + sizePart2;
    sizePart3 = count;
    count -= sizePart3;
    if (count != 0) {
        fprintf(stderr, "ERROR(%s): incorrect computing\n", __func__);
        exit(1);
    }

    // printf("Offset/Size: [0x%x/%d] [0x%x/%d] [0x%x/%d]\n",
    //        offsetPart1, sizePart1,
    //        offsetPart2, sizePart2,
    //        offsetPart3, sizePart3);

    // allocate block buffer
    char *blockBuffer = fcb->blockBuffer;

    //
    // read three parts
    //
    int bytesRead = 0;

    // Part 1: first block
    if (sizePart1 > 0) {
        int blockNumber = blockInfo->table_blocknumbers[offsetPart1 / blockSize];
        if (fcb->bufferedBlockNumber != blockNumber) {
            LBAread(blockBuffer, 1, blockNumber);
        }
        memcpy(buffer, blockBuffer + (fcb->currPosition - offsetPart1), sizePart1);
        bytesRead += sizePart1;

        // record block number of buffered block data
        fcb->bufferedBlockNumber = blockNumber;
    }

    // Part 2: multiple of blocks
    for (int i = 0; i < (offsetPart3 - offsetPart2) / blockSize; i++) {
        int blockNumber = blockInfo->table_blocknumbers[(offsetPart2 / blockSize) + i];
        if (fcb->bufferedBlockNumber != blockNumber) {
            LBAread(blockBuffer, 1, blockNumber);
        }
        memcpy(buffer + sizePart1 + i * blockSize, blockBuffer, blockSize);
        bytesRead += blockSize;

        // record block number of buffered block data
        fcb->bufferedBlockNumber = blockNumber;
    }
    if (bytesRead != (sizePart1 + sizePart2)) {
        fprintf(stderr, "ERROR(%s): inproper reading part2\n", __func__);
        exit(1);
    }

    // Part 3: last block
    if (sizePart3 > 0) {
        int blockNumber = blockInfo->table_blocknumbers[(offsetPart3 / blockSize)];
        if (fcb->bufferedBlockNumber != blockNumber) {
            LBAread(blockBuffer, 1, blockNumber);
        }
        memcpy(buffer + bytesRead, blockBuffer, sizePart3);
        bytesRead += sizePart3;

        // record block number of buffered block data
        fcb->bufferedBlockNumber = blockNumber;
    }

    fcb->currPosition += sizePart1 + sizePart2 + sizePart3;
    if (fcb->currPosition > fcb->fi->fileSize) {
        fcb->currPosition = fcb->fi->fileSize;
    }

    return bytesRead;

    // int bytesRead = 0;
    // uint64_t fileOffset;
    // fileBlockInfo * tempBlockInfo = fcbArray[fd].blockInfo;
    // fileInfo * tempInfo = fcbArray[fd].fi;


    // if (tempBlockInfo->blockOffset >= B_CHUNK_SIZE)             // read a new block if currently at the end of a block
    // {
    //     readNextBlock(fd);
    // }
    // fileOffset = (tempBlockInfo->blockNumber * B_CHUNK_SIZE) + tempBlockInfo->blockOffset;

    // if (tempBlockInfo->blockOffset + count <= B_CHUNK_SIZE)     // entire request in current block, no new blocks read
    // {
    //     if ((fileOffset + count) > tempInfo->fileSize)
    //     {
    //         // more bytes requested than left in the file, truncate count
    //         count = tempInfo->fileSize - fileOffset;
    //     }
    //     memcpy(buffer, (tempBlockInfo->blockData + tempBlockInfo->blockOffset), count);
    //     bytesRead += count;
    //     tempBlockInfo->blockOffset += count;
    // }

    // else                                                        // request spans multiple blocks
    // {
    //     int copyBytes = B_CHUNK_SIZE - tempBlockInfo->blockOffset;
    //     // requested to read past EOF:
    //     if ((fileOffset + copyBytes) > tempInfo->fileSize)
    //     {
    //         // more bytes requested than left in the file, truncate count
    //         copyBytes = tempInfo->fileSize - fileOffset;
    //         memcpy(buffer, (tempBlockInfo->blockData + tempBlockInfo->blockOffset), copyBytes);
    //         bytesRead += copyBytes;
    //         tempBlockInfo->blockOffset += copyBytes;
    //         return copyBytes;
    //     }
    //     // copy remainder of starting block
    //     memcpy(buffer,(tempBlockInfo->blockData + tempBlockInfo->blockOffset),copyBytes);
    //     // update buffer ptr and count and # bytes read
    //     bytesRead += copyBytes;
    //     buffer += copyBytes;
    //     count -= copyBytes;
    //     readNextBlock(fd);
    //     while (count > B_CHUNK_SIZE)                            // copy all full blocks
    //     {
    //         if ((tempBlockInfo->blockNumber + 1) * B_CHUNK_SIZE > tempInfo->fileSize)        // EOF reached
    //         {
    //             int remainder = tempInfo->fileSize - (tempBlockInfo->blockNumber * B_CHUNK_SIZE);
    //             memcpy(buffer, tempBlockInfo->blockData, remainder);
    //             bytesRead += remainder;
    //             return bytesRead;
    //         }
    //         memcpy(buffer, tempBlockInfo->blockData, B_CHUNK_SIZE);
    //         // update buffer ptr and count and # bytes read
    //         bytesRead += B_CHUNK_SIZE;
    //         buffer += B_CHUNK_SIZE;
    //         count -= B_CHUNK_SIZE;
    //         readNextBlock(fd);
    //     }
    //     // copy end of final block and update offset
    //     if ((tempBlockInfo->blockNumber * B_CHUNK_SIZE + count) > tempInfo->fileSize)       // EOF reached
    //     {
    //         int remainder = tempInfo->fileSize - (tempBlockInfo->blockNumber * B_CHUNK_SIZE);
    //         memcpy(buffer, tempBlockInfo->blockData, remainder);
    //         bytesRead += remainder;
    //         return bytesRead;
    //     }
    //     memcpy(buffer, tempBlockInfo->blockData, count);
    //     bytesRead += count;
    //     tempBlockInfo->blockOffset = count;
    // }
    // return bytesRead;
}
	
// Interface to Close the file	
// b_close frees allocated memory and places the file control block back
// into the unused pool of file control blocks.
int b_close (b_io_fd fd)
{
    // check that fd is between 0 and (MAXFCBS-1) and that specified FCB is actually in use
    if ((fd < 0) || (fd >= MAXFCBS) || (fcbArray[fd].fi == NULL))
    {
        return -1;
    }
    // release the resources!!
    if (fcbArray[fd].blockInfo != NULL)
    {
        free(fcbArray[fd].blockInfo);
    }
    if (fcbArray[fd].fi != NULL) {
        fs_set_fileSize(fcbArray[fd].fi->dir, fcbArray[fd].fi->fileName,
                        fcbArray[fd].fi->fileSize);
        fs_closedir(fcbArray[fd].fi->dir);
        if (fcbArray[fd].fi->blockInfo != NULL) {
            free(fcbArray[fd].fi->blockInfo->table_blocknumbers);
            free(fcbArray[fd].fi->blockInfo);
        }
        free(fcbArray[fd].fi);
    }
    fcbArray[fd].blockInfo = NULL;
    fcbArray[fd].fi = NULL;

    free(fcbArray[fd].blockBuffer);
}
