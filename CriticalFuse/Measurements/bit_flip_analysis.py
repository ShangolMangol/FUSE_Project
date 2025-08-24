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
                 guetzli_split_path: str = "/usr/local/bin/GuetzliSplit", num_processes: int = None,
                 skip_ssim: bool = False, fast_mode: bool = False, fast_ssim: bool = True,
                 cpp_ssim_path: str = None):
        """
        Initialize the bit flip analyzer.
        
        Args:
            storage_folder: Path to storage folder containing .noncrit files
            bitflipper_path: Path to BitFlipper executable
            output_dir: Path to output directory for results
            test_images_folder: Path to folder containing test images to copy to mount point
            output_file: Optional path to save results JSON
            num_processes: Number of processes to use for parallel processing
            skip_ssim: Skip SSIM calculation entirely
            fast_mode: Use fewer flip percentages for faster processing
            fast_ssim: Use fast SSIM implementation (PyTorch-based)
            cpp_ssim_path: Path to C++ SSIM executable (fastest option)
        """
        self.storage_folder = Path(storage_folder)
        self.bitflipper_path = Path(bitflipper_path)
        self.output_dir = Path(output_dir)
        self.test_images_folder = Path(test_images_folder) if test_images_folder else None
        self.output_file = output_file
        self.max_test_images = max_test_images
        self.guetzli_split_path = Path(guetzli_split_path)
        self.num_processes = num_processes or min(mp.cpu_count(), 8)  # Limit to 8 processes max
        self.skip_ssim = skip_ssim
        self.fast_mode = fast_mode
        self.fast_ssim = fast_ssim
        self.cpp_ssim_path = Path(cpp_ssim_path) if cpp_ssim_path else None
        
        # Initialize fast SSIM if available
        self.ssim_fn = None
        if self.fast_ssim and not self.cpp_ssim_path:
            self.ssim_fn = self._init_fast_ssim()
        
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
            'skip_ssim': self.skip_ssim,
            'fast_mode': self.fast_mode,
            'fast_ssim': self.fast_ssim,
            'cpp_ssim_path': str(self.cpp_ssim_path) if self.cpp_ssim_path else None,
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
    
    def calculate_ssim_cpp(self, original_file: Path, modified_file: Path) -> float:
        """
        Calculate SSIM using external C++ executable (fastest option).
        
        Args:
            original_file: Path to original file
            modified_file: Path to modified file
            
        Returns:
            SSIM value (0.0 to 1.0, where 1.0 is identical)
        """
        try:
            if not self.cpp_ssim_path or not self.cpp_ssim_path.exists():
                print(f"C++ SSIM executable not found at {self.cpp_ssim_path}")
                return self.calculate_ssim_optimized(original_file, modified_file)
            
            # Run C++ SSIM executable
            cmd = [str(self.cpp_ssim_path), str(original_file), str(modified_file), "1"]  # Use windowed SSIM
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
            
            if result.returncode != 0:
                print(f"C++ SSIM failed: {result.stderr}")
                return self.calculate_ssim_optimized(original_file, modified_file)
            
            # Parse output
            try:
                ssim_value = float(result.stdout.strip())
                return ssim_value
            except ValueError:
                print(f"Invalid SSIM output: {result.stdout}")
                return self.calculate_ssim_optimized(original_file, modified_file)
                
        except subprocess.TimeoutExpired:
            print(f"C++ SSIM timed out for {original_file}")
            return self.calculate_ssim_optimized(original_file, modified_file)
        except Exception as e:
            print(f"Error running C++ SSIM: {e}")
            return self.calculate_ssim_optimized(original_file, modified_file)
    
    def calculate_ssim(self, original_file: Path, modified_file: Path) -> float:
        """
        Calculate SSIM between original and modified files.
        Uses the fastest available implementation.
        
        Args:
            original_file: Path to original file
            modified_file: Path to modified file
            
        Returns:
            SSIM value (0.0 to 1.0, where 1.0 is identical)
        """
        # Priority order: C++ > PyTorch > Optimized scikit-image
        if self.cpp_ssim_path and self.cpp_ssim_path.exists():
            return self.calculate_ssim_cpp(original_file, modified_file)
        elif self.fast_ssim and self.ssim_fn is not None:
            return self.calculate_ssim_fast_pytorch(original_file, modified_file)
        else:
            return self.calculate_ssim_optimized(original_file, modified_file)
    
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
    
    def calculate_ssim_batch(self, results_data):
        """
        Calculate SSIM for all processed images in batch.
        
        Args:
            results_data: List of result dictionaries from parallel processing
        """
        print("\n=== Calculating SSIM for all processed images ===")
        
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

    def _init_fast_ssim(self):
        """Initialize fast SSIM implementation using PyTorch."""
        try:
            import torch
            import torch.nn.functional as F
            from torch import nn
            
            class FastSSIM(nn.Module):
                def __init__(self, window_size=11, size_average=True):
                    super(FastSSIM, self).__init__()
                    self.window_size = window_size
                    self.size_average = size_average
                    self.channel = 1
                    self.window = self._create_window(window_size, self.channel)

                def _gaussian(self, window_size, sigma):
                    gauss = torch.Tensor([torch.exp(torch.tensor(-(x - window_size//2)**2/float(2*sigma**2))) for x in range(window_size)])
                    return gauss/gauss.sum()

                def _create_window(self, window_size, channel):
                    _1D_window = self._gaussian(window_size, 1.5).unsqueeze(1)
                    _2D_window = _1D_window.mm(_1D_window.t()).float().unsqueeze(0).unsqueeze(0)
                    window = _2D_window.expand(channel, 1, window_size, window_size).contiguous()
                    return window

                def _ssim(self, img1, img2, window, window_size, channel, size_average=True):
                    mu1 = F.conv2d(img1, window, padding=window_size//2, groups=channel)
                    mu2 = F.conv2d(img2, window, padding=window_size//2, groups=channel)

                    mu1_sq = mu1.pow(2)
                    mu2_sq = mu2.pow(2)
                    mu1_mu2 = mu1 * mu2

                    sigma1_sq = F.conv2d(img1 * img1, window, padding=window_size//2, groups=channel) - mu1_sq
                    sigma2_sq = F.conv2d(img2 * img2, window, padding=window_size//2, groups=channel) - mu2_sq
                    sigma12 = F.conv2d(img1 * img2, window, padding=window_size//2, groups=channel) - mu1_mu2

                    C1 = 0.01**2
                    C2 = 0.03**2

                    ssim_map = ((2 * mu1_mu2 + C1) * (2 * sigma12 + C2)) / ((mu1_sq + mu2_sq + C1) * (sigma1_sq + sigma2_sq + C2))

                    if size_average:
                        return ssim_map.mean()
                    else:
                        return ssim_map.mean(1).mean(1).mean(1)

                def forward(self, img1, img2):
                    (_, channel, _, _) = img1.size()

                    if channel == self.channel and self.window.data.type() == img1.data.type():
                        window = self.window
                    else:
                        window = self._create_window(self.window_size, channel)
                        
                        if img1.is_cuda:
                            window = window.cuda(img1.get_device())
                        window = window.type_as(img1)
                        
                        self.window = window
                        self.channel = channel

                    return self._ssim(img1, img2, window, self.window_size, channel, self.size_average)
            
            return FastSSIM()
            
        except ImportError:
            print("PyTorch not available, falling back to optimized scikit-image SSIM")
            return None
    
    def calculate_ssim_fast_pytorch(self, original_file: Path, modified_file: Path) -> float:
        """
        Calculate SSIM using PyTorch for maximum speed.
        
        Args:
            original_file: Path to original file
            modified_file: Path to modified file
            
        Returns:
            SSIM value (0.0 to 1.0, where 1.0 is identical)
        """
        try:
            import torch
            import torchvision.transforms as transforms
            from PIL import Image
            
            # Read images with PIL
            original_img = Image.open(original_file).convert('L')  # Convert to grayscale
            modified_img = Image.open(modified_file).convert('L')
            
            # Ensure same size
            if original_img.size != modified_img.size:
                modified_img = modified_img.resize(original_img.size, Image.LANCZOS)
            
            # Convert to tensors
            transform = transforms.ToTensor()
            original_tensor = transform(original_img).unsqueeze(0)  # Add batch dimension
            modified_tensor = transform(modified_img).unsqueeze(0)
            
            # Calculate SSIM
            if self.ssim_fn is not None:
                ssim_value = self.ssim_fn(original_tensor, modified_tensor)
                return float(ssim_value.item())
            else:
                # Fallback to optimized scikit-image
                return self.calculate_ssim_optimized(original_file, modified_file)
                
        except Exception as e:
            print(f"Error in PyTorch SSIM calculation: {e}")
            return self.calculate_ssim_optimized(original_file, modified_file)
    
    def calculate_ssim_optimized(self, original_file: Path, modified_file: Path) -> float:
        """
        Optimized SSIM calculation using scikit-image with better performance.
        
        Args:
            original_file: Path to original file
            modified_file: Path to modified file
            
        Returns:
            SSIM value (0.0 to 1.0, where 1.0 is identical)
        """
        try:
            # Use PIL for faster image loading
            from PIL import Image
            import numpy as np
            
            # Read images with PIL (faster than cv2)
            original_img = Image.open(original_file).convert('L')
            modified_img = Image.open(modified_file).convert('L')
            
            # Ensure same size
            if original_img.size != modified_img.size:
                modified_img = modified_img.resize(original_img.size, Image.LANCZOS)
            
            # Convert to numpy arrays
            original_array = np.array(original_img, dtype=np.float32)
            modified_array = np.array(modified_img, dtype=np.float32)
            
            # Calculate SSIM with optimized parameters
            ssim_value = ssim(original_array, modified_array, 
                            data_range=255,
                            gaussian_weights=True,
                            sigma=1.5,
                            use_sample_covariance=False,
                            K1=0.01,
                            K2=0.03)
            
            return float(ssim_value)
            
        except Exception as e:
            print(f"Error in optimized SSIM calculation: {e}")
            return 0.0
    
    def calculate_ssim_ultra_fast(self, original_file: Path, modified_file: Path) -> float:
        """
        Ultra-fast SSIM approximation using simple statistics.
        This is much faster but less accurate than full SSIM.
        
        Args:
            original_file: Path to original file
            modified_file: Path to modified file
            
        Returns:
            Approximate SSIM value (0.0 to 1.0, where 1.0 is identical)
        """
        try:
            from PIL import Image
            import numpy as np
            
            # Read images with PIL
            original_img = Image.open(original_file).convert('L')
            modified_img = Image.open(modified_file).convert('L')
            
            # Ensure same size
            if original_img.size != modified_img.size:
                modified_img = modified_img.resize(original_img.size, Image.LANCZOS)
            
            # Convert to numpy arrays
            original_array = np.array(original_img, dtype=np.float32)
            modified_array = np.array(modified_img, dtype=np.float32)
            
            # Calculate simple statistics
            mu1 = np.mean(original_array)
            mu2 = np.mean(modified_array)
            
            sigma1_sq = np.var(original_array)
            sigma2_sq = np.var(modified_array)
            sigma12 = np.mean((original_array - mu1) * (modified_array - mu2))
            
            # Constants for numerical stability
            C1 = (0.01 * 255) ** 2
            C2 = (0.03 * 255) ** 2
            
            # Simplified SSIM calculation
            numerator = (2 * mu1 * mu2 + C1) * (2 * sigma12 + C2)
            denominator = (mu1 ** 2 + mu2 ** 2 + C1) * (sigma1_sq + sigma2_sq + C2)
            
            ssim_value = numerator / denominator
            return float(ssim_value)
            
        except Exception as e:
            print(f"Error in ultra-fast SSIM calculation: {e}")
            return 0.0


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
    parser.add_argument('--skip-ssim', action='store_true', help='Skip SSIM calculation entirely')
    parser.add_argument('--fast-mode', action='store_true', help='Use fewer flip percentages for faster processing')
    parser.add_argument('--fast-ssim', action='store_true', default=True, help='Use fast SSIM implementation (PyTorch-based, default: True)')
    parser.add_argument('--ultra-fast-ssim', action='store_true', help='Use ultra-fast SSIM approximation (less accurate but much faster)')
    parser.add_argument('--cpp-ssim', help='Path to C++ SSIM executable (fastest option)')
    
    args = parser.parse_args()
    
    # Validate paths
    if not os.path.exists(args.storage_folder):
        print(f"Error: Storage folder '{args.storage_folder}' does not exist!")
        return 1
    
    if not os.path.exists(args.bitflipper_path):
        print(f"Error: BitFlipper executable '{args.bitflipper_path}' does not exist!")
        return 1
    
    # Handle SSIM mode selection
    fast_ssim = args.fast_ssim
    if args.ultra_fast_ssim:
        fast_ssim = False  # Will use ultra-fast mode in the calculation
    
    # Create analyzer and run analysis
    analyzer = BitFlipAnalyzer(args.storage_folder, args.bitflipper_path, 
                              args.output_dir, args.test_images, args.output, args.max_test_images, 
                              args.guetzli_split, args.num_processes, args.skip_ssim, args.fast_mode, fast_ssim,
                              args.cpp_ssim)
    
    # Set ultra-fast mode if requested
    if args.ultra_fast_ssim:
        analyzer.calculate_ssim = analyzer.calculate_ssim_ultra_fast
    
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
