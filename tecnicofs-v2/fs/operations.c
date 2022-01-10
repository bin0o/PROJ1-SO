#include "operations.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int tfs_init() {
    state_init();

    /* create root inode */
    int root = inode_create(T_DIRECTORY);
    if (root != ROOT_DIR_INUM) {
        return -1;
    }

    return 0;
}

int tfs_destroy() {
    state_destroy();
    return 0;
}

static bool valid_pathname(char const *name) {
    return name != NULL && strlen(name) > 1 && name[0] == '/';
}

int tfs_lookup(char const *name) {
    if (!valid_pathname(name)) {
        return -1;
    }

    // skip the initial '/' character
    name++;

    return find_in_dir(ROOT_DIR_INUM, name);
}

int tfs_open(char const *name, int flags) {
    int inum;
    size_t offset;

    /* Checks if the path name is valid */
    if (!valid_pathname(name)) {
        return -1;
    }

    inum = tfs_lookup(name);
    if (inum >= 0) {
        /* The file already exists */
        inode_t *inode = inode_get(inum);
        if (inode == NULL) {
            return -1;
        }

        /* Trucate (if requested) */
        if (flags & TFS_O_TRUNC) {
            if (inode->i_size > 0) {
                int blocks = (int)inode->i_size / BLOCK_SIZE;
                for (int i = 0; i < blocks + 1; i++)
                    if (data_block_free(inode->i_data_block[i]) == -1) {
                        return -1;
                    }
                inode->i_size = 0;
            }
        }
        /* Determine initial offset */
        if (flags & TFS_O_APPEND) {
            offset = inode->i_size;
        } else {
            offset = 0;
        }
    } else if (flags & TFS_O_CREAT) {
        /* The file doesn't exist; the flags specify that it should be created*/
        /* Create inode */
        inum = inode_create(T_FILE);
        if (inum == -1) {
            return -1;
        }
        /* Add entry in the root directory */
        if (add_dir_entry(ROOT_DIR_INUM, inum, name + 1) == -1) {
            inode_delete(inum);
            return -1;
        }
        offset = 0;
    } else {
        return -1;
    }

    /* Finally, add entry to the open file table and
     * return the corresponding handle */
    return add_to_open_file_table(inum, offset);

    /* Note: for simplification, if file was created with TFS_O_CREAT and there
     * is an error adding an entry to the open file table, the file is not
     * opened but it remains created */
}

int tfs_close(int fhandle) { return remove_from_open_file_table(fhandle); }

ssize_t tfs_write(int fhandle, void const *buffer, size_t to_write) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

    /* From the open file table entry, we get the inode */
    inode_t *inode = inode_get(file->of_inumber);
    if (inode == NULL) {
        return -1;
    }
    

    size_t to_write_portion = to_write;
    int copy_to_write = (int)to_write;
    size_t written=0;
    /* Determine how many bytes to write */

    while (copy_to_write > 0) {
        to_write_portion=(size_t) copy_to_write;
        int block_index = (int)inode->i_size / BLOCK_SIZE;


        if (to_write_portion > BLOCK_SIZE) {
            to_write_portion = BLOCK_SIZE;
        }


        if (to_write_portion + file->of_offset >(block_index + 1) * BLOCK_SIZE) {
            to_write_portion =(size_t)(block_index + 1) * BLOCK_SIZE - file->of_offset;
        }

        if (inode->i_size % BLOCK_SIZE == 0) {
            /* If empty block, allocate new block */
            inode->i_data_block[block_index] = data_block_alloc();
        }

        void *block = data_block_get(inode->i_data_block[block_index]);



        int block_offset =(int) file->of_offset % BLOCK_SIZE;



        if (block == NULL) {
            return -1;
        }
        
        /* Perform the actual write */
        memcpy(block + block_offset, buffer +written, to_write_portion);
        written+=to_write_portion;

        /* The offset associated with the file handle is
         * incremented accordingly */
        
        file->of_offset += to_write_portion;

        if (file->of_offset > inode->i_size) {
            inode->i_size = file->of_offset;
        }
        copy_to_write -= (int)to_write_portion;

    }
    return (ssize_t)to_write;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

    /* From the open file table entry, we get the inode */
    inode_t *inode = inode_get(file->of_inumber);
    if (inode == NULL) {
        return -1;
    }
    /* Determine how many bytes to read */
    size_t to_read = inode->i_size - file->of_offset;
    int copy_to_read = 1;
    size_t to_read_portion = to_read;
    size_t read=0;

    if (to_read >= len) {
        copy_to_read = (int)len;
        to_read_portion = len;
        to_read = len;
    }

    while (copy_to_read > 0) {
        to_read_portion=(size_t)copy_to_read;
        int block_index = (int)file->of_offset / BLOCK_SIZE;
        void *block = data_block_get(inode->i_data_block[block_index]);
        if (block == NULL) {
            return -1;
        }

        if ((to_read_portion + file->of_offset) >(block_index + 1) * BLOCK_SIZE) {
            to_read_portion =(size_t)(block_index + 1) * BLOCK_SIZE - file->of_offset;
        }
        
        int block_offset =(int) file->of_offset % BLOCK_SIZE;
        /* Perform the actual read */
  
        memcpy(buffer+read, block + block_offset, to_read_portion);
        read+=to_read_portion;
        /* The offset associated with the file handle is
         * incremented accordingly */
        
        file->of_offset += to_read_portion;

        copy_to_read -= (int)to_read_portion;

    }

    return (ssize_t)to_read;
}
