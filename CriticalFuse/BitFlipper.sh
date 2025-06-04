#!/bin/bash

# Usage: ./flip_bit.sh <file> <byte_offset> <bit_position>
# bit_position: 0 (LSB) to 7 (MSB)

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 <file> <byte_offset> <bit_position (0-7)>"
    exit 1
fi

FILE="$1"
OFFSET="$2"
BIT="$3"

if [ ! -f "$FILE" ]; then
    echo "Error: File not found: $FILE"
    exit 1
fi

if [ "$BIT" -lt 0 ] || [ "$BIT" -gt 7 ]; then
    echo "Error: Bit position must be between 0 and 7"
    exit 1
fi

# Read 1 byte from the offset
BYTE_HEX=$(dd if="$FILE" bs=1 count=1 skip="$OFFSET" 2>/dev/null | xxd -p)

if [ -z "$BYTE_HEX" ]; then
    echo "Error: Offset beyond end of file"
    exit 1
fi

# Convert to decimal, flip the bit, convert back to hex
BYTE_DEC=$(( 0x$BYTE_HEX ))
FLIPPED_DEC=$(( BYTE_DEC ^ (1 << BIT) ))
FLIPPED_HEX=$(printf "%02x" $FLIPPED_DEC)

# Write the modified byte back
printf "$FLIPPED_HEX" | xxd -r -p | dd of="$FILE" bs=1 seek="$OFFSET" conv=notrunc status=none

echo "Flipped bit $BIT at byte offset $OFFSET in $FILE"
