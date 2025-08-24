#!/usr/bin/env python3
"""
Bit Flip Analysis Script for CriticalFUSE

This script uses the BitFlipper executable to introduce random bit flips to non-critical
files and measures the structural similarity index (SSIM) by using GuetzliSplit for
splitting and merging images.

The script can work in two modes:
1. Analyze existing .ac.noncrit files in the storage folder
2. Split test images using GuetzliSplit, then analyze the .ac.noncrit files

Usage:
    python3 bit_flip_analysis.py <storage_folder> <bitflipper_path> [options]

Examples:
    # Analyze existing .ac.noncrit files
    python3 bit_flip_analysis.py ./storage ./BitFlipper --output-dir ./bitflip_results
    
    # Use test images from TestImages folder
    python3 bit_flip_analysis.py ./storage ./BitFlipper --output-dir ./bitflip_results \
        --test-images ./TestImages --use-test-images
    
    # Use only first 5 test images
    python3 bit_flip_analysis.py ./storage ./BitFlipper --output-dir ./bitflip_results \
        --test-images ./TestImages --use-test-images --max-test-images 5
    
    # Custom flip range
    python3 bit_flip_analysis.py ./storage ./BitFlipper -o ./results --flip-range 0.1 5.0
    
    # Use custom GuetzliSplit path
    python3 bit_flip_analysis.py ./storage ./BitFlipper --output-dir ./bitflip_results \
        --guetzli-split /path/to/GuetzliSplit
"""

import os
import shutil
import time
import argparse
import subprocess
from pathlib import Path
from typing import List, Dict, Tuple
import json
from datetime import datetime
import matplotlib.pyplot as plt
import numpy as np
from skimage.metrics import structural_similarity as ssim
from skimage import io
import cv2
import multiprocessing as mp
from concurrent.futures import ProcessPoolExecutor, as_completed
import pickle


