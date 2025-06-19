import jpegio as jio
import numpy as np
from PIL import Image
import os

def split_jpeg(jpeg_path):
    # Load JPEG and get DCT coefficients
    jpeg = jio.read(jpeg_path)

    ac_data = []
    dc_only = jpeg

    # Extract ACs and zero them out in a copy
    for comp in range(len(jpeg.coef_arrays)):
        coef = jpeg.coef_arrays[comp].copy()
        h, w = coef.shape[0] // 8, coef.shape[1] // 8
        blocks = coef.reshape(h, 8, w, 8).transpose(0, 2, 1, 3)
        
        for y in range(h):
            for x in range(w):
                block = blocks[y, x].flatten()
                ac = block[1:]
                ac_data.append(ac)
                block[1:] = 0
                blocks[y, x] = block.reshape(8, 8)
        
        # Store modified DC-only blocks
        dc_only.coef_arrays[comp] = blocks.transpose(0, 2, 1, 3).reshape(h*8, w*8)

    # Save critical (headers + DC only)
    jio.write(dc_only, "out.crit.jpg")

    # Save AC data separately
    np.save("out.ac.npy", np.array(ac_data, dtype=np.int16))
    print(f"✅ Saved out.crit.jpg and out.ac.npy with {len(ac_data)} blocks.")

def rebuild_jpeg(crit_path="out.crit.jpg", ac_path="out.ac.npy", out_path="reconstructed.jpg"):
    jpeg = jio.read(crit_path)
    ac_data = np.load(ac_path)

    idx = 0
    for comp in range(len(jpeg.coef_arrays)):
        coef = jpeg.coef_arrays[comp]
        h, w = coef.shape[0] // 8, coef.shape[1] // 8
        blocks = coef.reshape(h, 8, w, 8).transpose(0, 2, 1, 3)

        for y in range(h):
            for x in range(w):
                block = blocks[y, x].flatten()
                block[1:] = ac_data[idx]
                blocks[y, x] = block.reshape(8, 8)
                idx += 1

        jpeg.coef_arrays[comp] = blocks.transpose(0, 2, 1, 3).reshape(h*8, w*8)

    jio.write(jpeg, out_path)
    print(f"✅ Reconstructed JPEG written to: {out_path}")

if __name__ == "__main__":
    import sys
    if len(sys.argv) == 2 and sys.argv[1].endswith(".jpg"):
        split_jpeg(sys.argv[1])
    elif len(sys.argv) == 1:
        rebuild_jpeg()
    else:
        print("Usage:")
        print("  Split:   python jpeg_split_ac.py input.jpg")
        print("  Rebuild: python jpeg_split_ac.py")
