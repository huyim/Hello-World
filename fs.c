#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "disk.h"
#include "fs.h"


#define FAT_EOC 0xFFFF

struct SuperBlock{
	char signature[8];
	uint16_t blockTotal;
	uint16_t rootIndex;
	uint16_t startIndex;
	uint16_t dataCount;
	uint8_t fatNum;
	uint8_t padding[4079];
}__attribute__((packed));

typedef struct SuperBlock SuperBlock;

struct rootDir{
	char fileName[16];
	uint32_t fileSize;
	uint16_t firstIndex;
	uint8_t padding[10];
}__attribute__((packed));

typedef struct rootDir rootDir;

struct fileDes{
	char fileName[16];
	uint32_t fileSize;
	size_t offset;
	int blockIndex;  //this is just the block startIndex
};

typedef struct fileDes fileDes;

static SuperBlock *super;
static rootDir *root;
static uint16_t *fat;
static fileDes *table = NULL;

static int disk = 0;
static int open = 0;

int sb_checker()
{
	super = malloc(sizeof(SuperBlock));
	int status = block_read(0, super);
	if (status == -1)
	{
		return -1;
	}
	if (strncmp(super->signature, "ECS150FS",8))
	{
		return -1;
	}
	if (super->blockTotal != block_disk_count())
	{
		return -1;
	}
	if (super->dataCount != super->blockTotal - super->fatNum -2)
	{
		return -1;
	}
	if (super->rootIndex != super->fatNum + 1)
	{
		return -1;
	}
	if (super->startIndex != super->fatNum + 2)
	{
		return -1;
	}
	return 0;
}

int fs_mount(const char *diskname)
{
	int check = block_disk_open(diskname);
	if (check == -1)
	{
		return -1;
	}else
	{
		disk++;
	}

	//check and read into SuperBlock
	int check_super = sb_checker();
	if (check_super == -1)
	{
		return -1;
	}

	//read into FAT
	fat = malloc(BLOCK_SIZE * super->fatNum);

	for (int i = 0; i < super->fatNum; i++)
	{
		//do we need to copy first
		check = block_read(i + 1, fat + i * BLOCK_SIZE / 2);
		if (check == -1)
		{
			return -1;
		}
	}
	if (fat[0] != 0xFFFF)
	{		
		return -1;
	}
	//read into RootDir  128 * 32 = 4096 which is the same  as blocksize
	//0, 1, 2, 3, 4, 5
	root = malloc(128 * 96);
	check = block_read(super->fatNum + 1, root);
	if (check == -1)
	{
		return -1;
	}
	//initialze fd table
	if (table == NULL)
	{
		table = malloc(sizeof(fileDes) * FS_OPEN_MAX_COUNT);
		for (int i = 0; i < FS_OPEN_MAX_COUNT; i++)
		{
			strcpy(table[i].fileName, "");
			table[i].blockIndex = 0;
			table[i].fileSize = 0;
			table[i].offset = 0;
		}
	}
	return 0;
}

int fs_umount(void)
{
	if (disk == 0)
	{
		return -1;
	}	
	for (int i = 0; i < super->fatNum; i++)
	{
		block_write(i + 1, fat +  i * BLOCK_SIZE/ 2);
	}
	block_write(super->rootIndex, root);
	int checker = block_disk_close();
	disk--;
	if (checker == -1)
	{
		return -1;
	}
	if (super == NULL)
	{
		return -1;
	}
	free(super);
	super = NULL;
	free(root);
	root = NULL;
	free(fat);
	fat = NULL;
	return 0;
}

