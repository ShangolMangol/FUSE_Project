#!/bin/bash

# Usage: ./flip_bytes.sh <file> <start_offset> <end_offset>
# All bytes in [start_offset, end_offset] inclusive will be bitwise inverted.

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 <file> <start_offset> <end_offset>"
    exit 1
fi

FILE="$1"
START="$2"
END="$3"

if [ ! -f "$FILE" ]; then
    echo "Error: File not found: $FILE"
    exit 1
fi

if [ "$START" -gt "$END" ]; then
    echo "Error: start_offset must be <= end_offset"
    exit 1
fi

FILESIZE=$(stat -c%s "$FILE")
if [ "$END" -ge "$FILESIZE" ]; then
    echo "Error: end_offset is beyond end of file (file size: $FILESIZE bytes)"
    exit 1
fi

for (( i = START; i <= END; i++ )); do
    BYTE_HEX=$(dd if="$FILE" bs=1 count=1 skip="$i" 2>/dev/null | xxd -p)
    if [ -z "$BYTE_HEX" ]; then
        echo "Warning: failed to read byte at offset $i"
        continue
    fi
    BYTE_DEC=$(( 0x$BYTE_HEX ))
    FLIPPED_DEC=$(( BYTE_DEC ^ 0xFF ))  # flip all 8 bits
    FLIPPED_HEX=$(printf "%02x" $FLIPPED_DEC)
    printf "$FLIPPED_HEX" | xxd -r -p | dd of="$FILE" bs=1 seek="$i" conv=notrunc status=none
done

echo "Flipped all bits in bytes from offset $START to $END in $FILE"
