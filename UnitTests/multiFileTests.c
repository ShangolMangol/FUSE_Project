#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <assert.h>
#include <errno.h>

#define MOUNT_DIR "./mnt"
#define FILE1 MOUNT_DIR "/file1.txt"
#define FILE2 MOUNT_DIR "/file2.txt"
#define FILE3 MOUNT_DIR "/file3.txt"
#define FILE2_RENAMED MOUNT_DIR "/file2_renamed.txt"
#define DIR1 MOUNT_DIR "/dir1"
#define DIR2 MOUNT_DIR "/dir2"
#define DIR2_RENAMED MOUNT_DIR "/dir2_renamed"
#define TEXT1 "FileOneContents"
#define TEXT2 "FileTwoData"
#define TEXT3 "ThirdFile"

void write_file(const char *path, const char *text) {
    int fd = open(path, O_CREAT | O_WRONLY, 0644);
    assert(fd >= 0);
    assert(write(fd, text, strlen(text)) == (ssize_t)strlen(text));
    close(fd);
}

void test_create_multiple_files() {
    write_file(FILE1, TEXT1);
    write_file(FILE2, TEXT2);
    write_file(FILE3, TEXT3);
    printf("[PASS] Created multiple files\n");
}

void test_read_verify_contents() {
    char buf[64] = {0};

    int fd = open(FILE1, O_RDONLY); assert(fd >= 0);
    read(fd, buf, sizeof(buf)); close(fd);
    assert(strncmp(buf, TEXT1, strlen(TEXT1)) == 0);

    memset(buf, 0, sizeof(buf));
    fd = open(FILE2, O_RDONLY); assert(fd >= 0);
    read(fd, buf, sizeof(buf)); close(fd);
    assert(strncmp(buf, TEXT2, strlen(TEXT2)) == 0);

    printf("[PASS] Read and verified contents of files\n");
}

void test_truncate_files() {
    int fd = open(FILE3, O_RDWR); assert(fd >= 0);
    assert(ftruncate(fd, 4) == 0);  // Truncate to "Thir"
    char buf[16] = {0};
    lseek(fd, 0, SEEK_SET);
    read(fd, buf, sizeof(buf));
    assert(strncmp(buf, "Thir", 4) == 0);
    close(fd);
    printf("[PASS] Truncated file\n");
}

void test_readdir_contains_files() {
    DIR *dir = opendir(MOUNT_DIR); assert(dir);
    int found1 = 0, found2 = 0, found3 = 0;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, "file1.txt") == 0) found1 = 1;
        if (strcmp(de->d_name, "file2.txt") == 0) found2 = 1;
        if (strcmp(de->d_name, "file3.txt") == 0) found3 = 1;
    }
    closedir(dir);
    assert(found1 && found2 && found3);
    printf("[PASS] readdir contains all files\n");
}

void test_rename_file() {
    assert(rename(FILE2, FILE2_RENAMED) == 0);
    assert(access(FILE2_RENAMED, F_OK) == 0);
    printf("[PASS] Renamed file\n");
}

void test_mkdir_rmdir_nested() {
    assert(mkdir(DIR1, 0755) == 0);
    assert(mkdir(DIR2, 0755) == 0);
    assert(rename(DIR2, DIR2_RENAMED) == 0);

    struct stat st;
    assert(stat(DIR1, &st) == 0 && S_ISDIR(st.st_mode));
    assert(stat(DIR2_RENAMED, &st) == 0 && S_ISDIR(st.st_mode));

    assert(rmdir(DIR1) == 0);
    assert(rmdir(DIR2_RENAMED) == 0);
    printf("[PASS] mkdir, rename dir, rmdir\n");
}

void test_cleanup() {
    assert(unlink(FILE1) == 0);
    assert(unlink(FILE2_RENAMED) == 0);
    assert(unlink(FILE3) == 0);
    printf("[PASS] Cleaned up files\n");
}

int main() {
    printf("🔍 Running advanced FUSE tests on mount: %s\n", MOUNT_DIR);
    test_create_multiple_files();
    test_read_verify_contents();
    test_truncate_files();
    test_readdir_contains_files();
    test_rename_file();
    test_mkdir_rmdir_nested();
    test_cleanup();
    printf("✅ All advanced tests passed!\n");
    return 0;
}