int fs_info(void)
{
	int countFree = 0;
	int countFile = 0;
	if (super == NULL)
	{
		return -1;
	}
	printf("FS Info:\n");
	printf("total_blk_count=%d\n",super->blockTotal);
	printf("fat_blk_count=%d\n",super->fatNum);
	printf("rdir_blk=%d\n",super->rootIndex);
	printf("data_blk=%d\n",super->startIndex);
	printf("data_blk_count=%d\n",super->dataCount);
	for(int i = 0; i < super->dataCount; i++)
	{
		if (fat[i] == 0)
		{
			countFree++;
		}
	}
	printf("fat_free_ratio=%d/%d\n", countFree, super->dataCount);
	for(int i = 0; i < FS_FILE_MAX_COUNT; i++)
	{
		if (strcmp(root[i].fileName, "") == 0)
		{
			countFile++;
		}
	}
	printf("rdir_free_ratio=%d/%d\n", countFile, FS_FILE_MAX_COUNT);
	return 0;
}

int check_create(const char *filename)
{
	if (filename == NULL)
	{
		return -1;
	}
	if (strlen(filename) > FS_FILENAME_LEN -1)
	{
		return -1;
	}
	return 0;
}

int fs_create(const char *filename)
{
	int check = check_create(filename);
	if (check == -1)
	{
		return -1;
	}

	for (int i = 0; i < FS_FILE_MAX_COUNT; i++)
	{
		//already exsits
		if (strcmp(root[i].fileName, filename) == 0)
		{
			return -1;
		}
		//find the first available entry
		if (strlen(root[i].fileName) == 0)
		{
			strcpy(root[i].fileName, filename);
			root[i].fileSize = 0;
			root[i].firstIndex = FAT_EOC;
			break;
		}
		//cant find entry in the entire root directory
		if (i == FS_FILE_MAX_COUNT - 1)
		{
			return -1;
		}
	}

	return 0;
}

int fs_delete(const char *filename)
{
	int tmpIndex;
	if (filename == NULL || root == NULL)
	{
		return -1;
	}

	//find the file in the root directory first
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++)
	{
		if (strcmp(root[i].fileName, filename) == 0)
		{
			
			int currentIndex = root[i].firstIndex;
			if (currentIndex == FAT_EOC)
			{
				strcpy(root[i].fileName, "");
				root[i].fileSize = 0;
				root[i].firstIndex = FAT_EOC;
				break;
			}
			
			while (fat[currentIndex] != FAT_EOC)
			{
				tmpIndex = fat[currentIndex];
				fat[currentIndex] = 0;
				currentIndex = tmpIndex;
			}
			strcpy(root[i].fileName, "");
			root[i].fileSize = 0;
			root[i].firstIndex = FAT_EOC;
			break;
		}

		//invalid if cant find the fileName
		if (i == FS_FILE_MAX_COUNT - 1)
		{
			return -1;
		}
	}
	return 0;

}

int fs_ls(void)
{
	if (super == NULL)
	{
		return -1;
	}
	printf("FS Ls:\n");
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++)
	{
		if (strlen(root[i].fileName) != 0)
		{
			printf("file: %s, size: %d, data_blk: %d\n", root[i].fileName, root[i].fileSize, root[i].firstIndex);
		}
	}
	return 0;
}

int fs_open(const char *filename)
{
	if (filename == NULL || open > FS_FILE_MAX_COUNT)
	{
		return -1;
	}
	open++;
	for (int i = 0; i < FS_FILE_MAX_COUNT;i++)
	{
		//find the fileName in the root directory
		if (!strcmp(root[i].fileName, filename))
		{
			for (int j = 0; j < FS_OPEN_MAX_COUNT; j++)
			{
				if (strcmp(table[j].fileName, "") == 0)
				{
					strcpy(table[j].fileName, filename);
					table[j].fileSize = root[i].fileSize;
					table[j].offset = 0;
					table[j].blockIndex = root[i].firstIndex;
					return j;
				}

				//cannot find appropriate fd
				if (j == FS_OPEN_MAX_COUNT - 1){
					return -1;
				}
			}
		}
	}
	return -1;
}

int fs_close(int fd)
{
	open --;
	if (fd < 0 || fd > FS_OPEN_MAX_COUNT)
	{
		return -1;
	}
	if (strcmp(table[fd].fileName, "") == 0)
	{
		return -1;
	}

	strcpy(table[fd].fileName, "");
	table[fd].blockIndex = 0;
	table[fd].fileSize = 0;
	table[fd].offset = 0;
	return 0;
}