class BitFlipAnalyzer:
    def __init__(self, storage_folder: str, bitflipper_path: str, output_dir: str, 
                 test_images_folder: str = None, output_file: str = None, max_test_images: int = None,
                 guetzli_split_path: str = "/usr/local/bin/GuetzliSplit", num_processes: int = None):
        """
        Initialize the bit flip analyzer.
        
        Args:
            storage_folder: Path to storage folder containing .noncrit files
            bitflipper_path: Path to BitFlipper executable
            output_dir: Path to output directory for results
            test_images_folder: Path to folder containing test images to copy to mount point
            output_file: Optional path to save results JSON
            num_processes: Number of processes to use for parallel processing
        """
        self.storage_folder = Path(storage_folder)
        self.bitflipper_path = Path(bitflipper_path)
        self.output_dir = Path(output_dir)
        self.test_images_folder = Path(test_images_folder) if test_images_folder else None
        self.output_file = output_file
        self.max_test_images = max_test_images
        self.guetzli_split_path = Path(guetzli_split_path)
        self.num_processes = num_processes or min(mp.cpu_count(), 8)  # Limit to 8 processes max
        
        # Create separate graphs directory
        self.graphs_dir = self.output_dir.parent / f"{self.output_dir.name}_graphs"
        self.graphs_dir.mkdir(parents=True, exist_ok=True)
        
        # Create temporary directories for parallel processing
        self.temp_dir = self.output_dir / "temp"
        self.temp_dir.mkdir(parents=True, exist_ok=True)
        
        self.results = {
            'timestamp': datetime.now().isoformat(),
            'storage_folder': str(self.storage_folder),
            'bitflipper_path': str(self.bitflipper_path),
            'guetzli_split_path': str(self.guetzli_split_path),
            'output_dir': str(self.output_dir),
            'test_images_folder': str(self.test_images_folder) if self.test_images_folder else None,
            'max_test_images': self.max_test_images,
            'graphs_dir': str(self.graphs_dir),
            'num_processes': self.num_processes,
            'tests': []
        }
        
        # Ensure output directory exists
        self.output_dir.mkdir(parents=True, exist_ok=True)
        
        # Verify bitflipper exists
        print(f"Checking BitFlipper at: {self.bitflipper_path}")
        print(f"BitFlipper absolute path: {self.bitflipper_path.absolute()}")
        print(f"BitFlipper exists: {self.bitflipper_path.exists()}")
        if not self.bitflipper_path.exists():
            raise FileNotFoundError(f"BitFlipper not found at {self.bitflipper_path}")
        
        # Verify GuetzliSplit executable exists
        print(f"Checking GuetzliSplit at: {self.guetzli_split_path}")
        print(f"GuetzliSplit exists: {self.guetzli_split_path.exists()}")
        if not self.guetzli_split_path.exists():
            raise FileNotFoundError(f"GuetzliSplit not found at {self.guetzli_split_path}")
        
        # Verify test images folder exists if provided
        if self.test_images_folder:
            print(f"Checking test images folder at: {self.test_images_folder}")
            print(f"Test images folder exists: {self.test_images_folder.exists()}")
            if not self.test_images_folder.exists():
                raise FileNotFoundError(f"Test images folder not found at {self.test_images_folder}")
    
    def find_noncrit_files(self) -> List[Path]:
        """Find all .ac.noncrit files in the storage folder."""
        noncrit_files = list(self.storage_folder.glob("*.ac.noncrit"))
        noncrit_files.sort()
        
        print(f"Found {len(noncrit_files)} .ac.noncrit files in {self.storage_folder}")
        return noncrit_files
    
    def split_image_with_guetzli(self, input_image: Path, output_base: Path) -> bool:
        """
        Split an image using GuetzliSplit executable.
        
        Args:
            input_image: Path to input image file
            output_base: Base path for output files (without extension)
            
        Returns:
            True if successful, False otherwise
        """
        try:
            # GuetzliSplit command: GuetzliSplit --split input_image output_base
            cmd = [str(self.guetzli_split_path), "--split", str(input_image), str(output_base)]
            
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
            
            if result.returncode != 0:
                print(f"GuetzliSplit failed: {result.stderr}")
                return False
            
            return True
            
        except subprocess.TimeoutExpired:
            print(f"GuetzliSplit timed out for {input_image}")
            return False
        except Exception as e:
            print(f"Error running GuetzliSplit: {e}")
            return False
    
    def merge_image_with_guetzli(self, crit_file: Path, output_image: Path) -> bool:
        """
        Merge .crit and .ac.noncrit files using GuetzliSplit executable.
        
        Args:
            crit_file: Path to .crit file
            output_image: Path for output merged image
            
        Returns:
            True if successful, False otherwise
        """
        try:
            # GuetzliSplit command: GuetzliSplit --merge crit_file output_image
            cmd = [str(self.guetzli_split_path), "--merge", str(crit_file), str(output_image)]
            
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
            
            if result.returncode != 0:
                print(f"GuetzliSplit merge failed: {result.stderr}")
                return False
            
            return True
            
        except subprocess.TimeoutExpired:
            print(f"GuetzliSplit merge timed out for {crit_file}")
            return False
        except Exception as e:
            print(f"Error running GuetzliSplit merge: {e}")
            return False
    
    def find_test_images(self) -> List[Path]:
        """Find all test images in the test images folder."""
        if not self.test_images_folder:
            return []
        
        # Look for common image extensions
        image_extensions = ['*.jpg', '*.jpeg']
        test_images = []
        
        for ext in image_extensions:
            test_images.extend(list(self.test_images_folder.glob(ext)))
            test_images.extend(list(self.test_images_folder.glob(ext.upper())))
        
        test_images.sort()
        print(f"Found {len(test_images)} test images in {self.test_images_folder}")
        
        # Limit the number of test images if specified
        if self.max_test_images and len(test_images) > self.max_test_images:
            test_images = test_images[:self.max_test_images]
            print(f"Limited to {self.max_test_images} test images")
        
        return test_images
    
    def run_bitflipper(self, input_file: Path, flip_percentage: float) -> bool:
        """
        Run BitFlipper executable to introduce bit flips in-place.
        
        Args:
            input_file: Path to input file (will be modified in-place)
            flip_percentage: Percentage of bits to flip (0.0 to 100.0)
            
        Returns:
            True if successful, False otherwise
        """
        try:
            # Create a backup of the original file
            backup_file = input_file.with_suffix(input_file.suffix + '.backup')
            with open(input_file, 'rb') as src, open(backup_file, 'wb') as dst:
                dst.write(src.read())
            
            # Run BitFlipper command in random mode (-r)
            bitflipper_abs_path = self.bitflipper_path.absolute()
            cmd = [str(bitflipper_abs_path), "-r", str(flip_percentage), str(input_file)]
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
            
            if result.returncode != 0:
                print(f"BitFlipper failed: {result.stderr}")
                # Restore backup
                with open(backup_file, 'rb') as src, open(input_file, 'wb') as dst:
                    dst.write(src.read())
                return False
            
            # Remove backup on success
            backup_file.unlink(missing_ok=True)
            return True
            
        except subprocess.TimeoutExpired:
            print(f"BitFlipper timed out for {input_file}")
            # Restore backup
            if backup_file.exists():
                with open(backup_file, 'rb') as src, open(input_file, 'wb') as dst:
                    dst.write(src.read())
            return False
        except Exception as e:
            print(f"Error running BitFlipper: {e}")
            # Restore backup
            if backup_file.exists():
                with open(backup_file, 'rb') as src, open(input_file, 'wb') as dst:
                    dst.write(src.read())
            return False
    
    def calculate_ssim(self, original_file: Path, modified_file: Path) -> float:
        """
        Calculate SSIM between original and modified files.
        
        Args:
            original_file: Path to original file
            modified_file: Path to modified file
            
        Returns:
            SSIM value (0.0 to 1.0, where 1.0 is identical)
        """
        try:
            # Read images
            original_img = cv2.imread(str(original_file))
            modified_img = cv2.imread(str(modified_file))
            
            if original_img is None or modified_img is None:
                print(f"Could not read images: {original_file} or {modified_file}")
                return 0.0
            
            # Convert to grayscale for SSIM calculation
            original_gray = cv2.cvtColor(original_img, cv2.COLOR_BGR2GRAY)
            modified_gray = cv2.cvtColor(modified_img, cv2.COLOR_BGR2GRAY)
            
            # Ensure same size
            if original_gray.shape != modified_gray.shape:
                # Resize modified image to match original
                modified_gray = cv2.resize(modified_gray, (original_gray.shape[1], original_gray.shape[0]))
            
            # Calculate SSIM
            ssim_value = ssim(original_gray, modified_gray)
            return ssim_value
            
        except Exception as e:
            print(f"Error calculating SSIM: {e}")
            return 0.0
    
    def process_single_test_image(self, args_tuple):
        """
        Process a single test image with all flip percentages.
        This function is designed to be called in parallel.
        
        Args:
            args_tuple: Tuple containing (test_image, flip_percentages, process_id)
            
        Returns:
            Dictionary with results for this image
        """
        test_image, flip_percentages, process_id = args_tuple
        
        print(f"[Process {process_id}] Processing: {test_image.name}")
        
        # Create process-specific temporary directory
        process_temp_dir = self.temp_dir / f"process_{process_id}"
        process_temp_dir.mkdir(exist_ok=True)
        
        # Create unique base name for split files
        base_name = f"test_{test_image.stem}_{process_id}_{int(time.time())}"
        output_base = process_temp_dir / base_name
        
        # Split the test image using GuetzliSplit
        if not self.split_image_with_guetzli(test_image, output_base):
            print(f"[Process {process_id}] Failed to split {test_image}")
            return None
        
        # Find the generated .crit and .ac.noncrit files
        crit_file = output_base.with_suffix('.jpg.crit')
        noncrit_file = output_base.with_suffix('.jpg.ac.noncrit')
        
        if not crit_file.exists() or not noncrit_file.exists():
            print(f"[Process {process_id}] Split files not found: {crit_file} or {noncrit_file}")
            return None
        
        # Create a copy of the original test image for comparison
        original_copy = process_temp_dir / f"original_{test_image.stem}{test_image.suffix}"
        with open(test_image, 'rb') as src, open(original_copy, 'wb') as dst:
            dst.write(src.read())
        
        # Create a backup of the original .ac.noncrit file to restore before each test
        original_noncrit_backup = process_temp_dir / f"original_{noncrit_file.name}"
        with open(noncrit_file, 'rb') as src, open(original_noncrit_backup, 'wb') as dst:
            dst.write(src.read())
        
        file_results = {
            'filename': test_image.name,
            'crit_file': str(crit_file),
            'noncrit_file': str(noncrit_file),
            'flip_percentages': [],
            'ssim_values': [],
            'file_size': test_image.stat().st_size,
            'original_image_path': str(original_copy),
            'modified_image_paths': []
        }
        
        for flip_pct in flip_percentages:
            # Restore the original .ac.noncrit file before each test
            with open(original_noncrit_backup, 'rb') as src, open(noncrit_file, 'wb') as dst:
                dst.write(src.read())
            
            # Apply bit flips to the .ac.noncrit file
            if not self.run_bitflipper(noncrit_file, flip_pct):
                print(f"[Process {process_id}] BitFlipper failed for {flip_pct:.2f}%")
                continue
            
            # Merge the files using GuetzliSplit to get the modified image
            merged_image = process_temp_dir / f"merged_{test_image.stem}_{flip_pct:.2f}{test_image.suffix}"
            if not self.merge_image_with_guetzli(crit_file, merged_image):
                print(f"[Process {process_id}] GuetzliSplit merge failed for {flip_pct:.2f}%")
                continue
            
            file_results['flip_percentages'].append(flip_pct)
            file_results['modified_image_paths'].append(str(merged_image))
            print(f"[Process {process_id}] Completed {flip_pct:.2f}% bit flips for {test_image.name}")
        
        # Clean up backup
        original_noncrit_backup.unlink(missing_ok=True)
        
        # Clean up split files
        crit_file.unlink(missing_ok=True)
        noncrit_file.unlink(missing_ok=True)
        
        return file_results
    
    def process_single_existing_file(self, args_tuple):
        """
        Process a single existing .ac.noncrit file with all flip percentages.
        This function is designed to be called in parallel.
        
        Args:
            args_tuple: Tuple containing (noncrit_file, flip_percentages, process_id)
            
        Returns:
            Dictionary with results for this file
        """
        noncrit_file, flip_percentages, process_id = args_tuple
        
        print(f"[Process {process_id}] Processing: {noncrit_file.name}")
        
        # Create process-specific temporary directory
        process_temp_dir = self.temp_dir / f"process_{process_id}"
        process_temp_dir.mkdir(exist_ok=True)
        
        # Find the corresponding .crit file
        crit_file = noncrit_file.with_suffix('.jpg.crit').with_name(noncrit_file.stem.replace('.ac', '') + '.jpg.crit')
        if not crit_file.exists():
            print(f"[Process {process_id}] Could not find corresponding .crit file for {noncrit_file.name}")
            return None
        
        # Create a copy of the original test image for comparison by merging the original files
        original_merged = process_temp_dir / f"original_{noncrit_file.stem.replace('.ac', '')}.jpg"
        if not self.merge_image_with_guetzli(crit_file, original_merged):
            print(f"[Process {process_id}] Failed to merge original files for comparison")
            return None
        
        # Create a backup of the original .ac.noncrit file to restore before each test
        original_noncrit_backup = process_temp_dir / f"original_{noncrit_file.name}"
        with open(noncrit_file, 'rb') as src, open(original_noncrit_backup, 'wb') as dst:
            dst.write(src.read())
        
        file_results = {
            'filename': noncrit_file.name,
            'crit_file': str(crit_file),
            'noncrit_file': str(noncrit_file),
            'flip_percentages': [],
            'ssim_values': [],
            'file_size': noncrit_file.stat().st_size,
            'original_image_path': str(original_merged),
            'modified_image_paths': []
        }
        
        for flip_pct in flip_percentages:
            # Restore the original .ac.noncrit file before each test
            with open(original_noncrit_backup, 'rb') as src, open(noncrit_file, 'wb') as dst:
                dst.write(src.read())
            
            # Apply bit flips to the .ac.noncrit file
            if not self.run_bitflipper(noncrit_file, flip_pct):
                print(f"[Process {process_id}] BitFlipper failed for {flip_pct:.2f}%")
                continue
            
            # Merge the files using GuetzliSplit to get the modified image
            modified_merged = process_temp_dir / f"modified_{noncrit_file.stem.replace('.ac', '')}_{flip_pct:.2f}.jpg"
            if not self.merge_image_with_guetzli(crit_file, modified_merged):
                print(f"[Process {process_id}] GuetzliSplit merge failed for {flip_pct:.2f}%")
                continue
            
            file_results['flip_percentages'].append(flip_pct)
            file_results['modified_image_paths'].append(str(modified_merged))
            print(f"[Process {process_id}] Completed {flip_pct:.2f}% bit flips for {noncrit_file.name}")
        
        # Clean up backup
        original_noncrit_backup.unlink(missing_ok=True)
        
        return file_results
    
    def calculate_ssim_parallel(self, args_tuple):
        """
        Calculate SSIM for a single image comparison in parallel.
        This method is self-contained to work in separate processes.
        
        Args:
            args_tuple: Tuple containing (original_path, modified_path, filename, flip_pct)
            
        Returns:
            Tuple of (filename, flip_pct, ssim_value)
        """
        original_path, modified_path, filename, flip_pct = args_tuple
        
        try:
            import cv2
            from skimage.metrics import structural_similarity as ssim
            from pathlib import Path
            
            original_path = Path(original_path)
            modified_path = Path(modified_path)
            
            if not original_path.exists() or not modified_path.exists():
                return (filename, flip_pct, 0.0)
            
            # Read images
            original_img = cv2.imread(str(original_path))
            modified_img = cv2.imread(str(modified_path))
            
            if original_img is None or modified_img is None:
                return (filename, flip_pct, 0.0)
            
            # Convert to grayscale for SSIM calculation
            original_gray = cv2.cvtColor(original_img, cv2.COLOR_BGR2GRAY)
            modified_gray = cv2.cvtColor(modified_img, cv2.COLOR_BGR2GRAY)
            
            # Ensure same size
            if original_gray.shape != modified_gray.shape:
                modified_gray = cv2.resize(modified_gray, (original_gray.shape[1], original_gray.shape[0]))
            
            # Calculate SSIM
            ssim_value = ssim(original_gray, modified_gray)
            return (filename, flip_pct, ssim_value)
            
        except Exception as e:
            print(f"Error calculating SSIM for {filename} at {flip_pct}%: {e}")
            return (filename, flip_pct, 0.0)
    
    def calculate_ssim_batch(self, results_data):
        """
        Calculate SSIM for all processed images in parallel batch.
        
        Args:
            results_data: List of result dictionaries from parallel processing
        """
        print("\n=== Calculating SSIM for all processed images in parallel ===")
        
        # Prepare all SSIM calculation tasks
        ssim_tasks = []
        for result in results_data:
            if not result or not result.get('modified_image_paths'):
                continue
            
            original_path = Path(result['original_image_path'])
            if not original_path.exists():
                print(f"Original image not found: {original_path}")
                continue
            
            for i, modified_path_str in enumerate(result['modified_image_paths']):
                modified_path = Path(modified_path_str)
                flip_pct = result['flip_percentages'][i] if i < len(result['flip_percentages']) else 0.0
                ssim_tasks.append((str(original_path), str(modified_path), result['filename'], flip_pct))
        
        if not ssim_tasks:
            print("No SSIM tasks to process")
            return
        
        print(f"Processing {len(ssim_tasks)} SSIM calculations in parallel...")
        
        # Process SSIM calculations in parallel
        ssim_results = {}
        try:
            with ProcessPoolExecutor(max_workers=self.num_processes) as executor:
                futures = [executor.submit(self.calculate_ssim_parallel, task) for task in ssim_tasks]
                
                # Collect results with progress tracking
                completed = 0
                for future in as_completed(futures):
                    try:
                        filename, flip_pct, ssim_value = future.result()
                        
                        # Group results by filename
                        if filename not in ssim_results:
                            ssim_results[filename] = {'flip_percentages': [], 'ssim_values': []}
                        
                        ssim_results[filename]['flip_percentages'].append(flip_pct)
                        ssim_results[filename]['ssim_values'].append(ssim_value)
                        
                        completed += 1
                        if completed % 10 == 0 or completed == len(ssim_tasks):
                            print(f"SSIM progress: {completed}/{len(ssim_tasks)} ({completed/len(ssim_tasks)*100:.1f}%)")
                            
                    except Exception as e:
                        print(f"Error in SSIM calculation: {e}")
                        completed += 1
        except Exception as e:
            print(f"Error in parallel SSIM processing: {e}")
            print("Falling back to sequential SSIM calculation...")
            self.calculate_ssim_batch_sequential(results_data)
            return
        
        # Update the original results with SSIM values
        for result in results_data:
            if result and result.get('filename') in ssim_results:
                ssim_data = ssim_results[result['filename']]
                # Sort by flip percentage to ensure correct order
                sorted_data = sorted(zip(ssim_data['flip_percentages'], ssim_data['ssim_values']))
                result['flip_percentages'] = [pct for pct, _ in sorted_data]
                result['ssim_values'] = [ssim for _, ssim in sorted_data]
                print(f"Calculated SSIM for {result['filename']}: {len(result['ssim_values'])} values")
            else:
                # Ensure we have SSIM values even if parallel processing failed
                if result and result.get('flip_percentages'):
                    result['ssim_values'] = [0.0] * len(result['flip_percentages'])
                    print(f"Using default SSIM values for {result['filename']}: {len(result['ssim_values'])} values")
    
    def calculate_ssim_batch_sequential(self, results_data):
        """
        Fallback sequential SSIM calculation if parallel processing fails.
        
        Args:
            results_data: List of result dictionaries from parallel processing
        """
        print("\n=== Calculating SSIM sequentially (fallback) ===")
        
        for result in results_data:
            if not result or not result.get('modified_image_paths'):
                continue
            
            original_path = Path(result['original_image_path'])
            if not original_path.exists():
                print(f"Original image not found: {original_path}")
                continue
            
            ssim_values = []
            for modified_path_str in result['modified_image_paths']:
                modified_path = Path(modified_path_str)
                if modified_path.exists():
                    ssim_value = self.calculate_ssim(original_path, modified_path)
                    ssim_values.append(ssim_value)
                else:
                    print(f"Modified image not found: {modified_path}")
                    ssim_values.append(0.0)
            
            result['ssim_values'] = ssim_values
            print(f"Calculated SSIM for {result['filename']}: {len(ssim_values)} values")
    
    def create_bit_flip_graphs(self, analysis_results: List[Dict]):
        """Create graphs showing the impact of bit flips on SSIM."""
        if not analysis_results:
            print("No data available for graph generation")
            return
        
        # Extract data for plotting
        all_flip_percentages = []
        all_ssim_values = []
        filenames = []
        
        for result in analysis_results:
            if result and result.get('flip_percentages'):
                all_flip_percentages.extend(result['flip_percentages'])
                all_ssim_values.extend(result['ssim_values'])
                filenames.extend([result['filename']] * len(result['flip_percentages']))
        
        if not all_flip_percentages:
            print("No valid data for plotting")
            return
        
        # Create figure with subplots
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 6))
        fig.suptitle('Bit Flip Impact Analysis on Non-Critical Files (FUSE)', fontsize=16, fontweight='bold')
        
        # Plot 1: Individual file SSIM vs Bit Flip Percentage
        for result in analysis_results:
            if result and result.get('flip_percentages'):
                ax1.plot(result['flip_percentages'], result['ssim_values'], 
                        marker='o', alpha=0.7, label=result['filename'], linewidth=2)
        
        ax1.set_xlabel('Bit Flip Percentage (%)', fontsize=12)
        ax1.set_ylabel('Structural Similarity Index (SSIM)', fontsize=12)
        ax1.set_title('SSIM vs Bit Flip Percentage by File', fontsize=14, fontweight='bold')
        ax1.grid(True, alpha=0.3)
        ax1.legend(bbox_to_anchor=(1.05, 1), loc='upper left')
        
        # Plot 2: Average SSIM vs Bit Flip Percentage
        unique_percentages = sorted(set(all_flip_percentages))
        avg_ssim_values = []
        
        for pct in unique_percentages:
            # Find all SSIM values for this percentage
            ssim_for_pct = [ssim for flip_pct, ssim in zip(all_flip_percentages, all_ssim_values) if flip_pct == pct]
            if ssim_for_pct:
                avg_ssim_values.append(np.mean(ssim_for_pct))
            else:
                avg_ssim_values.append(0.0)
        
        ax2.plot(unique_percentages, avg_ssim_values, marker='s', color='red', 
                linewidth=3, markersize=8, label='Average SSIM')
        ax2.set_xlabel('Bit Flip Percentage (%)', fontsize=12)
        ax2.set_ylabel('Average SSIM', fontsize=12)
        ax2.set_title('Average SSIM vs Bit Flip Percentage', fontsize=14, fontweight='bold')
        ax2.grid(True, alpha=0.3)
        ax2.legend()
        
        # Add trend line for average
        if len(unique_percentages) > 1:
            z = np.polyfit(unique_percentages, avg_ssim_values, 1)
            p = np.poly1d(z)
            ax2.plot(unique_percentages, p(unique_percentages), "r--", alpha=0.8, linewidth=2,
                    label=f'Trend: y = {z[0]:.4f}x + {z[1]:.4f}')
        
        # Adjust layout and save
        plt.tight_layout()
        
        # Save the combined graph
        graph_path = self.graphs_dir / "bit_flip_analysis.png"
        plt.savefig(graph_path, dpi=300, bbox_inches='tight')
        print(f"Bit flip analysis graphs saved to: {graph_path}")
        
        # Create detailed individual graphs
        self.create_detailed_bit_flip_graphs(analysis_results)
        
        plt.close()
    
    def create_detailed_bit_flip_graphs(self, analysis_results: List[Dict]):
        """Create detailed individual graphs for bit flip analysis."""
        
        # Individual file analysis
        for result in analysis_results:
            if not result or not result.get('flip_percentages'):
                continue
            
            plt.figure(figsize=(12, 8))
            plt.plot(result['flip_percentages'], result['ssim_values'], 
                    marker='o', linewidth=3, markersize=8, color='blue')
            
            # Add trend line
            if len(result['flip_percentages']) > 1:
                z = np.polyfit(result['flip_percentages'], result['ssim_values'], 1)
                p = np.poly1d(z)
                plt.plot(result['flip_percentages'], p(result['flip_percentages']), 
                        "r--", alpha=0.8, linewidth=2, 
                        label=f'Trend: y = {z[0]:.4f}x + {z[1]:.4f}')
                plt.legend()
            
            plt.xlabel('Bit Flip Percentage (%)', fontsize=14)
            plt.ylabel('Structural Similarity Index (SSIM)', fontsize=14)
            plt.title(f'Bit Flip Impact: {result["filename"]}', fontsize=16, fontweight='bold')
            plt.grid(True, alpha=0.3)
            
            # Save individual graph
            safe_filename = result['filename'].replace('.', '_').replace(' ', '_')
            graph_path = self.graphs_dir / f"bit_flip_{safe_filename}.png"
            plt.savefig(graph_path, dpi=300, bbox_inches='tight')
            print(f"Individual bit flip graph saved to: {graph_path}")
            plt.close()
        
        # Summary statistics graph
        plt.figure(figsize=(12, 8))
        
        # Calculate statistics for each percentage
        unique_percentages = set()
        for result in analysis_results:
            if result and result.get('flip_percentages'):
                unique_percentages.update(result['flip_percentages'])
        
        unique_percentages = sorted(unique_percentages)
        avg_ssim = []
        std_ssim = []
        min_ssim = []
        max_ssim = []
        
        for pct in unique_percentages:
            ssim_values = []
            for result in analysis_results:
                if result and result.get('flip_percentages'):
                    for flip_pct, ssim_val in zip(result['flip_percentages'], result['ssim_values']):
                        if flip_pct == pct:
                            ssim_values.append(ssim_val)
            
            if ssim_values:
                avg_ssim.append(np.mean(ssim_values))
                std_ssim.append(np.std(ssim_values))
                min_ssim.append(np.min(ssim_values))
                max_ssim.append(np.max(ssim_values))
            else:
                avg_ssim.append(0.0)
                std_ssim.append(0.0)
                min_ssim.append(0.0)
                max_ssim.append(0.0)
        
        # Plot with error bars
        plt.errorbar(unique_percentages, avg_ssim, yerr=std_ssim, 
                    marker='o', linewidth=3, markersize=8, 
                    label='Average ± Std Dev', capsize=5)
        
        plt.fill_between(unique_percentages, min_ssim, max_ssim, alpha=0.3, 
                        label='Min-Max Range')
        
        plt.xlabel('Bit Flip Percentage (%)', fontsize=14)
        plt.ylabel('Structural Similarity Index (SSIM)', fontsize=14)
        plt.title('Bit Flip Impact: Statistical Summary (FUSE)', fontsize=16, fontweight='bold')
        plt.grid(True, alpha=0.3)
        plt.legend()
        
        summary_graph_path = self.graphs_dir / "bit_flip_statistical_summary.png"
        plt.savefig(summary_graph_path, dpi=300, bbox_inches='tight')
        print(f"Statistical summary graph saved to: {summary_graph_path}")
        plt.close()
    
    def save_results(self):
        """Save results to JSON file if output file is specified."""
        if self.output_file:
            try:
                with open(self.output_file, 'w') as f:
                    json.dump(self.results, f, indent=2)
                print(f"\nResults saved to: {self.output_file}")
            except Exception as e:
                print(f"Warning: Could not save results: {e}")
    
    def cleanup(self):
        """Clean up temporary files."""
        print("\n=== Cleaning up ===")
        
        try:
            # Remove temporary directories
            if self.temp_dir.exists():
                shutil.rmtree(self.temp_dir)
                print(f"Removed temporary directory: {self.temp_dir}")
            
            print(f"Graphs preserved in: {self.graphs_dir}")
                
        except Exception as e:
            print(f"Warning: Could not clean up files: {e}")
    
    def run_analysis(self, flip_range: Tuple[float, float] = (0.1, 5.0), 
                    flip_steps: int = 20, use_test_images: bool = False):
        """
        Run the complete bit flip analysis with parallel processing.
        
        Args:
            flip_range: Tuple of (min_percentage, max_percentage)
            flip_steps: Number of steps between min and max
            use_test_images: If True, use test images from TestImages folder
        """
        print("=== Bit Flip Analysis for CriticalFUSE (FUSE) ===")
        print(f"Storage folder: {self.storage_folder}")
        print(f"BitFlipper path: {self.bitflipper_path}")
        print(f"Number of processes: {self.num_processes}")
        if self.test_images_folder:
            print(f"Test images folder: {self.test_images_folder}")
            if self.max_test_images:
                print(f"Max test images to analyze: {self.max_test_images}")
        print(f"Flip range: {flip_range[0]:.2f}% to {flip_range[1]:.2f}%")
        print(f"Number of steps: {flip_steps}")
        print(f"Using test images: {use_test_images}")
        
        # Generate flip percentages
        flip_percentages = np.linspace(flip_range[0], flip_range[1], flip_steps)
        print(f"Testing flip percentages: {flip_percentages}")
        
        start_time = time.time()
        
        if use_test_images and self.test_images_folder:
            # Use test images
            test_images = self.find_test_images()
            
            if not test_images:
                print("No test images found in the test images folder!")
                return
            
            print(f"\n=== Processing {len(test_images)} test images in parallel ===")
            
            # Prepare arguments for parallel processing
            args_list = [(img, flip_percentages, i) for i, img in enumerate(test_images)]
            
            # Process images in parallel
            with ProcessPoolExecutor(max_workers=self.num_processes) as executor:
                futures = [executor.submit(self.process_single_test_image, args) for args in args_list]
                
                # Collect results
                results = []
                for future in as_completed(futures):
                    try:
                        result = future.result()
                        if result:
                            results.append(result)
                    except Exception as e:
                        print(f"Error in parallel processing: {e}")
            
        else:
            # Use existing .noncrit files
            noncrit_files = self.find_noncrit_files()
            
            if not noncrit_files:
                print("No .noncrit files found in the storage folder!")
                return
            
            print(f"\n=== Processing {len(noncrit_files)} existing files in parallel ===")
            
            # Prepare arguments for parallel processing
            args_list = [(file, flip_percentages, i) for i, file in enumerate(noncrit_files)]
            
            # Process files in parallel
            with ProcessPoolExecutor(max_workers=self.num_processes) as executor:
                futures = [executor.submit(self.process_single_existing_file, args) for args in args_list]
                
                # Collect results
                results = []
                for future in as_completed(futures):
                    try:
                        result = future.result()
                        if result:
                            results.append(result)
                    except Exception as e:
                        print(f"Error in parallel processing: {e}")
        
        processing_time = time.time() - start_time
        print(f"\n=== Parallel processing completed in {processing_time:.2f} seconds ===")
        print(f"Successfully processed {len(results)} files")
        
        # Calculate SSIM for all processed images
        self.calculate_ssim_batch(results)
        
        # Store results
        self.results['tests'] = results
        self.results['processing_time_seconds'] = processing_time
        
        # Generate graphs
        if self.results['tests']:
            print("\n=== Generating Bit Flip Analysis Graphs ===")
            self.create_bit_flip_graphs(self.results['tests'])
        
        # Save results
        self.save_results()
        
        # Cleanup
        self.cleanup()


