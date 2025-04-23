"# FUSE_Project" 

Command for compiling:
gcc basicFuse.c `pkg-config fuse3 --cflags --libs` -o persistent_fs