int fs_stat(int fd)
{
	if (fd < 0 || fd > FS_OPEN_MAX_COUNT)
	{
		return -1;
	}
	if (strcmp(table[fd].fileName, "") == 0)
	{
		return -1;
	}

	return table[fd].fileSize;
}

int fs_lseek(int fd, size_t offset)
{
	if (fd < 0 || fd > FS_OPEN_MAX_COUNT)
	{
		return -1;
	}
	if (strcmp(table[fd].fileName, "") == 0)
	{
		return -1;
	}
	if (offset > fs_stat(fd))
	{
		return -1;
	}
	table[fd].offset = offset;
	return 0;
}

int findFreeDataBlock(void){
	for (int i = 1; i < super->dataCount; i++)
	{
		if (fat[i] == 0)
		{
			return i;
		}
	}
	return -1;
}


int getBlockIndexByOffset(size_t offset, int fd){
	int jump = offset/BLOCK_SIZE;
	int curBlockIndex = table[fd].blockIndex;
	while (jump > 0)
	{
		curBlockIndex = fat[curBlockIndex];
		jump--;
	}

	return curBlockIndex;
}

int getRootIndexByName(int fd){
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++)
	{
		//find the fileName in the root directory
		if (!strcmp(root[i].fileName, table[fd].fileName))
		{
			return i;
		}
	}
	return -1;
}

int fs_write(int fd, void *buf, size_t count)
{
		
	if (fd < 0 || fd > FS_OPEN_MAX_COUNT)
	{
		return -1;
	}
	if (strcmp(table[fd].fileName, "") == 0)
	{
		return -1;
	}

	int rootIndex = getRootIndexByName(fd);
  	// 0 1 2 3 4 5 6    7      8
	//     |   |
	//      p1   p2     p3
	//offset = 2, fileSize = 5   count = 6, 2 3 4 5 6 7 should be write to
	//we want get overwrite = 1
	// p1 is in original file,  p2 is not original file but still in valid
	// data block and p3 is overwrite which we need to allocate new block
	int overwrite = 0;
	int p1 = table[fd].fileSize - table[fd].offset; //2 3 4

	//special case, when fileSize is multiply of BLOCK_SIZE, p2 always 0
	int p2 = 0;
	if (table[fd].fileSize % BLOCK_SIZE != 0)
	{
		p2 = BLOCK_SIZE - (table[fd].fileSize % BLOCK_SIZE); // 5 6
	}
	//if we need overwrite
	if (count > p1 + p2)
	{
		overwrite = count - p1 - p2;
		count -= overwrite;
	}

	size_t bufIndex = 0;
	void* buffer = malloc(BLOCK_SIZE);
	// 0 1 2 3 4 5 6    7 8     9 10 11 12 13    14
	//     |   |
	size_t startPoint = table[fd].offset % BLOCK_SIZE;
	int curBlockIndex = getBlockIndexByOffset(table[fd].offset, fd);

	//left: read the first block
	//case 1: do not start from the first position
	if (startPoint != 0)
	{
		block_read(curBlockIndex + super->startIndex, buffer);
		//small operation : count is so less
		// 0 1 2 3 4 5 6    7 8 9 10 11 12 13    14
		//     |   |
		if (count < BLOCK_SIZE - startPoint)
		{
			memcpy(buffer + startPoint, buf + bufIndex, count);
			bufIndex += count;
			table[fd].offset += count;
			block_write(curBlockIndex + super->startIndex, buffer);
			return count;
		}else
		{ 
			int len = BLOCK_SIZE - startPoint;
			memcpy(buffer + startPoint, buf + bufIndex, len);
			bufIndex += len; 
			count -= len;
			block_write(curBlockIndex + super->startIndex, buffer);
			curBlockIndex = fat[curBlockIndex];
		}
	}

	//middle  read whole block
	while (count >= BLOCK_SIZE)
	{
		block_read(curBlockIndex + super->startIndex, buffer);
		int len = BLOCK_SIZE;
		memcpy(buffer, buf + bufIndex, len);
		bufIndex += len;
		count -= len;
		block_write(curBlockIndex + super->startIndex, buffer);
		curBlockIndex = fat[curBlockIndex];
	}

	//right  still have something left
	if (count > 0)
	{
		block_read(curBlockIndex + super->startIndex, buffer);
		memcpy(buffer, buf + bufIndex, count);
		bufIndex += count;
		block_write(curBlockIndex + super->startIndex, buffer);
		count = 0;
	}

	//now if overwrite, allocate new datablocks
	//curBlockIndex should point to new allocated datablock.
	while (overwrite > 0)
	{
		int freeIndex = findFreeDataBlock();
		int cnt = 0;
		if (overwrite >= BLOCK_SIZE)
		{
			cnt = BLOCK_SIZE;
		}else
		{
			cnt = overwrite;
		}
		//write to new data block
		memcpy(buffer, buf + bufIndex, cnt);
		block_write(freeIndex + super->startIndex, buffer);

		//point to the new data block
		if (curBlockIndex == FAT_EOC)
		{
			fat[freeIndex] = FAT_EOC;
			curBlockIndex = freeIndex;
			table[fd].blockIndex = freeIndex;
			root[rootIndex].firstIndex = freeIndex;
		}else
		{
			fat[curBlockIndex] = freeIndex;
			fat[freeIndex] = FAT_EOC;
			curBlockIndex = freeIndex;
		}


		bufIndex += cnt;
		overwrite -= cnt;
	}
	table[fd].offset += bufIndex;
	table[fd].fileSize += bufIndex;
	root[rootIndex].fileSize += bufIndex;
	return bufIndex;
}



