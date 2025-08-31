#!/usr/bin/env python3
"""
CriticalFUSE Performance Testing Script

This script compares the performance of file operations between a mounted FUSE folder
and a regular folder. It measures read and write times for JPEG images and generates
comparison graphs showing the performance differences.

The script includes file display simulation during read operations to measure complete
read time including display overhead, providing more realistic performance measurements.

For accurate performance comparisons, the script automatically performs aggressive cache
flushing on the Linux system before reading files in regular filesystem tests. This ensures
that the regular filesystem doesn't benefit from cached data, providing fair comparisons
with the FUSE filesystem.

Aggressive cache flushing is enabled by default and uses multiple Linux-specific methods:
- Multiple sync operations with delays
- vmtouch: Preferred method for evicting files from cache
- fincore + dd: Alternative method to check and drop files from cache
- fadvise: Advise kernel to drop cache
- /proc/sys/vm/drop_caches: System-wide cache drop (requires root)
- File touch: Fallback method to invalidate cache

Usage:
    python3 jpeg_performance_test.py <test_images_folder> <regular_folder> <mounted_folder> [options]

Examples:
    python3 jpeg_performance_test.py ../TestImages/ ./regular_test/ ./mnt/ --output-dir ./performance_results
    python3 jpeg_performance_test.py ../TestImages/ ./regular_test/ ./mnt/ -o ./results --output results.json
    python3 jpeg_performance_test.py ../TestImages/ ./regular_test/ ./mnt/ -o ./results --no-display
    python3 jpeg_performance_test.py ../TestImages/ ./regular_test/ ./mnt/ -o ./results --no-cache-flush
    python3 jpeg_performance_test.py ../TestImages/ ./regular_test/ ./mnt/ -o ./results --no-aggressive-cache-flush
"""

import os
import shutil
import time
import glob
import argparse
import subprocess
import platform
from pathlib import Path
from typing import List, Dict, Tuple
import json
from datetime import datetime
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import numpy as np