def main():
    parser = argparse.ArgumentParser(description='Analyze Bit Flip Impact on CriticalFUSE Non-Critical Files (FUSE)')
    parser.add_argument('storage_folder', help='Path to storage folder containing .noncrit files')
    parser.add_argument('bitflipper_path', help='Path to BitFlipper executable')
    parser.add_argument('--output-dir', '-o', required=True, help='Path to output directory for results')
    parser.add_argument('--test-images', help='Path to folder containing test images to copy to mount point')
    parser.add_argument('--use-test-images', action='store_true', help='Use test images instead of existing .noncrit files')
    parser.add_argument('--max-test-images', type=int, help='Maximum number of test images to analyze (default: all)')
    parser.add_argument('--guetzli-split', default='/usr/local/bin/GuetzliSplit', help='Path to GuetzliSplit executable')
    parser.add_argument('--output', help='Output JSON file for results')
    parser.add_argument('--flip-range', nargs=2, type=float, default=[0.1, 5.0], 
                       help='Range of bit flip percentages (min max)')
    parser.add_argument('--flip-steps', type=int, default=20, 
                       help='Number of steps between min and max percentage')
    parser.add_argument('--num-processes', type=int, help='Number of processes to use for parallel processing (default: min(CPU_count, 8))')
    parser.add_argument('--no-cleanup', action='store_true', help='Skip cleanup after testing')
    
    args = parser.parse_args()
    
    # Validate paths
    if not os.path.exists(args.storage_folder):
        print(f"Error: Storage folder '{args.storage_folder}' does not exist!")
        return 1
    
    if not os.path.exists(args.bitflipper_path):
        print(f"Error: BitFlipper executable '{args.bitflipper_path}' does not exist!")
        return 1
    
    # Create analyzer and run analysis
    analyzer = BitFlipAnalyzer(args.storage_folder, args.bitflipper_path, 
                              args.output_dir, args.test_images, args.output, args.max_test_images, 
                              args.guetzli_split, args.num_processes)
    
    try:
        analyzer.run_analysis(flip_range=tuple(args.flip_range), 
                            flip_steps=args.flip_steps,
                            use_test_images=args.use_test_images)
        
        if args.no_cleanup:
            print("\nSkipping cleanup as requested.")
            
    except KeyboardInterrupt:
        print("\n\nAnalysis interrupted by user.")
        analyzer.cleanup()
        return 1
    except Exception as e:
        print(f"\nError during analysis: {e}")
        analyzer.cleanup()
        return 1
    
    print("\n=== Analysis Complete ===")
    return 0


if __name__ == "__main__":
    exit(main())
