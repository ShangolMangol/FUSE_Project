#!/usr/bin/env python3
"""
GuetzliSplit Performance Testing Script

This script measures the performance of GuetzliSplit operations for JPEG images,
including splitting, reading, and merging operations using the --split and --merge flags.
It generates performance graphs showing split/merge times vs file sizes and includes 
automatic cleanup functionality. Graphs are automatically saved to a separate directory
that is never deleted.

Usage:
    python3 jpeg_performance_test.py <source_folder> --output-dir <output_directory> [options]

Examples:
    python3 jpeg_performance_test.py ../TestImages/ --output-dir ./guetzli_test
    python3 jpeg_performance_test.py ../TestImages/ -o ./guetzli_test --output results.json
"""

import os
import shutil
import time
import glob
import argparse
import subprocess
from pathlib import Path
from typing import List, Dict, Tuple
import json
from datetime import datetime
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import numpy as np


class JPEGPerformanceTester:
    def __init__(self, source_folder: str, output_dir: str, output_file: str = None):
        """
        Initialize the performance tester.
        
        Args:
            source_folder: Path to folder containing JPEG images
            output_dir: Path to output directory for GuetzliSplit operations
            output_file: Optional path to save results JSON
        """
        self.source_folder = Path(source_folder)
        self.output_dir = Path(output_dir)
        self.output_file = output_file
        self.guetzli_path = Path('/usr/local/bin/GuetzliSplit')
        
        # Create separate graphs directory
        self.graphs_dir = self.output_dir.parent / f"{self.output_dir.name}_graphs"
        self.graphs_dir.mkdir(parents=True, exist_ok=True)
        
        self.results = {
            'timestamp': datetime.now().isoformat(),
            'source_folder': str(self.source_folder),
            'output_dir': str(self.output_dir),
            'graphs_dir': str(self.graphs_dir),
            'guetzli_path': str(self.guetzli_path),
            'tests': []
        }
        
        # Ensure output directory exists
        self.output_dir.mkdir(parents=True, exist_ok=True)
        
        # Check if GuetzliSplit exists
        if not self.guetzli_path.exists():
            print(f"Error: GuetzliSplit not found at {self.guetzli_path}")
            print("This script requires GuetzliSplit to be installed.")
            raise FileNotFoundError(f"GuetzliSplit not found at {self.guetzli_path}")
    
    def find_jpeg_files(self) -> List[Path]:
        """Find all JPEG files in the source folder."""
        jpeg_patterns = ['*.jpg', '*.jpeg', '*.JPG', '*.JPEG']
        jpeg_files = []
        
        for pattern in jpeg_patterns:
            jpeg_files.extend(self.source_folder.glob(pattern))
            jpeg_files.extend(self.source_folder.glob(f'**/{pattern}'))
        
        # Remove duplicates and sort
        jpeg_files = list(set(jpeg_files))
        jpeg_files.sort()
        
        print(f"Found {len(jpeg_files)} JPEG files in {self.source_folder}")
        return jpeg_files
    
    def measure_file_operation(self, operation_func, *args, **kwargs) -> Tuple[float, int]:
        """
        Measure the time and size of a file operation.
        
        Args:
            operation_func: Function to measure
            *args, **kwargs: Arguments for the function
            
        Returns:
            Tuple of (time_taken, file_size)
        """
        start_time = time.time()
        file_size = operation_func(*args, **kwargs)
        end_time = time.time()
        
        return end_time - start_time, file_size
    
    def write_file(self, source_path: Path, dest_path: Path) -> int:
        """Write a file and return its size."""
        shutil.copy2(source_path, dest_path)
        return dest_path.stat().st_size
    
    def read_file(self, file_path: Path) -> int:
        """Read a file and return its size."""
        with open(file_path, 'rb') as f:
            data = f.read()
        return len(data)
    
    def guetzli_split_file(self, source_path: Path, dest_dir: Path) -> Tuple[int, float]:
        """Split a file using GuetzliSplit and return the total size and time."""
        if not self.guetzli_path.exists():
            raise FileNotFoundError(f"GuetzliSplit not found at {self.guetzli_path}")
        
        start_time = time.time()
        
        # Create output filename for split operation
        output_base = dest_dir / source_path.stem
        
        # Run GuetzliSplit command with --split flag
        cmd = [str(self.guetzli_path), "--split", str(source_path), str(output_base)]
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=300)  # 5 minute timeout
        
        if result.returncode != 0:
            raise subprocess.CalledProcessError(result.returncode, cmd, result.stdout, result.stderr)
        
        end_time = time.time()
        
        # Calculate total size of split files (.crit and .ac.noncrit)
        total_size = 0
        crit_file = output_base.with_suffix('.jpg.crit')
        noncrit_file = output_base.with_suffix('.jpg.ac.noncrit')
        
        if crit_file.exists():
            total_size += crit_file.stat().st_size
        if noncrit_file.exists():
            total_size += noncrit_file.stat().st_size
        
        return total_size, end_time - start_time
    
    def guetzli_merge_file(self, source_dir: Path, original_filename: str) -> Tuple[int, float]:
        """Merge split files using GuetzliSplit and return the size and time."""
        if not self.guetzli_path.exists():
            raise FileNotFoundError(f"GuetzliSplit not found at {self.guetzli_path}")
        
        start_time = time.time()
        
        # Create paths for merge operation
        base_name = Path(original_filename).stem
        crit_file = source_dir / f"{base_name}.jpg.crit"
        merged_output = source_dir / f"merged_{original_filename}"
        
        # Run GuetzliSplit merge command
        cmd = [str(self.guetzli_path), "--merge", str(crit_file), str(merged_output)]
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=300)  # 5 minute timeout
        
        if result.returncode != 0:
            raise subprocess.CalledProcessError(result.returncode, cmd, result.stdout, result.stderr)
        
        end_time = time.time()
        
        # Get the merged file size
        if merged_output.exists():
            return merged_output.stat().st_size, end_time - start_time
        else:
            raise FileNotFoundError(f"Merged file not found: {merged_output}")
    

    
    def test_guetzli_directory(self, test_dir: Path, jpeg_files: List[Path], test_name: str) -> Dict:
        """Test GuetzliSplit split/merge performance for a specific directory."""
        print(f"\n=== Testing {test_name} ===")
        
        test_results = {
            'directory': str(test_dir),
            'test_name': test_name,
            'files': []
        }
        
        total_split_time = 0
        total_merge_time = 0
        total_read_time = 0
        total_size = 0
        
        for i, jpeg_file in enumerate(jpeg_files, 1):
            print(f"Processing {i}/{len(jpeg_files)}: {jpeg_file.name}")
            
            # Create subdirectory for this file's split operations
            file_test_dir = test_dir / jpeg_file.stem
            file_test_dir.mkdir(exist_ok=True)
            
            try:
                # Measure split time
                split_size, split_time = self.guetzli_split_file(jpeg_file, file_test_dir)
                
                # Measure read time of critical file (the main split file)
                base_name = jpeg_file.stem
                crit_file = file_test_dir / f"{base_name}.jpg.crit"
                read_time, _ = self.measure_file_operation(
                    self.read_file, crit_file
                )
                
                # Measure merge time
                merge_size, merge_time = self.guetzli_merge_file(file_test_dir, jpeg_file.name)
                
                file_result = {
                    'filename': jpeg_file.name,
                    'original_size': jpeg_file.stat().st_size,
                    'split_size': split_size,
                    'merge_size': merge_size,
                    'split_time': split_time,
                    'read_time': read_time,
                    'merge_time': merge_time,
                    'split_speed_mbps': (split_size / (1024 * 1024)) / split_time if split_time > 0 else 0,
                    'read_speed_mbps': (split_size / (1024 * 1024)) / read_time if read_time > 0 else 0,
                    'merge_speed_mbps': (merge_size / (1024 * 1024)) / merge_time if merge_time > 0 else 0
                }
                
                test_results['files'].append(file_result)
                total_split_time += split_time
                total_merge_time += merge_time
                total_read_time += read_time
                total_size += split_size
                
                print(f"  Split: {split_time:.4f}s ({file_result['split_speed_mbps']:.2f} MB/s)")
                print(f"  Read:  {read_time:.4f}s ({file_result['read_speed_mbps']:.2f} MB/s)")
                print(f"  Merge: {merge_time:.4f}s ({file_result['merge_speed_mbps']:.2f} MB/s)")
                
            except Exception as e:
                print(f"  Error processing {jpeg_file.name}: {e}")
                continue
        
        if test_results['files']:
            # Calculate totals
            test_results['summary'] = {
                'total_files': len(test_results['files']),
                'total_size_mb': total_size / (1024 * 1024),
                'total_split_time': total_split_time,
                'total_read_time': total_read_time,
                'total_merge_time': total_merge_time,
                'avg_split_speed_mbps': (total_size / (1024 * 1024)) / total_split_time if total_split_time > 0 else 0,
                'avg_read_speed_mbps': (total_size / (1024 * 1024)) / total_read_time if total_read_time > 0 else 0,
                'avg_merge_speed_mbps': (total_size / (1024 * 1024)) / total_merge_time if total_merge_time > 0 else 0
            }
            
            print(f"\n{test_name} Summary:")
            print(f"  Total files: {test_results['summary']['total_files']}")
            print(f"  Total size: {test_results['summary']['total_size_mb']:.2f} MB")
            print(f"  Total split time: {total_split_time:.4f}s")
            print(f"  Total read time: {total_read_time:.4f}s")
            print(f"  Total merge time: {total_merge_time:.4f}s")
            print(f"  Avg split speed: {test_results['summary']['avg_split_speed_mbps']:.2f} MB/s")
            print(f"  Avg read speed: {test_results['summary']['avg_read_speed_mbps']:.2f} MB/s")
            print(f"  Avg merge speed: {test_results['summary']['avg_merge_speed_mbps']:.2f} MB/s")
        
        return test_results
    
    def cleanup(self):
        """Clean up test directories."""
        print("\n=== Cleaning up ===")
        
        try:
            if self.output_dir.exists():
                shutil.rmtree(self.output_dir)
                print(f"Removed test directory: {self.output_dir}")
                
            # Note: Graphs directory is preserved and never deleted
            print(f"Graphs preserved in: {self.graphs_dir}")
                
        except Exception as e:
            print(f"Warning: Could not clean up directory: {e}")
    
    def create_performance_graphs(self, test_results: Dict):
        """Create performance graphs for split/merge times vs file sizes."""
        if not test_results.get('files'):
            print("No data available for graph generation")
            return
        
        # Extract data for plotting
        file_sizes = []
        split_times = []
        merge_times = []
        read_times = []
        filenames = []
        
        for file_result in test_results['files']:
            # Convert file sizes to MB for better readability
            size_mb = file_result['original_size'] / (1024 * 1024)
            file_sizes.append(size_mb)
            split_times.append(file_result['split_time'])
            merge_times.append(file_result['merge_time'])
            read_times.append(file_result['read_time'])
            filenames.append(file_result['filename'])
        
        # Create figure with subplots
        fig, (ax1, ax2, ax3) = plt.subplots(1, 3, figsize=(18, 6))
        fig.suptitle('GuetzliSplit Performance Analysis', fontsize=16, fontweight='bold')
        
        # Plot 1: Split Time vs File Size
        ax1.scatter(file_sizes, split_times, alpha=0.7, s=50, color='blue', edgecolors='black')
        ax1.set_xlabel('File Size (MB)', fontsize=12)
        ax1.set_ylabel('Split Time (seconds)', fontsize=12)
        ax1.set_title('Split Time vs File Size', fontsize=14, fontweight='bold')
        ax1.grid(True, alpha=0.3)
        
        # Add trend line for split times
        if len(file_sizes) > 1:
            z = np.polyfit(file_sizes, split_times, 1)
            p = np.poly1d(z)
            ax1.plot(file_sizes, p(file_sizes), "r--", alpha=0.8, linewidth=2)
            ax1.text(0.05, 0.95, f'Trend: y = {z[0]:.4f}x + {z[1]:.4f}', 
                    transform=ax1.transAxes, fontsize=10, 
                    bbox=dict(boxstyle="round,pad=0.3", facecolor="white", alpha=0.8))
        
        # Plot 2: Merge Time vs File Size
        ax2.scatter(file_sizes, merge_times, alpha=0.7, s=50, color='green', edgecolors='black')
        ax2.set_xlabel('File Size (MB)', fontsize=12)
        ax2.set_ylabel('Merge Time (seconds)', fontsize=12)
        ax2.set_title('Merge Time vs File Size', fontsize=14, fontweight='bold')
        ax2.grid(True, alpha=0.3)
        
        # Add trend line for merge times
        if len(file_sizes) > 1:
            z = np.polyfit(file_sizes, merge_times, 1)
            p = np.poly1d(z)
            ax2.plot(file_sizes, p(file_sizes), "r--", alpha=0.8, linewidth=2)
            ax2.text(0.05, 0.95, f'Trend: y = {z[0]:.4f}x + {z[1]:.4f}', 
                    transform=ax2.transAxes, fontsize=10, 
                    bbox=dict(boxstyle="round,pad=0.3", facecolor="white", alpha=0.8))
        
        # Plot 3: Read Time vs File Size
        ax3.scatter(file_sizes, read_times, alpha=0.7, s=50, color='orange', edgecolors='black')
        ax3.set_xlabel('File Size (MB)', fontsize=12)
        ax3.set_ylabel('Read Time (seconds)', fontsize=12)
        ax3.set_title('Read Time vs File Size', fontsize=14, fontweight='bold')
        ax3.grid(True, alpha=0.3)
        
        # Add trend line for read times
        if len(file_sizes) > 1:
            z = np.polyfit(file_sizes, read_times, 1)
            p = np.poly1d(z)
            ax3.plot(file_sizes, p(file_sizes), "r--", alpha=0.8, linewidth=2)
            ax3.text(0.05, 0.95, f'Trend: y = {z[0]:.4f}x + {z[1]:.4f}', 
                    transform=ax3.transAxes, fontsize=10, 
                    bbox=dict(boxstyle="round,pad=0.3", facecolor="white", alpha=0.8))
        
        # Adjust layout and save
        plt.tight_layout()
        
        # Save the combined graph
        graph_path = self.graphs_dir / "performance_analysis.png"
        plt.savefig(graph_path, dpi=300, bbox_inches='tight')
        print(f"Performance graphs saved to: {graph_path}")
        
        # Create individual graphs for better detail
        self.create_individual_graphs(file_sizes, split_times, merge_times, read_times, filenames)
        
        plt.close()
    
    def create_individual_graphs(self, file_sizes, split_times, merge_times, read_times, filenames):
        """Create individual detailed graphs for each operation type."""
        
        # Individual Split Time Graph
        plt.figure(figsize=(12, 8))
        plt.scatter(file_sizes, split_times, alpha=0.8, s=80, color='blue', edgecolors='black')
        
        # Add trend line
        if len(file_sizes) > 1:
            z = np.polyfit(file_sizes, split_times, 1)
            p = np.poly1d(z)
            plt.plot(file_sizes, p(file_sizes), "r--", alpha=0.8, linewidth=3, label=f'Trend: y = {z[0]:.4f}x + {z[1]:.4f}')
        
        # Add file labels for some points (avoid overcrowding)
        for i, filename in enumerate(filenames):
            if i % max(1, len(filenames) // 8) == 0:  # Show ~8 labels
                plt.annotate(filename, (file_sizes[i], split_times[i]), 
                           xytext=(5, 5), textcoords='offset points', fontsize=8, alpha=0.7)
        
        plt.xlabel('File Size (MB)', fontsize=14)
        plt.ylabel('Split Time (seconds)', fontsize=14)
        plt.title('GuetzliSplit: Split Time vs File Size', fontsize=16, fontweight='bold')
        plt.grid(True, alpha=0.3)
        plt.legend()
        
        split_graph_path = self.graphs_dir / "split_time_analysis.png"
        plt.savefig(split_graph_path, dpi=300, bbox_inches='tight')
        print(f"Split time graph saved to: {split_graph_path}")
        plt.close()
        
        # Individual Merge Time Graph
        plt.figure(figsize=(12, 8))
        plt.scatter(file_sizes, merge_times, alpha=0.8, s=80, color='green', edgecolors='black')
        
        # Add trend line
        if len(file_sizes) > 1:
            z = np.polyfit(file_sizes, merge_times, 1)
            p = np.poly1d(z)
            plt.plot(file_sizes, p(file_sizes), "r--", alpha=0.8, linewidth=3, label=f'Trend: y = {z[0]:.4f}x + {z[1]:.4f}')
        
        # Add file labels for some points
        for i, filename in enumerate(filenames):
            if i % max(1, len(filenames) // 8) == 0:  # Show ~8 labels
                plt.annotate(filename, (file_sizes[i], merge_times[i]), 
                           xytext=(5, 5), textcoords='offset points', fontsize=8, alpha=0.7)
        
        plt.xlabel('File Size (MB)', fontsize=14)
        plt.ylabel('Merge Time (seconds)', fontsize=14)
        plt.title('GuetzliSplit: Merge Time vs File Size', fontsize=16, fontweight='bold')
        plt.grid(True, alpha=0.3)
        plt.legend()
        
        merge_graph_path = self.graphs_dir / "merge_time_analysis.png"
        plt.savefig(merge_graph_path, dpi=300, bbox_inches='tight')
        print(f"Merge time graph saved to: {merge_graph_path}")
        plt.close()
        
        # Individual Read Time Graph
        plt.figure(figsize=(12, 8))
        plt.scatter(file_sizes, read_times, alpha=0.8, s=80, color='orange', edgecolors='black')
        
        # Add trend line
        if len(file_sizes) > 1:
            z = np.polyfit(file_sizes, read_times, 1)
            p = np.poly1d(z)
            plt.plot(file_sizes, p(file_sizes), "r--", alpha=0.8, linewidth=3, label=f'Trend: y = {z[0]:.4f}x + {z[1]:.4f}')
        
        # Add file labels for some points
        for i, filename in enumerate(filenames):
            if i % max(1, len(filenames) // 8) == 0:  # Show ~8 labels
                plt.annotate(filename, (file_sizes[i], read_times[i]), 
                           xytext=(5, 5), textcoords='offset points', fontsize=8, alpha=0.7)
        
        plt.xlabel('File Size (MB)', fontsize=14)
        plt.ylabel('Read Time (seconds)', fontsize=14)
        plt.title('GuetzliSplit: Read Time vs File Size', fontsize=16, fontweight='bold')
        plt.grid(True, alpha=0.3)
        plt.legend()
        
        read_graph_path = self.graphs_dir / "read_time_analysis.png"
        plt.savefig(read_graph_path, dpi=300, bbox_inches='tight')
        print(f"Read time graph saved to: {read_graph_path}")
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
    
    def run_tests(self):
        """Run all performance tests."""
        print("=== GuetzliSplit Performance Testing ===")
        print(f"Source folder: {self.source_folder}")
        print(f"Output directory: {self.output_dir}")
        print(f"GuetzliSplit path: {self.guetzli_path}")
        
        # Find JPEG files
        jpeg_files = self.find_jpeg_files()
        
        if not jpeg_files:
            print("No JPEG files found in the source folder!")
            return
        
        # Test GuetzliSplit operations
        guetzli_results = self.test_guetzli_directory(self.output_dir, jpeg_files, "GuetzliSplit Operations")
        if guetzli_results:
            self.results['tests'].append(guetzli_results)
            
            # Generate performance graphs
            print("\n=== Generating Performance Graphs ===")
            self.create_performance_graphs(guetzli_results)
        
        # Save results
        self.save_results()
        
        # Cleanup
        self.cleanup()
    



def main():
    parser = argparse.ArgumentParser(description='Test GuetzliSplit performance for JPEG images')
    parser.add_argument('source_folder', help='Path to folder containing JPEG images')
    parser.add_argument('--output-dir', '-o', required=True, help='Path to output directory for GuetzliSplit operations')
    parser.add_argument('--output', help='Output JSON file for results')
    parser.add_argument('--no-cleanup', action='store_true', help='Skip cleanup after testing')
    
    args = parser.parse_args()
    
    # Validate source folder
    if not os.path.exists(args.source_folder):
        print(f"Error: Source folder '{args.source_folder}' does not exist!")
        return 1
    
    # Create tester and run tests
    tester = JPEGPerformanceTester(args.source_folder, args.output_dir, args.output)
    
    try:
        tester.run_tests()
        
        if args.no_cleanup:
            print("\nSkipping cleanup as requested.")
            print(f"Test files remain in: {tester.output_dir}")
        else:
            tester.cleanup()
            
    except KeyboardInterrupt:
        print("\n\nTest interrupted by user.")
        tester.cleanup()
        return 1
    except Exception as e:
        print(f"\nError during testing: {e}")
        tester.cleanup()
        return 1
    
    print("\n=== Testing Complete ===")
    return 0


if __name__ == "__main__":
    exit(main())