int fs_read(int fd, void *buf, size_t count)
{

	if (fd < 0 || fd > FS_OPEN_MAX_COUNT)
	{
		return -1;
	}
	if (strcmp(table[fd].fileName, "") == 0)
	{
		return -1;
	}


	//before anything could be done, check count is greater than fileSize
	size_t remain = table[fd].fileSize - table[fd].offset;
	if (remain < count)
	{
		count = remain;
	}

	size_t bufIndex = 0;
	void* buffer = malloc(BLOCK_SIZE);
	// 0 1 2 3 4 5 6    7 8     9 10 11 12 13    14
	//     |   |
	size_t startPoint = table[fd].offset % BLOCK_SIZE;
	int curBlockIndex = getBlockIndexByOffset(table[fd].offset, fd);

	//left: read the first block
	//case 1: do not start from the first position
	if (startPoint != 0)
	{
		block_read(curBlockIndex + super->startIndex, buffer);
		//small operation : count is so less
		// 0 1 2 3 4 5 6    7 8 9 10 11 12 13    14
		//     |       |
		if (count < BLOCK_SIZE - startPoint)
		{
			memcpy(buf + bufIndex, buffer + startPoint, count);
			bufIndex += count;
			table[fd].offset += count;
			return count;
		}else
		{ //what abot count == BLOCK_SIZE - startPoint
			int len = BLOCK_SIZE - startPoint;
			memcpy(buf + bufIndex, buffer + startPoint, len);
			bufIndex += len;
			count -= len;
			curBlockIndex = fat[curBlockIndex];
		}
	}
	//middle  read whole block
	while (count >= BLOCK_SIZE)
	{
		block_read(curBlockIndex + super->startIndex, buffer);
		int len = BLOCK_SIZE;
		memcpy(buf + bufIndex, buffer, len);
		bufIndex += len;
		count -= len;
		curBlockIndex = fat[curBlockIndex];
	}
	//right  still have something left
	if (count > 0)
	{
		block_read(curBlockIndex + super->startIndex, buffer);
		memcpy(buf + bufIndex, buffer, count);
		bufIndex += count;
		count = 0;
	}
	table[fd].offset += bufIndex;
	return bufIndex;
}