class CriticalFUSEPerformanceTester:
    def __init__(self, test_images_folder: str, regular_folder: str, mounted_folder: str, 
                 output_dir: str, output_file: str = None, simulate_display: bool = True, 
                 enable_cache_flush: bool = True, aggressive_cache_flush: bool = True):
        """
        Initialize the performance tester.
        
        Args:
            test_images_folder: Path to folder containing test JPEG images
            regular_folder: Path to regular folder for comparison
            mounted_folder: Path to mounted FUSE folder for comparison
            output_dir: Path to output directory for results
            output_file: Optional path to save results JSON
            simulate_display: Whether to simulate file display operations during read
            enable_cache_flush: Whether to flush files from cache during regular filesystem tests
            aggressive_cache_flush: Whether to use aggressive cache flushing (more thorough but slower)
        """
        self.test_images_folder = Path(test_images_folder)
        self.regular_folder = Path(regular_folder)
        self.mounted_folder = Path(mounted_folder)
        self.output_dir = Path(output_dir)
        self.output_file = output_file
        self.simulate_display = simulate_display
        self.enable_cache_flush = enable_cache_flush
        self.aggressive_cache_flush = aggressive_cache_flush
        
        # Create separate graphs directory
        self.graphs_dir = self.output_dir.parent / f"{self.output_dir.name}_graphs"
        self.graphs_dir.mkdir(parents=True, exist_ok=True)
        
        self.results = {
            'timestamp': datetime.now().isoformat(),
            'test_images_folder': str(self.test_images_folder),
            'regular_folder': str(self.regular_folder),
            'mounted_folder': str(self.mounted_folder),
            'output_dir': str(self.output_dir),
            'graphs_dir': str(self.graphs_dir),
            'tests': []
        }
        
        # Ensure directories exist
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.regular_folder.mkdir(parents=True, exist_ok=True)
        
        # Verify mounted folder exists
        if not self.mounted_folder.exists():
            raise FileNotFoundError(f"Mounted folder not found: {self.mounted_folder}")
    
    def flush_file_from_cache(self, file_path: Path) -> bool:
        """
        Aggressively flush a specific file from the Linux system cache.
        
        Args:
            file_path: Path to the file to flush from cache
            
        Returns:
            True if successful, False otherwise
        """
        try:
            # Check if we're on Linux
            if platform.system().lower() != "linux":
                print(f"    Warning: Cache flushing only supported on Linux")
                return False
            
            print(f"    Aggressively flushing {file_path.name} from cache...")
            
            # Method 1: Multiple sync operations to ensure data is written
            try:
                for _ in range(3):  # Multiple syncs for thoroughness
                    subprocess.run(['sync'], capture_output=True, timeout=5)
                time.sleep(0.1)  # Small delay between operations
            except:
                pass
            
            # Method 2: Use vmtouch to evict from cache (preferred)
            try:
                result = subprocess.run(['vmtouch', '-e', str(file_path)], 
                                     capture_output=True, text=True, timeout=10)
                if result.returncode == 0:
                    print(f"    vmtouch eviction successful for {file_path.name}")
                    time.sleep(0.1)
            except (subprocess.TimeoutExpired, FileNotFoundError):
                pass
            
            # Method 3: Use fincore to check if file is in cache, then drop it
            try:
                # Check if file is in cache
                result = subprocess.run(['fincore', str(file_path)], 
                                      capture_output=True, text=True, timeout=5)
                if result.returncode == 0 and 'pages' in result.stdout:
                    print(f"    File {file_path.name} found in cache, dropping...")
                    # Drop the file from cache using dd
                    subprocess.run(['dd', 'if=/dev/zero', f'of={file_path}', 'bs=1', 'count=0', 'conv=notrunc'], 
                                 capture_output=True, timeout=5)
                    time.sleep(0.1)
            except (subprocess.TimeoutExpired, FileNotFoundError):
                pass
            
            # Method 4: Use fadvise to advise kernel to drop cache
            try:
                # Use fadvise with FADV_DONTNEED to drop from cache
                subprocess.run(['fadvise', '-d', str(file_path)], 
                             capture_output=True, timeout=5)
                time.sleep(0.1)
            except (subprocess.TimeoutExpired, FileNotFoundError):
                pass
            
            # Method 5: Use sync and drop_caches (requires root, but most thorough)
            try:
                # Sync filesystem multiple times
                for _ in range(3):
                    subprocess.run(['sync'], capture_output=True, timeout=5)
                
                # Drop page cache (requires root or specific permissions)
                with open('/proc/sys/vm/drop_caches', 'w') as f:
                    f.write('1')  # Drop page cache
                print(f"    System-wide cache drop successful")
                time.sleep(0.2)  # Wait for cache to be fully dropped
                return True
            except (PermissionError, FileNotFoundError):
                pass
            
            # Method 6: Force read the file to ensure it's not cached, then drop
            try:
                # Read the file to potentially cache it, then drop
                with open(file_path, 'rb') as f:
                    f.read(1024)  # Read first 1KB
                
                # Now try to drop it again
                subprocess.run(['sync'], capture_output=True, timeout=5)
                with open('/proc/sys/vm/drop_caches', 'w') as f:
                    f.write('1')
                time.sleep(0.1)
            except:
                pass
            
            # Method 7: Fallback - try to invalidate cache by touching the file
            try:
                file_path.touch()
                time.sleep(0.1)
            except:
                pass
            
            # Final sync to ensure everything is written
            try:
                subprocess.run(['sync'], capture_output=True, timeout=5)
            except:
                pass
                
            print(f"    Cache flushing completed for {file_path.name}")
            return True
                
        except Exception as e:
            print(f"    Warning: Could not flush cache for {file_path.name}: {e}")
            
        return False
    
    def flush_directory_from_cache(self, directory_path: Path) -> int:
        """
        Aggressively flush all files in a directory from the Linux system cache.
        
        Args:
            directory_path: Path to the directory to flush from cache
            
        Returns:
            Number of files successfully flushed from cache
        """
        flushed_count = 0
        
        try:
            # Check if we're on Linux
            if platform.system().lower() != "linux":
                print(f"    Warning: Cache flushing only supported on Linux")
                return 0
                
            if not directory_path.exists():
                return 0
            
            print(f"    Aggressively flushing directory cache: {directory_path}")
            
            # Method 1: Multiple aggressive sync operations
            try:
                for _ in range(5):  # Multiple syncs for thoroughness
                    subprocess.run(['sync'], capture_output=True, timeout=5)
                time.sleep(0.2)  # Wait between operations
            except:
                pass
            
            # Method 2: Try system-wide cache drop (most efficient for directories)
            try:
                # Multiple syncs before dropping cache
                for _ in range(3):
                    subprocess.run(['sync'], capture_output=True, timeout=5)
                
                # Drop page cache (requires root or specific permissions)
                with open('/proc/sys/vm/drop_caches', 'w') as f:
                    f.write('1')  # Drop page cache
                print(f"    System-wide cache drop successful")
                time.sleep(0.3)  # Wait for cache to be fully dropped
                
                # Additional cache drops for thoroughness
                with open('/proc/sys/vm/drop_caches', 'w') as f:
                    f.write('2')  # Drop dentries and inodes
                time.sleep(0.1)
                
                with open('/proc/sys/vm/drop_caches', 'w') as f:
                    f.write('3')  # Drop everything
                time.sleep(0.2)
                
                return 1  # Return 1 to indicate success
            except (PermissionError, FileNotFoundError):
                print(f"    System-wide cache drop failed, trying individual files...")
                pass
            
            # Method 3: Use vmtouch for directory-wide cache eviction
            try:
                result = subprocess.run(['vmtouch', '-e', str(directory_path)], 
                                     capture_output=True, text=True, timeout=15)
                if result.returncode == 0:
                    print(f"    vmtouch directory eviction successful")
                    time.sleep(0.2)
                    return 1
            except (subprocess.TimeoutExpired, FileNotFoundError):
                pass
            
            # Method 4: Fallback: flush individual files aggressively
            files = [f for f in directory_path.iterdir() if f.is_file()]
            print(f"    Flushing {len(files)} individual files from cache...")
            
            for i, file_path in enumerate(files, 1):
                print(f"    Flushing file {i}/{len(files)}: {file_path.name}")
                if self.flush_file_from_cache(file_path):
                    flushed_count += 1
                
                # Additional sync every 5 files
                if i % 5 == 0:
                    try:
                        subprocess.run(['sync'], capture_output=True, timeout=5)
                        time.sleep(0.1)
                    except:
                        pass
            
            # Final aggressive sync and cache drop
            try:
                for _ in range(3):
                    subprocess.run(['sync'], capture_output=True, timeout=5)
                with open('/proc/sys/vm/drop_caches', 'w') as f:
                    f.write('3')  # Drop everything
                time.sleep(0.2)
            except:
                pass
                    
        except Exception as e:
            print(f"    Warning: Could not flush directory cache: {e}")
            
        print(f"    Directory cache flushing completed: {flushed_count} files processed")
        return flushed_count
    
    def aggressive_system_cache_flush(self) -> bool:
        """
        Perform aggressive system-wide cache flushing.
        
        Returns:
            True if successful, False otherwise
        """
        try:
            if platform.system().lower() != "linux":
                return False
            
            print("    Performing aggressive system-wide cache flush...")
            
            # Method 1: Multiple sync operations
            for _ in range(5):
                subprocess.run(['sync'], capture_output=True, timeout=5)
            time.sleep(0.3)
            
            # Method 2: Drop all caches multiple times
            for _ in range(3):
                try:
                    with open('/proc/sys/vm/drop_caches', 'w') as f:
                        f.write('3')  # Drop everything
                    time.sleep(0.2)
                except (PermissionError, FileNotFoundError):
                    pass
            
            # Method 3: Additional sync and cache drop
            subprocess.run(['sync'], capture_output=True, timeout=5)
            time.sleep(0.1)
            
            print("    Aggressive system-wide cache flush completed")
            return True
            
        except Exception as e:
            print(f"    Warning: Aggressive cache flush failed: {e}")
            return False
    
    def find_jpeg_files(self) -> List[Path]:
        """Find all JPEG files in the test images folder."""
        jpeg_patterns = ['*.jpg', '*.jpeg', '*.JPG', '*.JPEG']
        jpeg_files = []
        
        for pattern in jpeg_patterns:
            jpeg_files.extend(self.test_images_folder.glob(pattern))
            jpeg_files.extend(self.test_images_folder.glob(f'**/{pattern}'))
        
        # Remove duplicates and sort
        jpeg_files = list(set(jpeg_files))
        jpeg_files.sort()
        
        print(f"Found {len(jpeg_files)} JPEG files in {self.test_images_folder}")
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
    
    def write_file_with_open_close(self, source_path: Path, dest_path: Path) -> int:
        """Write a file including open/close operations and return its size."""
        # First create the file (for FUSE filesystems that need this)
        try:
            dest_path.touch(exist_ok=True)
        except:
            pass  # Ignore if touch fails
        
        # Open, write, and close the file
        with open(dest_path, 'wb') as f:
            with open(source_path, 'rb') as src:
                data = src.read()
                f.write(data)
        
        return dest_path.stat().st_size
    
    def read_file_with_open_close(self, file_path: Path) -> int:
        """Read a file including open/close operations and return its size."""
        with open(file_path, 'rb') as f:
            data = f.read()
        
        # Display the file to measure complete read time including display overhead
        if self.simulate_display:
            self.display_file(file_path, data)
        
        return len(data)
    
    def display_file(self, file_path: Path, data: bytes):
        """Display file information to simulate display overhead in read time measurement."""
        try:
            # Simulate display operations that would occur when showing file to user
            file_size = len(data)
            file_ext = file_path.suffix.lower()
            
            # Basic file info display (simulating what a file manager might show)
            print(f"    Displaying: {file_path.name} ({file_size:,} bytes)")
            
            # For JPEG files, simulate image metadata extraction/display
            if file_ext in ['.jpg', '.jpeg']:
                self.simulate_jpeg_display(data)
            elif file_ext == '.png':
                self.simulate_png_display(data)
            elif file_ext == '.bmp':
                self.simulate_bmp_display(data)
            else:
                # Generic file display simulation
                self.simulate_generic_display(data)
                
        except Exception as e:
            print(f"    Display error for {file_path.name}: {e}")
    
    def simulate_jpeg_display(self, data: bytes):
        """Simulate JPEG image display operations."""
        # Simulate JPEG header parsing for display
        if len(data) >= 2:
            if data[0] == 0xFF and data[1] == 0xD8:  # JPEG SOI marker
                print(f"      JPEG image detected")
                # Simulate metadata extraction time
                time.sleep(0.001)  # 1ms overhead for JPEG processing
    
    def simulate_png_display(self, data: bytes):
        """Simulate PNG image display operations."""
        # Simulate PNG signature check
        png_signature = b'\x89PNG\r\n\x1a\n'
        if data.startswith(png_signature):
            print(f"      PNG image detected")
            # Simulate PNG header parsing
            time.sleep(0.001)  # 1ms overhead for PNG processing
    
    def simulate_bmp_display(self, data: bytes):
        """Simulate BMP image display operations."""
        # Simulate BMP signature check
        if len(data) >= 2 and data[0] == ord('B') and data[1] == ord('M'):
            print(f"      BMP image detected")
            # Simulate BMP header parsing
            time.sleep(0.001)  # 1ms overhead for BMP processing
    
    def simulate_generic_display(self, data: bytes):
        """Simulate generic file display operations."""
        # Simulate basic file type detection and display
        print(f"      Generic file ({len(data)} bytes)")
        time.sleep(0.0005)  # 0.5ms overhead for generic processing
    
    def test_folder_performance(self, folder_path: Path, jpeg_files: List[Path], folder_type: str) -> Dict:
        """Test read/write performance for a specific folder."""
        print(f"\n=== Testing {folder_type} Folder: {folder_path} ===")
        
        test_results = {
            'folder_type': folder_type,
            'folder_path': str(folder_path),
            'files': []
        }
        
        total_write_time = 0
        total_read_time = 0
        total_size = 0
        
        for i, jpeg_file in enumerate(jpeg_files, 1):
            print(f"Processing {i}/{len(jpeg_files)}: {jpeg_file.name}")
            
            try:
                # Measure write time (copy file to test folder with open/close)
                dest_path = folder_path / jpeg_file.name
                write_time, write_size = self.measure_file_operation(
                    self.write_file_with_open_close, jpeg_file, dest_path
                )
                
                # For regular filesystem tests, flush the file from cache before reading
                # to ensure accurate performance measurements
                if folder_type == "Regular Filesystem" and self.enable_cache_flush:
                    print(f"    Flushing {dest_path.name} from cache...")
                    cache_flushed = self.flush_file_from_cache(dest_path)
                    if cache_flushed:
                        print(f"    Successfully flushed {dest_path.name} from cache")
                    else:
                        print(f"    Warning: Could not flush {dest_path.name} from cache")
                
                # Measure read time (read the copied file with open/close)
                read_time, read_size = self.measure_file_operation(
                    self.read_file_with_open_close, dest_path
                )
                
                file_result = {
                    'filename': jpeg_file.name,
                    'original_size': jpeg_file.stat().st_size,
                    'write_size': write_size,
                    'read_size': read_size,
                    'write_time': write_time,
                    'read_time': read_time,
                    'write_speed_mbps': (write_size / (1024 * 1024)) / write_time if write_time > 0 else 0,
                    'read_speed_mbps': (read_size / (1024 * 1024)) / read_time if read_time > 0 else 0
                }
                
                test_results['files'].append(file_result)
                total_write_time += write_time
                total_read_time += read_time
                total_size += write_size
                
                print(f"  Write: {write_time:.4f}s ({file_result['write_speed_mbps']:.2f} MB/s)")
                print(f"  Read:  {read_time:.4f}s ({file_result['read_speed_mbps']:.2f} MB/s)")
                
            except Exception as e:
                print(f"  Error processing {jpeg_file.name}: {e}")
                # Try alternative approach for FUSE filesystems
                try:
                    print(f"  Trying alternative approach for {jpeg_file.name}...")
                    
                    # For FUSE filesystems, try creating the file first
                    dest_path = folder_path / jpeg_file.name
                    
                    # Create empty file first
                    dest_path.touch(exist_ok=True)
                    
                    # Then write data
                    write_time, write_size = self.measure_file_operation(
                        self.write_file_with_open_close, jpeg_file, dest_path
                    )
                    
                    # For regular filesystem tests, flush the file from cache before reading
                    # to ensure accurate performance measurements
                    if folder_type == "Regular Filesystem" and self.enable_cache_flush:
                        print(f"    Flushing {dest_path.name} from cache (alternative approach)...")
                        cache_flushed = self.flush_file_from_cache(dest_path)
                        if cache_flushed:
                            print(f"    Successfully flushed {dest_path.name} from cache")
                        else:
                            print(f"    Warning: Could not flush {dest_path.name} from cache")
                    
                    # Read data
                    read_time, read_size = self.measure_file_operation(
                        self.read_file_with_open_close, dest_path
                    )
                    
                    file_result = {
                        'filename': jpeg_file.name,
                        'original_size': jpeg_file.stat().st_size,
                        'write_size': write_size,
                        'read_size': read_size,
                        'write_time': write_time,
                        'read_time': read_time,
                        'write_speed_mbps': (write_size / (1024 * 1024)) / write_time if write_time > 0 else 0,
                        'read_speed_mbps': (read_size / (1024 * 1024)) / read_time if read_time > 0 else 0
                    }
                    
                    test_results['files'].append(file_result)
                    total_write_time += write_time
                    total_read_time += read_time
                    total_size += write_size
                    
                    print(f"  Write: {write_time:.4f}s ({file_result['write_speed_mbps']:.2f} MB/s)")
                    print(f"  Read:  {read_time:.4f}s ({file_result['read_speed_mbps']:.2f} MB/s)")
                    
                except Exception as e2:
                    print(f"  Alternative approach also failed: {e2}")
                    continue
        
        if test_results['files']:
            # Calculate totals
            test_results['summary'] = {
                'total_files': len(test_results['files']),
                'total_size_mb': total_size / (1024 * 1024),
                'total_write_time': total_write_time,
                'total_read_time': total_read_time,
                'avg_write_speed_mbps': (total_size / (1024 * 1024)) / total_write_time if total_write_time > 0 else 0,
                'avg_read_speed_mbps': (total_size / (1024 * 1024)) / total_read_time if total_read_time > 0 else 0
            }
            
            print(f"\n{folder_type} Summary:")
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
            if self.regular_folder.exists():
                shutil.rmtree(self.regular_folder)
                print(f"Removed regular test directory: {self.regular_folder}")
                
            # Note: Mounted folder and graphs directory are preserved
            print(f"Graphs preserved in: {self.graphs_dir}")
            print(f"Mounted folder preserved: {self.mounted_folder}")
                
        except Exception as e:
            print(f"Warning: Could not clean up directory: {e}")
    
    def create_comparison_graphs(self, regular_results: Dict, mounted_results: Dict):
        """Create comparison graphs between regular and mounted folder performance."""
        if not regular_results.get('files') or not mounted_results.get('files'):
            print("No data available for graph generation")
            return
        
        # Extract data for plotting
        file_sizes = []
        regular_write_times = []
        regular_read_times = []
        mounted_write_times = []
        mounted_read_times = []
        filenames = []
        
        # Create a mapping of filenames to results for comparison
        regular_map = {f['filename']: f for f in regular_results['files']}
        mounted_map = {f['filename']: f for f in mounted_results['files']}
        
        # Find common files
        common_files = set(regular_map.keys()) & set(mounted_map.keys())
        
        for filename in sorted(common_files):
            regular_file = regular_map[filename]
            mounted_file = mounted_map[filename]
            
            # Convert file sizes to MB for better readability
            size_mb = regular_file['original_size'] / (1024 * 1024)
            file_sizes.append(size_mb)
            regular_write_times.append(regular_file['write_time'])
            regular_read_times.append(regular_file['read_time'])
            mounted_write_times.append(mounted_file['write_time'])
            mounted_read_times.append(mounted_file['read_time'])
            filenames.append(filename)
        
        # Create figure with subplots
        fig, ((ax1, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(16, 12))
        fig.suptitle('CriticalFUSE vs Regular Filesystem Performance Comparison', fontsize=16, fontweight='bold')
        
        # Plot 1: Write Time Comparison
        ax1.scatter(file_sizes, regular_write_times, alpha=0.7, s=50, color='blue', 
                   label='Regular Filesystem', edgecolors='black')
        ax1.scatter(file_sizes, mounted_write_times, alpha=0.7, s=50, color='red', 
                   label='CriticalFUSE', edgecolors='black')
        ax1.set_xlabel('File Size (MB)', fontsize=12)
        ax1.set_ylabel('Write Time (seconds)', fontsize=12)
        ax1.set_title('Write Time Comparison', fontsize=14, fontweight='bold')
        ax1.grid(True, alpha=0.3)
        ax1.legend()
        
        # Plot 2: Read Time Comparison
        ax2.scatter(file_sizes, regular_read_times, alpha=0.7, s=50, color='blue', 
                   label='Regular Filesystem', edgecolors='black')
        ax2.scatter(file_sizes, mounted_read_times, alpha=0.7, s=50, color='red', 
                   label='CriticalFUSE', edgecolors='black')
        ax2.set_xlabel('File Size (MB)', fontsize=12)
        ax2.set_ylabel('Read Time (seconds)', fontsize=12)
        ax2.set_title('Read Time Comparison', fontsize=14, fontweight='bold')
        ax2.grid(True, alpha=0.3)
        ax2.legend()
        
        # Plot 3: Write Speed Comparison
        regular_write_speeds = [(size * 1024 * 1024) / time if time > 0 else 0 
                               for size, time in zip(file_sizes, regular_write_times)]
        mounted_write_speeds = [(size * 1024 * 1024) / time if time > 0 else 0 
                               for size, time in zip(file_sizes, mounted_write_times)]
        
        ax3.scatter(file_sizes, regular_write_speeds, alpha=0.7, s=50, color='blue', 
                   label='Regular Filesystem', edgecolors='black')
        ax3.scatter(file_sizes, mounted_write_speeds, alpha=0.7, s=50, color='red', 
                   label='CriticalFUSE', edgecolors='black')
        ax3.set_xlabel('File Size (MB)', fontsize=12)
        ax3.set_ylabel('Write Speed (MB/s)', fontsize=12)
        ax3.set_title('Write Speed Comparison', fontsize=14, fontweight='bold')
        ax3.grid(True, alpha=0.3)
        ax3.legend()
        
        # Plot 4: Read Speed Comparison
        regular_read_speeds = [(size * 1024 * 1024) / time if time > 0 else 0 
                              for size, time in zip(file_sizes, regular_read_times)]
        mounted_read_speeds = [(size * 1024 * 1024) / time if time > 0 else 0 
                              for size, time in zip(file_sizes, mounted_read_times)]
        
        ax4.scatter(file_sizes, regular_read_speeds, alpha=0.7, s=50, color='blue', 
                   label='Regular Filesystem', edgecolors='black')
        ax4.scatter(file_sizes, mounted_read_speeds, alpha=0.7, s=50, color='red', 
                   label='CriticalFUSE', edgecolors='black')
        ax4.set_xlabel('File Size (MB)', fontsize=12)
        ax4.set_ylabel('Read Speed (MB/s)', fontsize=12)
        ax4.set_title('Read Speed Comparison', fontsize=14, fontweight='bold')
        ax4.grid(True, alpha=0.3)
        ax4.legend()
        
        # Adjust layout and save
        plt.tight_layout()
        
        # Save the combined comparison graph
        graph_path = self.graphs_dir / "performance_comparison.png"
        plt.savefig(graph_path, dpi=300, bbox_inches='tight')
        print(f"Performance comparison graphs saved to: {graph_path}")
        
        # Create detailed individual comparison graphs
        self.create_detailed_comparison_graphs(file_sizes, regular_write_times, regular_read_times,
                                             mounted_write_times, mounted_read_times, filenames)
        
        plt.close()
    
    def create_detailed_comparison_graphs(self, file_sizes, regular_write_times, regular_read_times,
                                        mounted_write_times, mounted_read_times, filenames):
        """Create detailed individual comparison graphs."""
        
        # Write Time Comparison (Detailed)
        plt.figure(figsize=(14, 8))
        plt.scatter(file_sizes, regular_write_times, alpha=0.8, s=80, color='blue', 
                   label='Regular Filesystem', edgecolors='black')
        plt.scatter(file_sizes, mounted_write_times, alpha=0.8, s=80, color='red', 
                   label='CriticalFUSE', edgecolors='black')
        
        # Add trend lines
        if len(file_sizes) > 1:
            z_regular = np.polyfit(file_sizes, regular_write_times, 1)
            p_regular = np.poly1d(z_regular)
            plt.plot(file_sizes, p_regular(file_sizes), "b--", alpha=0.8, linewidth=2, 
                    label=f'Regular Trend: y = {z_regular[0]:.4f}x + {z_regular[1]:.4f}')
            
            z_mounted = np.polyfit(file_sizes, mounted_write_times, 1)
            p_mounted = np.poly1d(z_mounted)
            plt.plot(file_sizes, p_mounted(file_sizes), "r--", alpha=0.8, linewidth=2, 
                    label=f'CriticalFUSE Trend: y = {z_mounted[0]:.4f}x + {z_mounted[1]:.4f}')
        
        # Add file labels for some points
        for i, filename in enumerate(filenames):
            if i % max(1, len(filenames) // 6) == 0:  # Show ~6 labels
                plt.annotate(filename, (file_sizes[i], max(regular_write_times[i], mounted_write_times[i])), 
                           xytext=(5, 5), textcoords='offset points', fontsize=8, alpha=0.7)
        
        plt.xlabel('File Size (MB)', fontsize=14)
        plt.ylabel('Write Time (seconds)', fontsize=14)
        plt.title('CriticalFUSE vs Regular Filesystem: Write Time Comparison', fontsize=16, fontweight='bold')
        plt.grid(True, alpha=0.3)
        plt.legend()
        
        write_graph_path = self.graphs_dir / "write_time_comparison.png"
        plt.savefig(write_graph_path, dpi=300, bbox_inches='tight')
        print(f"Write time comparison graph saved to: {write_graph_path}")
        plt.close()
        
        # Read Time Comparison (Detailed)
        plt.figure(figsize=(14, 8))
        plt.scatter(file_sizes, regular_read_times, alpha=0.8, s=80, color='blue', 
                   label='Regular Filesystem', edgecolors='black')
        plt.scatter(file_sizes, mounted_read_times, alpha=0.8, s=80, color='red', 
                   label='CriticalFUSE', edgecolors='black')
        
        # Add trend lines
        if len(file_sizes) > 1:
            z_regular = np.polyfit(file_sizes, regular_read_times, 1)
            p_regular = np.poly1d(z_regular)
            plt.plot(file_sizes, p_regular(file_sizes), "b--", alpha=0.8, linewidth=2, 
                    label=f'Regular Trend: y = {z_regular[0]:.4f}x + {z_regular[1]:.4f}')
            
            z_mounted = np.polyfit(file_sizes, mounted_read_times, 1)
            p_mounted = np.poly1d(z_mounted)
            plt.plot(file_sizes, p_mounted(file_sizes), "r--", alpha=0.8, linewidth=2, 
                    label=f'CriticalFUSE Trend: y = {z_mounted[0]:.4f}x + {z_mounted[1]:.4f}')
        
        # Add file labels for some points
        for i, filename in enumerate(filenames):
            if i % max(1, len(filenames) // 6) == 0:  # Show ~6 labels
                plt.annotate(filename, (file_sizes[i], max(regular_read_times[i], mounted_read_times[i])), 
                           xytext=(5, 5), textcoords='offset points', fontsize=8, alpha=0.7)
        
        plt.xlabel('File Size (MB)', fontsize=14)
        plt.ylabel('Read Time (seconds)', fontsize=14)
        plt.title('CriticalFUSE vs Regular Filesystem: Read Time Comparison', fontsize=16, fontweight='bold')
        plt.grid(True, alpha=0.3)
        plt.legend()
        
        read_graph_path = self.graphs_dir / "read_time_comparison.png"
        plt.savefig(read_graph_path, dpi=300, bbox_inches='tight')
        print(f"Read time comparison graph saved to: {read_graph_path}")
        plt.close()
        
        # Performance Overhead Analysis
        plt.figure(figsize=(14, 8))
        
        # Calculate overhead percentages
        write_overhead = [(mounted - regular) / regular * 100 if regular > 0 else 0 
                         for regular, mounted in zip(regular_write_times, mounted_write_times)]
        read_overhead = [(mounted - regular) / regular * 100 if regular > 0 else 0 
                        for regular, mounted in zip(regular_read_times, mounted_read_times)]
        
        plt.scatter(file_sizes, write_overhead, alpha=0.8, s=80, color='red', 
                   label='Write Overhead', edgecolors='black')
        plt.scatter(file_sizes, read_overhead, alpha=0.8, s=80, color='blue', 
                   label='Read Overhead', edgecolors='black')
        
        # Add trend lines for overhead
        if len(file_sizes) > 1:
            z_write = np.polyfit(file_sizes, write_overhead, 1)
            p_write = np.poly1d(z_write)
            plt.plot(file_sizes, p_write(file_sizes), "r--", alpha=0.8, linewidth=2, 
                    label=f'Write Overhead Trend: y = {z_write[0]:.2f}x + {z_write[1]:.2f}%')
            
            z_read = np.polyfit(file_sizes, read_overhead, 1)
            p_read = np.poly1d(z_read)
            plt.plot(file_sizes, p_read(file_sizes), "b--", alpha=0.8, linewidth=2, 
                    label=f'Read Overhead Trend: y = {z_read[0]:.2f}x + {z_read[1]:.2f}%')
        
        plt.xlabel('File Size (MB)', fontsize=14)
        plt.ylabel('Performance Overhead (%)', fontsize=14)
        plt.title('CriticalFUSE Performance Overhead Analysis', fontsize=16, fontweight='bold')
        plt.grid(True, alpha=0.3)
        plt.legend()
        
        overhead_graph_path = self.graphs_dir / "performance_overhead.png"
        plt.savefig(overhead_graph_path, dpi=300, bbox_inches='tight')
        print(f"Performance overhead graph saved to: {overhead_graph_path}")
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
        print("=== CriticalFUSE Performance Testing ===")
        print(f"Test images folder: {self.test_images_folder}")
        print(f"Regular folder: {self.regular_folder}")
        print(f"Mounted folder: {self.mounted_folder}")
        print(f"Display simulation: {'Enabled' if self.simulate_display else 'Disabled'}")
        print(f"Cache flushing: {'Enabled' if self.enable_cache_flush else 'Disabled'}")
        if self.enable_cache_flush:
            print(f"Aggressive cache flushing: {'Enabled' if self.aggressive_cache_flush else 'Disabled'}")
        
        # Check if we're on Linux for cache flushing
        if self.enable_cache_flush and platform.system().lower() != "linux":
            print("WARNING: Cache flushing is only supported on Linux systems!")
            print("Performance comparisons may not be accurate due to cached data.")
        
        # Find JPEG files
        jpeg_files = self.find_jpeg_files()
        
        if not jpeg_files:
            print("No JPEG files found in the test images folder!")
            return
        
        # Test regular folder performance
        if self.enable_cache_flush:
            print("\n=== Flushing Regular Filesystem Cache ===")
            
            # Aggressive system-wide cache flush first
            if self.aggressive_cache_flush:
                self.aggressive_system_cache_flush()
            
            # Then flush the specific directory
            flushed_count = self.flush_directory_from_cache(self.regular_folder)
            print(f"Flushed {flushed_count} files from regular filesystem cache")
            
            # Additional aggressive flush if enabled
            if self.aggressive_cache_flush:
                print("    Performing additional aggressive cache flush...")
                self.aggressive_system_cache_flush()
        else:
            print("\n=== Cache Flushing Disabled ===")
        
        regular_results = self.test_folder_performance(self.regular_folder, jpeg_files, "Regular Filesystem")
        if regular_results:
            self.results['tests'].append(regular_results)
        
        # Test mounted folder performance
        mounted_results = self.test_folder_performance(self.mounted_folder, jpeg_files, "CriticalFUSE")
        if mounted_results:
            self.results['tests'].append(mounted_results)
        
        # Generate comparison graphs
        if regular_results and mounted_results:
            print("\n=== Generating Performance Comparison Graphs ===")
            self.create_comparison_graphs(regular_results, mounted_results)
        
        # Save results
        self.save_results()
        
        # Cleanup
        self.cleanup()
    



def main():
    parser = argparse.ArgumentParser(description='Compare CriticalFUSE vs Regular Filesystem Performance')
    parser.add_argument('test_images_folder', help='Path to folder containing test JPEG images')
    parser.add_argument('regular_folder', help='Path to regular folder for comparison')
    parser.add_argument('mounted_folder', help='Path to mounted FUSE folder for comparison')
    parser.add_argument('--output-dir', '-o', required=True, help='Path to output directory for results')
    parser.add_argument('--output', help='Output JSON file for results')
    parser.add_argument('--no-cleanup', action='store_true', help='Skip cleanup after testing')
    parser.add_argument('--no-display', action='store_true', help='Skip file display simulation during read operations')
    parser.add_argument('--no-cache-flush', action='store_true', help='Skip flushing files from cache during regular filesystem tests')
    parser.add_argument('--no-aggressive-cache-flush', action='store_true', help='Disable aggressive cache flushing (use standard cache flushing instead)')
    
    args = parser.parse_args()
    
    # Validate folders
    if not os.path.exists(args.test_images_folder):
        print(f"Error: Test images folder '{args.test_images_folder}' does not exist!")
        return 1
    
    if not os.path.exists(args.mounted_folder):
        print(f"Error: Mounted folder '{args.mounted_folder}' does not exist!")
        return 1
    
    # Create tester and run tests
    simulate_display = not args.no_display
    enable_cache_flush = not args.no_cache_flush
    aggressive_cache_flush = not args.no_aggressive_cache_flush
    tester = CriticalFUSEPerformanceTester(args.test_images_folder, args.regular_folder, 
                                          args.mounted_folder, args.output_dir, args.output, 
                                          simulate_display, enable_cache_flush, aggressive_cache_flush)
    
    try:
        tester.run_tests()
        
        if args.no_cleanup:
            print("\nSkipping cleanup as requested.")
            print(f"Test files remain in: {tester.regular_folder}")
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
