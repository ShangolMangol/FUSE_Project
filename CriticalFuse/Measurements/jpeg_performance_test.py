#!/usr/bin/env python3
"""
JPEG Performance Testing Script

This script measures the performance of writing and reading JPEG images
from /mnt and /storage folders. It includes automatic cleanup functionality.
"""

import os
import shutil
import time
import glob
import argparse
from pathlib import Path
from typing import List, Dict, Tuple
import json
from datetime import datetime


class JPEGPerformanceTester:
    def __init__(self, source_folder: str, output_file: str = None):
        """
        Initialize the performance tester.
        
        Args:
            source_folder: Path to folder containing JPEG images
            output_file: Optional path to save results JSON
        """
        self.source_folder = Path(source_folder)
        self.output_file = output_file
        self.results = {
            'timestamp': datetime.now().isoformat(),
            'source_folder': str(self.source_folder),
            'tests': []
        }
        
        # Test directories
        self.mnt_dir = Path('/mnt/jpeg_test')
        self.storage_dir = Path('/storage/jpeg_test')
        
        # Ensure test directories exist
        self.mnt_dir.mkdir(parents=True, exist_ok=True)
        self.storage_dir.mkdir(parents=True, exist_ok=True)
    
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
    
    def test_directory(self, test_dir: Path, jpeg_files: List[Path], test_name: str) -> Dict:
        """Test write and read performance for a specific directory."""
        print(f"\n=== Testing {test_name} ===")
        
        test_results = {
            'directory': str(test_dir),
            'test_name': test_name,
            'files': []
        }
        
        total_write_time = 0
        total_read_time = 0
        total_size = 0
        
        for i, jpeg_file in enumerate(jpeg_files, 1):
            print(f"Processing {i}/{len(jpeg_files)}: {jpeg_file.name}")
            
            dest_path = test_dir / jpeg_file.name
            
            # Measure write time
            write_time, file_size = self.measure_file_operation(
                self.write_file, jpeg_file, dest_path
            )
            
            # Measure read time
            read_time, _ = self.measure_file_operation(
                self.read_file, dest_path
            )
            
            file_result = {
                'filename': jpeg_file.name,
                'original_size': jpeg_file.stat().st_size,
                'copied_size': file_size,
                'write_time': write_time,
                'read_time': read_time,
                'write_speed_mbps': (file_size / (1024 * 1024)) / write_time if write_time > 0 else 0,
                'read_speed_mbps': (file_size / (1024 * 1024)) / read_time if read_time > 0 else 0
            }
            
            test_results['files'].append(file_result)
            total_write_time += write_time
            total_read_time += read_time
            total_size += file_size
            
            print(f"  Write: {write_time:.4f}s ({file_result['write_speed_mbps']:.2f} MB/s)")
            print(f"  Read:  {read_time:.4f}s ({file_result['read_speed_mbps']:.2f} MB/s)")
        
        # Calculate totals
        test_results['summary'] = {
            'total_files': len(jpeg_files),
            'total_size_mb': total_size / (1024 * 1024),
            'total_write_time': total_write_time,
            'total_read_time': total_read_time,
            'avg_write_speed_mbps': (total_size / (1024 * 1024)) / total_write_time if total_write_time > 0 else 0,
            'avg_read_speed_mbps': (total_size / (1024 * 1024)) / total_read_time if total_read_time > 0 else 0
        }
        
        print(f"\n{test_name} Summary:")
        print(f"  Total files: {test_results['summary']['total_files']}")
        print(f"  Total size: {test_results['summary']['total_size_mb']:.2f} MB")
        print(f"  Total write time: {total_write_time:.4f}s")
        print(f"  Total read time: {total_read_time:.4f}s")
        print(f"  Avg write speed: {test_results['summary']['avg_write_speed_mbps']:.2f} MB/s")
        print(f"  Avg read speed: {test_results['summary']['avg_read_speed_mbps']:.2f} MB/s")
        
        return test_results
    
    def cleanup(self):
        """Clean up test directories."""
        print("\n=== Cleaning up ===")
        
        try:
            if self.mnt_dir.exists():
                shutil.rmtree(self.mnt_dir)
                print(f"Removed {self.mnt_dir}")
            
            if self.storage_dir.exists():
                shutil.rmtree(self.storage_dir)
                print(f"Removed {self.storage_dir}")
                
        except Exception as e:
            print(f"Warning: Could not clean up directories: {e}")
    
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
        print("=== JPEG Performance Testing ===")
        print(f"Source folder: {self.source_folder}")
        
        # Find JPEG files
        jpeg_files = self.find_jpeg_files()
        
        if not jpeg_files:
            print("No JPEG files found in the source folder!")
            return
        
        # Test /mnt directory
        mnt_results = self.test_directory(self.mnt_dir, jpeg_files, "MNT Directory")
        self.results['tests'].append(mnt_results)
        
        # Test /storage directory
        storage_results = self.test_directory(self.storage_dir, jpeg_files, "Storage Directory")
        self.results['tests'].append(storage_results)
        
        # Compare results
        self.compare_results(mnt_results, storage_results)
        
        # Save results
        self.save_results()
        
        # Cleanup
        self.cleanup()
    
    def compare_results(self, mnt_results: Dict, storage_results: Dict):
        """Compare results between MNT and Storage directories."""
        print("\n=== Performance Comparison ===")
        
        mnt_summary = mnt_results['summary']
        storage_summary = storage_results['summary']
        
        print(f"{'Metric':<20} {'MNT':<15} {'Storage':<15} {'Difference':<15}")
        print("-" * 65)
        
        # Write speed comparison
        mnt_write = mnt_summary['avg_write_speed_mbps']
        storage_write = storage_summary['avg_write_speed_mbps']
        write_diff = ((storage_write - mnt_write) / mnt_write * 100) if mnt_write > 0 else 0
        print(f"{'Write Speed (MB/s)':<20} {mnt_write:<15.2f} {storage_write:<15.2f} {write_diff:+.1f}%")
        
        # Read speed comparison
        mnt_read = mnt_summary['avg_read_speed_mbps']
        storage_read = storage_summary['avg_read_speed_mbps']
        read_diff = ((storage_read - mnt_read) / mnt_read * 100) if mnt_read > 0 else 0
        print(f"{'Read Speed (MB/s)':<20} {mnt_read:<15.2f} {storage_read:<15.2f} {read_diff:+.1f}%")
        
        # Total time comparison
        mnt_total = mnt_summary['total_write_time'] + mnt_summary['total_read_time']
        storage_total = storage_summary['total_write_time'] + storage_summary['total_read_time']
        time_diff = ((storage_total - mnt_total) / mnt_total * 100) if mnt_total > 0 else 0
        print(f"{'Total Time (s)':<20} {mnt_total:<15.4f} {storage_total:<15.4f} {time_diff:+.1f}%")


def main():
    parser = argparse.ArgumentParser(description='Test JPEG file I/O performance on /mnt and /storage')
    parser.add_argument('source_folder', help='Path to folder containing JPEG images')
    parser.add_argument('--output', '-o', help='Output JSON file for results')
    parser.add_argument('--no-cleanup', action='store_true', help='Skip cleanup after testing')
    
    args = parser.parse_args()
    
    # Validate source folder
    if not os.path.exists(args.source_folder):
        print(f"Error: Source folder '{args.source_folder}' does not exist!")
        return 1
    
    # Create tester and run tests
    tester = JPEGPerformanceTester(args.source_folder, args.output)
    
    try:
        tester.run_tests()
        
        if args.no_cleanup:
            print("\nSkipping cleanup as requested.")
            print(f"Test files remain in: {tester.mnt_dir} and {tester.storage_dir}")
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
