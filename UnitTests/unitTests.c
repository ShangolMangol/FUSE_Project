#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <assert.h>
#include <errno.h>

#define MOUNT_DIR "./mnt"
#define TEST_FILE MOUNT_DIR "/testfile.txt"
#define TEST_TEXT "Hello, FUSE!"

void test_create_write_read() {
    int fd = open(TEST_FILE, O_CREAT | O_WRONLY, 0644);
    assert(fd >= 0);
    ssize_t written = write(fd, TEST_TEXT, strlen(TEST_TEXT));
    assert(written == (ssize_t)strlen(TEST_TEXT));
    close(fd);

    char buf[128] = {0};
    fd = open(TEST_FILE, O_RDONLY);
    assert(fd >= 0);
    ssize_t read_bytes = read(fd, buf, sizeof(buf));
    assert(read_bytes == (ssize_t)strlen(TEST_TEXT));
    assert(strncmp(buf, TEST_TEXT, strlen(TEST_TEXT)) == 0);
    close(fd);

    printf("[PASS] create, write, read\n");
}

void test_unlink() {
    int res = unlink(TEST_FILE);
    assert(res == 0);
    assert(access(TEST_FILE, F_OK) == -1);
    printf("[PASS] unlink\n");
}

void test_mkdir_rmdir() {
    const char *dirname = MOUNT_DIR "/testdir";
    int res = mkdir(dirname, 0755);
    assert(res == 0);

    struct stat st;
    res = stat(dirname, &st);
    assert(res == 0 && S_ISDIR(st.st_mode));

    res = rmdir(dirname);
    assert(res == 0);
    printf("[PASS] mkdir, rmdir\n");
}

int main() {
    printf("Running FUSE functional tests on mount: %s\n", MOUNT_DIR);
    test_create_write_read();
    test_unlink();
    test_mkdir_rmdir();
    printf("All tests passed!\n");
    return 0;
}
