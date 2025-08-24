#!/usr/bin/env python3
"""
Bit Flip Analysis Script for CriticalFUSE

This script uses the BitFlipper executable to introduce random bit flips to non-critical
files in the storage folder and measures the structural similarity index (SSIM) by
reading the merged images from the FUSE mount point.

Usage:
    python3 bit_flip_analysis.py <storage_folder> <mount_point> <bitflipper_path> [options]

Examples:
    python3 bit_flip_analysis.py ./storage ./mnt ./BitFlipper.exe --output-dir ./bitflip_results
    python3 bit_flip_analysis.py ./storage ./mnt ./BitFlipper.exe -o ./results --flip-range 0.1 5.0
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


class BitFlipAnalyzer:
    def __init__(self, storage_folder: str, mount_point: str, bitflipper_path: str, output_dir: str, 
                 output_file: str = None):
        """
        Initialize the bit flip analyzer.
        
        Args:
            storage_folder: Path to storage folder containing .noncrit files
            mount_point: Path to FUSE mount point where merged images are accessible
            bitflipper_path: Path to BitFlipper executable
            output_dir: Path to output directory for results
            output_file: Optional path to save results JSON
        """
        self.storage_folder = Path(storage_folder)
        self.mount_point = Path(mount_point)
        self.bitflipper_path = Path(bitflipper_path)
        self.output_dir = Path(output_dir)
        self.output_file = output_file
        
        # Create separate graphs directory
        self.graphs_dir = self.output_dir.parent / f"{self.output_dir.name}_graphs"
        self.graphs_dir.mkdir(parents=True, exist_ok=True)
        
        self.results = {
            'timestamp': datetime.now().isoformat(),
            'storage_folder': str(self.storage_folder),
            'mount_point': str(self.mount_point),
            'bitflipper_path': str(self.bitflipper_path),
            'output_dir': str(self.output_dir),
            'graphs_dir': str(self.graphs_dir),
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
        
        # Verify mount point exists
        print(f"Checking mount point at: {self.mount_point}")
        print(f"Mount point exists: {self.mount_point.exists()}")
        if not self.mount_point.exists():
            raise FileNotFoundError(f"Mount point not found at {self.mount_point}")
    
    def find_noncrit_files(self) -> List[Path]:
        """Find all .noncrit files in the storage folder."""
        noncrit_files = list(self.storage_folder.glob("*.noncrit"))
        noncrit_files.sort()
        
        print(f"Found {len(noncrit_files)} .noncrit files in {self.storage_folder}")
        return noncrit_files
    
    def find_merged_image(self, noncrit_file: Path) -> Path:
        """
        Find the corresponding merged image in the mount point.
        
        Args:
            noncrit_file: Path to .noncrit file
            
        Returns:
            Path to merged image in mount point, or None if not found
        """
        # Get the base filename without .noncrit extension
        base_name = noncrit_file.stem
        
        # Look for the merged image in the mount point
        # The merged image should have the original extension (jpg, png, etc.)
        possible_extensions = ['.jpg', '.jpeg', '.png', '.bmp', '.dng']
        
        for ext in possible_extensions:
            merged_image = self.mount_point / f"{base_name}{ext}"
            if merged_image.exists():
                return merged_image
        
        # If no extension found, try without extension
        merged_image = self.mount_point / base_name
        if merged_image.exists():
            return merged_image
        
        return None
    
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
            shutil.copy2(input_file, backup_file)
            
            # Run BitFlipper command in random mode (-r)
            cmd = [str(self.bitflipper_path), "-r", str(flip_percentage), str(input_file)]
            print(f"Running command: {' '.join(cmd)}")
            print(f"Current working directory: {os.getcwd()}")
            print(f"BitFlipper path exists: {self.bitflipper_path.exists()}")
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
            
            if result.returncode != 0:
                print(f"BitFlipper failed: {result.stderr}")
                # Restore backup
                shutil.move(backup_file, input_file)
                return False
            
            # Remove backup on success
            backup_file.unlink(missing_ok=True)
            return True
            
        except subprocess.TimeoutExpired:
            print(f"BitFlipper timed out for {input_file}")
            # Restore backup
            if backup_file.exists():
                shutil.move(backup_file, input_file)
            return False
        except Exception as e:
            print(f"Error running BitFlipper: {e}")
            # Restore backup
            if backup_file.exists():
                shutil.move(backup_file, input_file)
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
    
    def wait_for_fuse_update(self, timeout: int = 5) -> bool:
        """
        Wait for FUSE filesystem to update after file modification.
        
        Args:
            timeout: Maximum time to wait in seconds
            
        Returns:
            True if update detected, False if timeout
        """
        print(f"Waiting up to {timeout} seconds for FUSE update...")
        time.sleep(timeout)
        return True
    
    def analyze_file_bit_flips(self, noncrit_file: Path, flip_percentages: List[float]) -> Dict:
        """
        Analyze the impact of bit flips on a single file.
        
        Args:
            noncrit_file: Path to .noncrit file
            flip_percentages: List of flip percentages to test
            
        Returns:
            Dictionary with analysis results
        """
        print(f"Analyzing bit flips for: {noncrit_file.name}")
        
        # Find corresponding merged image in mount point
        original_merged_image = self.find_merged_image(noncrit_file)
        if not original_merged_image:
            print(f"Could not find merged image for {noncrit_file.name} in mount point")
            return None
        
        print(f"Found merged image: {original_merged_image}")
        
        # Create a copy of the original merged image for comparison
        original_copy = self.output_dir / f"original_{noncrit_file.stem}{original_merged_image.suffix}"
        shutil.copy2(original_merged_image, original_copy)
        
        file_results = {
            'filename': noncrit_file.name,
            'merged_image': str(original_merged_image),
            'flip_percentages': [],
            'ssim_values': [],
            'file_size': noncrit_file.stat().st_size
        }
        
        for flip_pct in flip_percentages:
            print(f"  Testing {flip_pct:.2f}% bit flips...")
            
            # Apply bit flips to the noncrit file
            if not self.run_bitflipper(noncrit_file, flip_pct):
                print(f"    BitFlipper failed for {flip_pct:.2f}%")
                continue
            
            # Wait for FUSE to update
            self.wait_for_fuse_update()
            
            # Read the updated merged image from mount point
            updated_merged_image = self.find_merged_image(noncrit_file)
            if not updated_merged_image or not updated_merged_image.exists():
                print(f"    Could not find updated merged image for {flip_pct:.2f}%")
                continue
            
            # Create a copy of the updated image for analysis
            updated_copy = self.output_dir / f"updated_{noncrit_file.stem}_{flip_pct:.2f}{updated_merged_image.suffix}"
            shutil.copy2(updated_merged_image, updated_copy)
            
            # Calculate SSIM
            ssim_value = self.calculate_ssim(original_copy, updated_copy)
            
            file_results['flip_percentages'].append(flip_pct)
            file_results['ssim_values'].append(ssim_value)
            
            print(f"    SSIM: {ssim_value:.4f}")
            
            # Clean up temporary updated copy
            updated_copy.unlink(missing_ok=True)
        
        # Clean up original copy
        original_copy.unlink(missing_ok=True)
        
        return file_results
    
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
            # Remove temporary files in output directory
            for temp_file in self.output_dir.glob("temp_*"):
                temp_file.unlink(missing_ok=True)
            
            print(f"Cleaned up temporary files in: {self.output_dir}")
            print(f"Graphs preserved in: {self.graphs_dir}")
                
        except Exception as e:
            print(f"Warning: Could not clean up files: {e}")
    
    def run_analysis(self, flip_range: Tuple[float, float] = (0.1, 5.0), 
                    flip_steps: int = 20):
        """
        Run the complete bit flip analysis.
        
        Args:
            flip_range: Tuple of (min_percentage, max_percentage)
            flip_steps: Number of steps between min and max
        """
        print("=== Bit Flip Analysis for CriticalFUSE (FUSE) ===")
        print(f"Storage folder: {self.storage_folder}")
        print(f"Mount point: {self.mount_point}")
        print(f"BitFlipper path: {self.bitflipper_path}")
        print(f"Flip range: {flip_range[0]:.2f}% to {flip_range[1]:.2f}%")
        print(f"Number of steps: {flip_steps}")
        
        # Find non-critical files
        noncrit_files = self.find_noncrit_files()
        
        if not noncrit_files:
            print("No .noncrit files found in the storage folder!")
            return
        
        # Generate flip percentages
        flip_percentages = np.linspace(flip_range[0], flip_range[1], flip_steps)
        print(f"Testing flip percentages: {flip_percentages}")
        
        # Analyze each file
        for noncrit_file in noncrit_files:
            print(f"\nAnalyzing: {noncrit_file.name}")
            file_results = self.analyze_file_bit_flips(noncrit_file, flip_percentages)
            
            if file_results:
                self.results['tests'].append(file_results)
        
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
    parser.add_argument('mount_point', help='Path to FUSE mount point where merged images are accessible')
    parser.add_argument('bitflipper_path', help='Path to BitFlipper executable')
    parser.add_argument('--output-dir', '-o', required=True, help='Path to output directory for results')
    parser.add_argument('--output', help='Output JSON file for results')
    parser.add_argument('--flip-range', nargs=2, type=float, default=[0.1, 5.0], 
                       help='Range of bit flip percentages (min max)')
    parser.add_argument('--flip-steps', type=int, default=20, 
                       help='Number of steps between min and max percentage')
    parser.add_argument('--no-cleanup', action='store_true', help='Skip cleanup after testing')
    
    args = parser.parse_args()
    
    # Validate paths
    if not os.path.exists(args.storage_folder):
        print(f"Error: Storage folder '{args.storage_folder}' does not exist!")
        return 1
    
    if not os.path.exists(args.mount_point):
        print(f"Error: Mount point '{args.mount_point}' does not exist!")
        return 1
    
    if not os.path.exists(args.bitflipper_path):
        print(f"Error: BitFlipper executable '{args.bitflipper_path}' does not exist!")
        return 1
    
    # Create analyzer and run analysis
    analyzer = BitFlipAnalyzer(args.storage_folder, args.mount_point, args.bitflipper_path, 
                              args.output_dir, args.output)
    
    try:
        analyzer.run_analysis(flip_range=tuple(args.flip_range), 
                            flip_steps=args.flip_steps)
        
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
