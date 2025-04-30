"# FUSE_Project" 

Command for compiling:
gcc basicFuse.c `pkg-config fuse3 --cflags --libs` -o persistent_fs

Command for running:
./fuse_fs ./mnt
-d -f for debugging

Command for unmounting:
fusermount3 -u ./mnt

