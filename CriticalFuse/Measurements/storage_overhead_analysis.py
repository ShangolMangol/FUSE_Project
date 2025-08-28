#!/usr/bin/env python3
"""
CriticalFUSE Storage Overhead Analysis Script

This script measures the storage overhead caused by the FUSE system by:
1. Taking files from a source folder
2. Writing them to a mounted FUSE folder
3. Analyzing the storage folder to find all related files (e.g., .crit, .ac.noncrit)
4. Comparing original file size with total storage used
5. Generating graphs showing storage overhead

Usage:
    python3 storage_overhead_analysis.py <source_folder> <mounted_folder> <storage_folder> [options]

Examples:
    python3 storage_overhead_analysis.py ./TestImages ./mnt ./storage --output-dir ./overhead_results
    python3 storage_overhead_analysis.py ./TestImages ./mnt ./storage -o ./results --max-files 10
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
import glob


class StorageOverheadAnalyzer:
    def __init__(self, source_folder: str, mounted_folder: str, storage_folder: str, 
                 output_dir: str, output_file: str = None, max_files: int = None):
        """
        Initialize the storage overhead analyzer.
        
        Args:
            source_folder: Path to folder containing original files
            mounted_folder: Path to mounted FUSE folder
            storage_folder: Path to storage folder where FUSE creates its files
            output_dir: Path to output directory for results
            output_file: Optional path to save results JSON
            max_files: Maximum number of files to analyze
        """
        self.source_folder = Path(source_folder)
        self.mounted_folder = Path(mounted_folder)
        self.storage_folder = Path(storage_folder)
        self.output_dir = Path(output_dir)
        self.output_file = output_file
        self.max_files = max_files
        
        # Create separate graphs directory
        self.graphs_dir = self.output_dir.parent / f"{self.output_dir.name}_graphs"
        self.graphs_dir.mkdir(parents=True, exist_ok=True)
        
        self.results = {
            'timestamp': datetime.now().isoformat(),
            'source_folder': str(self.source_folder),
            'mounted_folder': str(self.mounted_folder),
            'storage_folder': str(self.storage_folder),
            'output_dir': str(self.output_dir),
            'graphs_dir': str(self.graphs_dir),
            'max_files': self.max_files,
            'tests': []
        }
        
        # Ensure output directory exists
        self.output_dir.mkdir(parents=True, exist_ok=True)
        
        # Verify folders exist
        if not self.source_folder.exists():
            raise FileNotFoundError(f"Source folder not found: {self.source_folder}")
        
        if not self.mounted_folder.exists():
            raise FileNotFoundError(f"Mounted folder not found: {self.mounted_folder}")
        
        if not self.storage_folder.exists():
            raise FileNotFoundError(f"Storage folder not found: {self.storage_folder}")
    
    def find_source_files(self) -> List[Path]:
        """Find all files in the source folder."""
        # Look for common image extensions
        image_extensions = ['*.jpg', '*.jpeg', '*.png', '*.bmp', '*.tiff', '*.tif', '*.dng']
        source_files = []
        
        for ext in image_extensions:
            source_files.extend(list(self.source_folder.glob(ext)))
            source_files.extend(list(self.source_folder.glob(ext.upper())))
        
        # Remove duplicates and sort
        source_files = list(set(source_files))
        source_files.sort()
        
        print(f"Found {len(source_files)} files in {self.source_folder}")
        
        # Limit the number of files if specified
        if self.max_files and len(source_files) > self.max_files:
            source_files = source_files[:self.max_files]
            print(f"Limited to {self.max_files} files")
        
        return source_files
    
    def write_file_to_mount(self, source_file: Path) -> bool:
        """
        Write a file to the mounted FUSE folder.
        
        Args:
            source_file: Path to source file
            
        Returns:
            True if successful, False otherwise
        """
        try:
            dest_path = self.mounted_folder / source_file.name
            
            # Read the source file and write it directly to the mounted folder
            with open(source_file, 'rb') as src:
                with open(dest_path, 'wb') as dst:
                    dst.write(src.read())
            
            # Wait a moment for FUSE to process the file
            time.sleep(0.5)
            
            print(f"Successfully wrote {source_file.name} to mounted folder")
            return True
            
        except Exception as e:
            print(f"Error writing {source_file.name} to mounted folder: {e}")
            return False
    
    def find_related_storage_files(self, base_filename: str) -> List[Path]:
        """
        Find all storage files related to a given base filename.
        
        Args:
            base_filename: Base filename (e.g., 'image.jpg')
            
        Returns:
            List of related storage files
        """
        # Get the base name without extension
        base_name = Path(base_filename).stem
        base_ext = Path(base_filename).suffix
        
        # Look for files that start with the base name
        related_files = []
        
        # Search for files with patterns like:
        # - base_name.ext.crit
        # - base_name.ext.ac.noncrit
        # - base_name.ext.*
        patterns = [
            f"{base_name}{base_ext}.*",
            f"{base_name}{base_ext}.crit",
            f"{base_name}{base_ext}.ac.noncrit",
            f"{base_name}.*"
        ]
        
        for pattern in patterns:
            found_files = list(self.storage_folder.glob(pattern))
            related_files.extend(found_files)
        
        # Remove duplicates
        related_files = list(set(related_files))
        related_files.sort()
        
        return related_files
    
    def detect_jpeg_type(self, file_path: Path) -> str:
        """
        Detect if a JPEG file is progressive or baseline using ImageMagick identify command.
        
        Args:
            file_path: Path to JPEG file
            
        Returns:
            'progressive', 'baseline', or 'unknown'
        """
        try:
            import subprocess
            
            # Use ImageMagick identify command to get interlace information
            result = subprocess.run(
                ['identify', '-verbose', str(file_path)],
                capture_output=True,
                text=True,
                timeout=10  # 10 second timeout
            )
            
            if result.returncode == 0:
                # Look for Interlace line in the output
                for line in result.stdout.split('\n'):
                    if 'Interlace:' in line:
                        interlace_value = line.split('Interlace:')[1].strip()
                        if interlace_value == 'Line':
                            return 'progressive'
                        elif interlace_value == 'No':
                            return 'baseline'
                        else:
                            return 'unknown'
                
                # If no Interlace line found, assume baseline
                return 'baseline'
            else:
                print(f"ImageMagick identify failed for {file_path.name}: {result.stderr}")
                return 'unknown'
                
        except subprocess.TimeoutExpired:
            print(f"ImageMagick identify timeout for {file_path.name}")
            return 'unknown'
        except FileNotFoundError:
            print("ImageMagick 'identify' command not found. Please install ImageMagick.")
            return 'unknown'
        except Exception as e:
            print(f"Error detecting JPEG type for {file_path.name}: {e}")
            return 'unknown'

    def batch_detect_jpeg_types(self, jpeg_files: List[Path]) -> Dict[str, str]:
        """
        Efficiently detect JPEG types for multiple files using a single ImageMagick command.
        
        Args:
            jpeg_files: List of JPEG file paths
            
        Returns:
            Dictionary mapping filename to JPEG type
        """
        jpeg_types = {}
        
        if not jpeg_files:
            return jpeg_types
        
        try:
            import subprocess
            
            # Use ImageMagick identify command on all files at once
            file_paths = [str(f) for f in jpeg_files]
            result = subprocess.run(
                ['identify', '-verbose'] + file_paths,
                capture_output=True,
                text=True,
                timeout=30  # Longer timeout for multiple files
            )
            
            if result.returncode == 0:
                current_file = None
                for line in result.stdout.split('\n'):
                    # Check if this line contains a filename
                    if any(f.name in line for f in jpeg_files):
                        # Extract filename from the line
                        for f in jpeg_files:
                            if f.name in line:
                                current_file = f.name
                                break
                    elif 'Interlace:' in line and current_file:
                        interlace_value = line.split('Interlace:')[1].strip()
                        if interlace_value == 'Line':
                            jpeg_types[current_file] = 'progressive'
                        elif interlace_value == 'No':
                            jpeg_types[current_file] = 'baseline'
                        else:
                            jpeg_types[current_file] = 'unknown'
                        current_file = None
                
                # Set default for any files not found in output
                for f in jpeg_files:
                    if f.name not in jpeg_types:
                        jpeg_types[f.name] = 'baseline'  # Default assumption
                        
            else:
                print(f"ImageMagick identify failed: {result.stderr}")
                # Fallback to individual detection
                for f in jpeg_files:
                    jpeg_types[f.name] = self.detect_jpeg_type(f)
                    
        except subprocess.TimeoutExpired:
            print("ImageMagick identify timeout for batch processing")
            # Fallback to individual detection
            for f in jpeg_files:
                jpeg_types[f.name] = self.detect_jpeg_type(f)
        except FileNotFoundError:
            print("ImageMagick 'identify' command not found. Please install ImageMagick.")
            # Fallback to individual detection
            for f in jpeg_files:
                jpeg_types[f.name] = self.detect_jpeg_type(f)
        except Exception as e:
            print(f"Error in batch JPEG type detection: {e}")
            # Fallback to individual detection
            for f in jpeg_files:
                jpeg_types[f.name] = self.detect_jpeg_type(f)
        
        return jpeg_types

    def calculate_storage_overhead(self, source_file: Path, jpeg_type: str = None) -> Dict:
        """
        Calculate storage overhead for a single file.
        
        Args:
            source_file: Path to source file
            jpeg_type: Pre-detected JPEG type (optional)
            
        Returns:
            Dictionary with storage overhead analysis
        """
        print(f"Analyzing storage overhead for: {source_file.name}")
        
        # Get original file size
        original_size = source_file.stat().st_size
        
        # Use provided JPEG type or detect if needed
        if jpeg_type is None and source_file.suffix.lower() in ['.jpg', '.jpeg']:
            jpeg_type = self.detect_jpeg_type(source_file)
            print(f"  JPEG type detected: {jpeg_type}")
        elif jpeg_type is not None:
            print(f"  JPEG type: {jpeg_type}")
        else:
            jpeg_type = 'unknown'
        
        # Write file to mounted folder
        if not self.write_file_to_mount(source_file):
            print(f"Failed to write {source_file.name} to mounted folder")
            return None
        
        # Find related storage files
        related_files = self.find_related_storage_files(source_file.name)
        
        if not related_files:
            print(f"No related storage files found for {source_file.name}")
            return None
        
        # Calculate total storage size
        total_storage_size = sum(f.stat().st_size for f in related_files)
        
        # Calculate overhead
        overhead_bytes = total_storage_size - original_size
        overhead_percentage = (overhead_bytes / original_size * 100) if original_size > 0 else 0
        
        # Create detailed file breakdown
        file_breakdown = []
        for file_path in related_files:
            file_breakdown.append({
                'filename': file_path.name,
                'size_bytes': file_path.stat().st_size,
                'size_mb': file_path.stat().st_size / (1024 * 1024)
            })
        
        result = {
            'filename': source_file.name,
            'original_size_bytes': original_size,
            'original_size_mb': original_size / (1024 * 1024),
            'total_storage_bytes': total_storage_size,
            'total_storage_mb': total_storage_size / (1024 * 1024),
            'overhead_bytes': overhead_bytes,
            'overhead_mb': overhead_bytes / (1024 * 1024),
            'overhead_percentage': overhead_percentage,
            'related_files': file_breakdown,
            'file_count': len(related_files),
            'jpeg_type': jpeg_type
        }
        
        print(f"  Original size: {result['original_size_mb']:.2f} MB")
        print(f"  Total storage: {result['total_storage_mb']:.2f} MB")
        print(f"  Overhead: {result['overhead_mb']:.2f} MB ({overhead_percentage:.1f}%)")
        print(f"  Related files: {len(related_files)}")
        for file_info in file_breakdown:
            print(f"    {file_info['filename']}: {file_info['size_mb']:.2f} MB")
        
        return result
    
    def create_storage_overhead_graphs(self, analysis_results: List[Dict]):
        """Create graphs showing storage overhead analysis."""
        if not analysis_results:
            print("No data available for graph generation")
            return
        
        # Extract data for plotting
        original_sizes = [r['original_size_mb'] for r in analysis_results]
        overhead_sizes = [r['overhead_mb'] for r in analysis_results]
        overhead_percentages = [r['overhead_percentage'] for r in analysis_results]
        total_storage_sizes = [r['total_storage_mb'] for r in analysis_results]
        filenames = [r['filename'] for r in analysis_results]
        
        # Create figure with subplots
        fig, ((ax1, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(16, 12))
        fig.suptitle('CriticalFUSE Storage Overhead Analysis', fontsize=16, fontweight='bold')
        
        # Plot 1: Storage Overhead vs Original File Size
        ax1.scatter(original_sizes, overhead_sizes, alpha=0.7, s=60, color='red', edgecolors='black')
        ax1.set_xlabel('Original File Size (MB)', fontsize=12)
        ax1.set_ylabel('Storage Overhead (MB)', fontsize=12)
        ax1.set_title('Storage Overhead vs Original File Size', fontsize=14, fontweight='bold')
        ax1.grid(True, alpha=0.3)
        
        # Add trend line
        if len(original_sizes) > 1:
            z = np.polyfit(original_sizes, overhead_sizes, 1)
            p = np.poly1d(z)
            ax1.plot(original_sizes, p(original_sizes), "r--", alpha=0.8, linewidth=2,
                    label=f'Trend: y = {z[0]:.4f}x + {z[1]:.4f}')
            ax1.legend()
        
        # Plot 2: Overhead Percentage vs Original File Size
        ax2.scatter(original_sizes, overhead_percentages, alpha=0.7, s=60, color='blue', edgecolors='black')
        ax2.set_xlabel('Original File Size (MB)', fontsize=12)
        ax2.set_ylabel('Storage Overhead (%)', fontsize=12)
        ax2.set_title('Storage Overhead Percentage vs Original File Size', fontsize=14, fontweight='bold')
        ax2.grid(True, alpha=0.3)
        
        # Add trend line
        if len(original_sizes) > 1:
            z = np.polyfit(original_sizes, overhead_percentages, 1)
            p = np.poly1d(z)
            ax2.plot(original_sizes, p(original_sizes), "b--", alpha=0.8, linewidth=2,
                    label=f'Trend: y = {z[0]:.2f}x + {z[1]:.2f}%')
            ax2.legend()
        
        # Plot 3: Total Storage vs Original File Size
        ax3.scatter(original_sizes, total_storage_sizes, alpha=0.7, s=60, color='green', edgecolors='black')
        ax3.plot([0, max(original_sizes)], [0, max(original_sizes)], 'k--', alpha=0.5, label='No Overhead Line')
        ax3.set_xlabel('Original File Size (MB)', fontsize=12)
        ax3.set_ylabel('Total Storage Used (MB)', fontsize=12)
        ax3.set_title('Total Storage vs Original File Size', fontsize=14, fontweight='bold')
        ax3.grid(True, alpha=0.3)
        ax3.legend()
        
        # Plot 4: Overhead Distribution (Histogram)
        ax4.hist(overhead_percentages, bins=min(10, len(overhead_percentages)), alpha=0.7, color='orange', edgecolor='black')
        ax4.set_xlabel('Storage Overhead (%)', fontsize=12)
        ax4.set_ylabel('Number of Files', fontsize=12)
        ax4.set_title('Distribution of Storage Overhead Percentages', fontsize=14, fontweight='bold')
        ax4.grid(True, alpha=0.3)
        
        # Add statistics
        mean_overhead = np.mean(overhead_percentages)
        median_overhead = np.median(overhead_percentages)
        ax4.axvline(mean_overhead, color='red', linestyle='--', alpha=0.8, label=f'Mean: {mean_overhead:.1f}%')
        ax4.axvline(median_overhead, color='blue', linestyle='--', alpha=0.8, label=f'Median: {median_overhead:.1f}%')
        ax4.legend()
        
        # Adjust layout and save
        plt.tight_layout()
        
        # Save the combined graph
        graph_path = self.graphs_dir / "storage_overhead_analysis.png"
        plt.savefig(graph_path, dpi=300, bbox_inches='tight')
        print(f"Storage overhead analysis graphs saved to: {graph_path}")
        
        # Create detailed individual graphs
        self.create_detailed_storage_graphs(analysis_results)
        
        plt.close()
    
    def create_detailed_storage_graphs(self, analysis_results: List[Dict]):
        """Create detailed individual graphs for storage analysis."""
        
        # Storage Efficiency Analysis
        plt.figure(figsize=(14, 8))
        
        # Calculate storage efficiency (original_size / total_storage)
        storage_efficiency = [r['original_size_mb'] / r['total_storage_mb'] * 100 for r in analysis_results]
        original_sizes = [r['original_size_mb'] for r in analysis_results]
        
        plt.scatter(original_sizes, storage_efficiency, alpha=0.8, s=80, color='purple', edgecolors='black')
        
        # Add trend line
        if len(original_sizes) > 1:
            z = np.polyfit(original_sizes, storage_efficiency, 1)
            p = np.poly1d(z)
            plt.plot(original_sizes, p(original_sizes), "purple", alpha=0.8, linewidth=2,
                    label=f'Trend: y = {z[0]:.2f}x + {z[1]:.2f}%')
        
        # Add horizontal line at 100% efficiency
        plt.axhline(y=100, color='red', linestyle='--', alpha=0.5, label='100% Efficiency (No Overhead)')
        
        plt.xlabel('Original File Size (MB)', fontsize=14)
        plt.ylabel('Storage Efficiency (%)', fontsize=14)
        plt.title('CriticalFUSE Storage Efficiency Analysis', fontsize=16, fontweight='bold')
        plt.grid(True, alpha=0.3)
        plt.legend()
        
        efficiency_graph_path = self.graphs_dir / "storage_efficiency.png"
        plt.savefig(efficiency_graph_path, dpi=300, bbox_inches='tight')
        print(f"Storage efficiency graph saved to: {efficiency_graph_path}")
        plt.close()
        
        # File Type Breakdown Analysis
        plt.figure(figsize=(14, 8))
        
        # Group by file extensions, merging JPEG variants
        extension_groups = {}
        for result in analysis_results:
            ext = Path(result['filename']).suffix.lower()
            
            # Merge JPEG variants (.jpg and .jpeg) into a single group
            if ext in ['.jpg', '.jpeg']:
                ext = '.jpg'  # Use .jpg as the canonical extension
            
            if ext not in extension_groups:
                extension_groups[ext] = []
            extension_groups[ext].append(result['overhead_percentage'])
        
        # Create box plot for each extension
        if extension_groups:
            ext_labels = list(extension_groups.keys())
            ext_data = list(extension_groups.values())
            
            plt.boxplot(ext_data, labels=ext_labels, patch_artist=True)
            plt.xlabel('File Extension', fontsize=14)
            plt.ylabel('Storage Overhead (%)', fontsize=14)
            plt.title('Storage Overhead by File Type', fontsize=16, fontweight='bold')
            plt.grid(True, alpha=0.3)
            
            # Add statistics
            for i, (ext, data) in enumerate(extension_groups.items()):
                mean_val = np.mean(data)
                plt.text(i+1, mean_val, f'{mean_val:.1f}%', ha='center', va='bottom', fontweight='bold')
            
            filetype_graph_path = self.graphs_dir / "overhead_by_filetype.png"
            plt.savefig(filetype_graph_path, dpi=300, bbox_inches='tight')
            print(f"Overhead by file type graph saved to: {filetype_graph_path}")
            plt.close()
        
        # Create separate graphs for each file type
        self.create_file_type_specific_graphs(analysis_results)
        
        # Create general actual saved storage graph for all files
        self.create_general_saved_storage_graph(analysis_results)
        
        # Create JPEG type comparison graphs
        self.create_jpeg_type_comparison_graphs(analysis_results)
        
        # Create storage percentage breakdown chart
        self.create_storage_percentage_chart(analysis_results)
        
        # Create stacked storage composition chart
        self.create_stacked_storage_composition_chart(analysis_results)
        
        # Summary Statistics Table
        self.create_summary_statistics(analysis_results)
    
    def create_file_type_specific_graphs(self, analysis_results: List[Dict]):
        """Create separate detailed graphs for each file type."""
        
        # Group results by file extension, merging JPEG variants
        extension_groups = {}
        for result in analysis_results:
            ext = Path(result['filename']).suffix.lower()
            
            # Merge JPEG variants (.jpg and .jpeg) into a single group
            if ext in ['.jpg', '.jpeg']:
                ext = '.jpg'  # Use .jpg as the canonical extension
            
            if ext not in extension_groups:
                extension_groups[ext] = []
            extension_groups[ext].append(result)
        
        # Create separate analysis for each file type
        for ext, file_results in extension_groups.items():
            if len(file_results) < 2:
                print(f"Skipping {ext} - only {len(file_results)} file(s) available")
                continue
            
            print(f"Creating detailed graphs for {ext} files ({len(file_results)} files)")
            
            # Extract data for this file type
            original_sizes = [r['original_size_mb'] for r in file_results]
            overhead_sizes = [r['overhead_mb'] for r in file_results]
            overhead_percentages = [r['overhead_percentage'] for r in file_results]
            total_storage_sizes = [r['total_storage_mb'] for r in file_results]
            filenames = [r['filename'] for r in file_results]
            
            # Create comprehensive figure for this file type
            fig, ((ax1, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(16, 12))
            fig.suptitle(f'CriticalFUSE Storage Overhead Analysis - {ext.upper()} Files', fontsize=16, fontweight='bold')
            
            # Plot 1: Storage Overhead vs Original File Size
            ax1.scatter(original_sizes, overhead_sizes, alpha=0.7, s=60, color='red', edgecolors='black')
            ax1.set_xlabel('Original File Size (MB)', fontsize=12)
            ax1.set_ylabel('Storage Overhead (MB)', fontsize=12)
            ax1.set_title(f'Storage Overhead vs Original File Size ({ext})', fontsize=14, fontweight='bold')
            ax1.grid(True, alpha=0.3)
            
            # Add trend line
            if len(original_sizes) > 1:
                z = np.polyfit(original_sizes, overhead_sizes, 1)
                p = np.poly1d(z)
                ax1.plot(original_sizes, p(original_sizes), "r--", alpha=0.8, linewidth=2,
                        label=f'Trend: y = {z[0]:.4f}x + {z[1]:.4f}')
                ax1.legend()
            
            # Plot 2: Overhead Percentage vs Original File Size
            ax2.scatter(original_sizes, overhead_percentages, alpha=0.7, s=60, color='blue', edgecolors='black')
            ax2.set_xlabel('Original File Size (MB)', fontsize=12)
            ax2.set_ylabel('Storage Overhead (%)', fontsize=12)
            ax2.set_title(f'Storage Overhead Percentage vs Original File Size ({ext})', fontsize=14, fontweight='bold')
            ax2.grid(True, alpha=0.3)
            
            # Add trend line
            if len(original_sizes) > 1:
                z = np.polyfit(original_sizes, overhead_percentages, 1)
                p = np.poly1d(z)
                ax2.plot(original_sizes, p(original_sizes), "b--", alpha=0.8, linewidth=2,
                        label=f'Trend: y = {z[0]:.2f}x + {z[1]:.2f}%')
                ax2.legend()
            
            # Plot 3: Total Storage vs Original File Size
            ax3.scatter(original_sizes, total_storage_sizes, alpha=0.7, s=60, color='green', edgecolors='black')
            ax3.plot([0, max(original_sizes)], [0, max(original_sizes)], 'k--', alpha=0.5, label='No Overhead Line')
            ax3.set_xlabel('Original File Size (MB)', fontsize=12)
            ax3.set_ylabel('Total Storage Used (MB)', fontsize=12)
            ax3.set_title(f'Total Storage vs Original File Size ({ext})', fontsize=14, fontweight='bold')
            ax3.grid(True, alpha=0.3)
            ax3.legend()
            
            # Plot 4: Overhead Distribution (Histogram)
            ax4.hist(overhead_percentages, bins=min(8, len(overhead_percentages)), alpha=0.7, color='orange', edgecolor='black')
            ax4.set_xlabel('Storage Overhead (%)', fontsize=12)
            ax4.set_ylabel('Number of Files', fontsize=12)
            ax4.set_title(f'Distribution of Storage Overhead Percentages ({ext})', fontsize=14, fontweight='bold')
            ax4.grid(True, alpha=0.3)
            
            # Add statistics
            mean_overhead = np.mean(overhead_percentages)
            median_overhead = np.median(overhead_percentages)
            ax4.axvline(mean_overhead, color='red', linestyle='--', alpha=0.8, label=f'Mean: {mean_overhead:.1f}%')
            ax4.axvline(median_overhead, color='blue', linestyle='--', alpha=0.8, label=f'Median: {median_overhead:.1f}%')
            ax4.legend()
            
            # Adjust layout and save
            plt.tight_layout()
            
            # Save the file type specific graph
            safe_ext = ext.replace('.', '_')
            graph_path = self.graphs_dir / f"storage_overhead_{safe_ext}_files.png"
            plt.savefig(graph_path, dpi=300, bbox_inches='tight')
            print(f"Storage overhead analysis for {ext} files saved to: {graph_path}")
            plt.close()
            
            # Create additional detailed analysis for this file type
            self.create_file_type_detailed_analysis(ext, file_results)
    
    def create_file_type_detailed_analysis(self, file_ext: str, file_results: List[Dict]):
        """Create additional detailed analysis for a specific file type."""
        
        # Extract data
        original_sizes = [r['original_size_mb'] for r in file_results]
        overhead_percentages = [r['overhead_percentage'] for r in file_results]
        storage_efficiency = [r['original_size_mb'] / r['total_storage_mb'] * 100 for r in file_results]
        filenames = [r['filename'] for r in file_results]
        
        # Create detailed efficiency analysis
        plt.figure(figsize=(14, 8))
        
        plt.scatter(original_sizes, storage_efficiency, alpha=0.8, s=80, color='purple', edgecolors='black')
        
        # Add trend line
        if len(original_sizes) > 1:
            z = np.polyfit(original_sizes, storage_efficiency, 1)
            p = np.poly1d(z)
            plt.plot(original_sizes, p(original_sizes), "purple", alpha=0.8, linewidth=2,
                    label=f'Trend: y = {z[0]:.2f}x + {z[1]:.2f}%')
        
        # Add horizontal line at 100% efficiency
        plt.axhline(y=100, color='red', linestyle='--', alpha=0.5, label='100% Efficiency (No Overhead)')
        
        # Add file labels for some points
        for i, filename in enumerate(filenames):
            if i % max(1, len(filenames) // 4) == 0:  # Show ~4 labels
                plt.annotate(filename, (original_sizes[i], storage_efficiency[i]), 
                           xytext=(5, 5), textcoords='offset points', fontsize=8, alpha=0.7)
        
        plt.xlabel('Original File Size (MB)', fontsize=14)
        plt.ylabel('Storage Efficiency (%)', fontsize=14)
        plt.title(f'CriticalFUSE Storage Efficiency Analysis - {file_ext.upper()} Files', fontsize=16, fontweight='bold')
        plt.grid(True, alpha=0.3)
        plt.legend()
        
        safe_ext = file_ext.replace('.', '_')
        efficiency_graph_path = self.graphs_dir / f"storage_efficiency_{safe_ext}_files.png"
        plt.savefig(efficiency_graph_path, dpi=300, bbox_inches='tight')
        print(f"Storage efficiency analysis for {file_ext} files saved to: {efficiency_graph_path}")
        plt.close()
        
        # Create file-by-file breakdown (sorted by original file size)
        plt.figure(figsize=(14, 8))
        
        # Sort files by original size for better visualization
        sorted_indices = sorted(range(len(original_sizes)), key=lambda i: original_sizes[i])
        sorted_filenames = [filenames[i] for i in sorted_indices]
        sorted_overhead_percentages = [overhead_percentages[i] for i in sorted_indices]
        sorted_original_sizes = [original_sizes[i] for i in sorted_indices]
        
        # Create bar chart of overhead percentages (sorted by file size)
        x_pos = range(len(sorted_filenames))
        plt.bar(x_pos, sorted_overhead_percentages, alpha=0.7, color='orange', edgecolor='black')
        
        # Add value labels on bars
        for i, (x, y) in enumerate(zip(x_pos, sorted_overhead_percentages)):
            plt.text(x, y + 0.5, f'{y:.1f}%', ha='center', va='bottom', fontweight='bold', fontsize=8)
        
        plt.xlabel('Files (sorted by size)', fontsize=14)
        plt.ylabel('Storage Overhead (%)', fontsize=14)
        plt.title(f'Storage Overhead by File - {file_ext.upper()} Files (Sorted by Size)', fontsize=16, fontweight='bold')
        plt.xticks(x_pos, [f.split('.')[0] for f in sorted_filenames], rotation=45, ha='right')
        plt.grid(True, alpha=0.3, axis='y')
        
        # Add statistics line
        mean_overhead = np.mean(sorted_overhead_percentages)
        plt.axhline(y=mean_overhead, color='red', linestyle='--', alpha=0.8, 
                   label=f'Mean: {mean_overhead:.1f}%')
        plt.legend()
        
        plt.tight_layout()
        
        breakdown_graph_path = self.graphs_dir / f"file_breakdown_{safe_ext}_files.png"
        plt.savefig(breakdown_graph_path, dpi=300, bbox_inches='tight')
        print(f"File breakdown analysis for {file_ext} files saved to: {breakdown_graph_path}")
        plt.close()
        
        # Create actual saved storage graph
        plt.figure(figsize=(14, 8))
        
        # Calculate actual saved storage (total storage - original size)
        actual_saved_storage = [r['total_storage_mb'] - r['original_size_mb'] for r in file_results]
        
        # Sort by original file size
        sorted_actual_saved = [actual_saved_storage[i] for i in sorted_indices]
        
        # Create bar chart of actual saved storage
        x_pos = range(len(sorted_filenames))
        plt.bar(x_pos, sorted_actual_saved, alpha=0.7, color='green', edgecolor='black')
        
        # Add value labels on bars
        for i, (x, y) in enumerate(zip(x_pos, sorted_actual_saved)):
            plt.text(x, y + 0.01, f'{y:.2f}MB', ha='center', va='bottom', fontweight='bold', fontsize=8)
        
        plt.xlabel('Files (sorted by size)', fontsize=14)
        plt.ylabel('Actual Saved Storage (MB)', fontsize=14)
        plt.title(f'Actual Saved Storage by File - {file_ext.upper()} Files (Sorted by Size)', fontsize=16, fontweight='bold')
        plt.xticks(x_pos, [f.split('.')[0] for f in sorted_filenames], rotation=45, ha='right')
        plt.grid(True, alpha=0.3, axis='y')
        
        # Add statistics line
        mean_saved = np.mean(sorted_actual_saved)
        plt.axhline(y=mean_saved, color='red', linestyle='--', alpha=0.8, 
                   label=f'Mean: {mean_saved:.2f}MB')
        plt.legend()
        
        plt.tight_layout()
        
        saved_storage_graph_path = self.graphs_dir / f"actual_saved_storage_{safe_ext}_files.png"
        plt.savefig(saved_storage_graph_path, dpi=300, bbox_inches='tight')
        print(f"Actual saved storage analysis for {file_ext} files saved to: {saved_storage_graph_path}")
        plt.close()
        
        # Create summary statistics for this file type
        self.create_file_type_statistics(file_ext, file_results)
    
    def create_general_saved_storage_graph(self, analysis_results: List[Dict]):
        """Create a general graph showing actual saved storage for all files."""
        
        # Extract data
        original_sizes = [r['original_size_mb'] for r in analysis_results]
        actual_saved_storage = [r['total_storage_mb'] - r['original_size_mb'] for r in analysis_results]
        filenames = [r['filename'] for r in analysis_results]
        
        # Sort files by original size for better visualization
        sorted_indices = sorted(range(len(original_sizes)), key=lambda i: original_sizes[i])
        sorted_filenames = [filenames[i] for i in sorted_indices]
        sorted_actual_saved = [actual_saved_storage[i] for i in sorted_indices]
        sorted_original_sizes = [original_sizes[i] for i in sorted_indices]
        
        # Create the graph
        plt.figure(figsize=(16, 10))
        
        # Create bar chart of actual saved storage
        x_pos = range(len(sorted_filenames))
        bars = plt.bar(x_pos, sorted_actual_saved, alpha=0.7, color='green', edgecolor='black')
        
        # Add value labels on bars
        for i, (x, y) in enumerate(zip(x_pos, sorted_actual_saved)):
            plt.text(x, y + 0.01, f'{y:.2f}MB', ha='center', va='bottom', fontweight='bold', fontsize=8)
        
        # Color bars by file type
        for i, filename in enumerate(sorted_filenames):
            ext = Path(filename).suffix.lower()
            if ext in ['.jpg', '.jpeg']:
                bars[i].set_color('blue')
            elif ext == '.png':
                bars[i].set_color('red')
            elif ext == '.bmp':
                bars[i].set_color('orange')
            else:
                bars[i].set_color('green')
        
        plt.xlabel('Files (sorted by size)', fontsize=14)
        plt.ylabel('Actual Saved Storage (MB)', fontsize=14)
        plt.title('Actual Saved Storage by File - All File Types (Sorted by Size)', fontsize=16, fontweight='bold')
        plt.xticks(x_pos, [f.split('.')[0] for f in sorted_filenames], rotation=45, ha='right')
        plt.grid(True, alpha=0.3, axis='y')
        
        # Add statistics line
        mean_saved = np.mean(sorted_actual_saved)
        plt.axhline(y=mean_saved, color='red', linestyle='--', alpha=0.8, 
                   label=f'Mean: {mean_saved:.2f}MB')
        
        # Add legend for file types
        from matplotlib.patches import Patch
        legend_elements = [
            Patch(facecolor='blue', label='JPEG'),
            Patch(facecolor='red', label='PNG'),
            Patch(facecolor='orange', label='BMP'),
            Patch(facecolor='green', label='Other')
        ]
        plt.legend(handles=legend_elements, loc='upper right')
        
        plt.tight_layout()
        
        saved_storage_graph_path = self.graphs_dir / "actual_saved_storage_all_files.png"
        plt.savefig(saved_storage_graph_path, dpi=300, bbox_inches='tight')
        print(f"General actual saved storage analysis saved to: {saved_storage_graph_path}")
        plt.close()
        
        # Create scatter plot: Original Size vs Actual Saved Storage
        plt.figure(figsize=(14, 8))
        
        # Color points by file type
        colors = []
        for filename in filenames:
            ext = Path(filename).suffix.lower()
            if ext in ['.jpg', '.jpeg']:
                colors.append('blue')
            elif ext == '.png':
                colors.append('red')
            elif ext == '.bmp':
                colors.append('orange')
            else:
                colors.append('green')
        
        plt.scatter(original_sizes, actual_saved_storage, alpha=0.7, s=80, c=colors, edgecolors='black')
        
        # Add trend line
        if len(original_sizes) > 1:
            z = np.polyfit(original_sizes, actual_saved_storage, 1)
            p = np.poly1d(z)
            plt.plot(original_sizes, p(original_sizes), "purple", alpha=0.8, linewidth=2,
                    label=f'Trend: y = {z[0]:.4f}x + {z[1]:.4f}')
        
        plt.xlabel('Original File Size (MB)', fontsize=14)
        plt.ylabel('Actual Saved Storage (MB)', fontsize=14)
        plt.title('Actual Saved Storage vs Original File Size - All File Types', fontsize=16, fontweight='bold')
        plt.grid(True, alpha=0.3)
        
        # Add legend
        legend_elements = [
            Patch(facecolor='blue', label='JPEG'),
            Patch(facecolor='red', label='PNG'),
            Patch(facecolor='orange', label='BMP'),
            Patch(facecolor='green', label='Other')
        ]
        plt.legend(handles=legend_elements)
        
        plt.tight_layout()
        
        scatter_graph_path = self.graphs_dir / "saved_storage_vs_size_scatter.png"
        plt.savefig(scatter_graph_path, dpi=300, bbox_inches='tight')
        print(f"Saved storage vs size scatter plot saved to: {scatter_graph_path}")
        plt.close()
    
    def create_jpeg_type_comparison_graphs(self, analysis_results: List[Dict]):
        """Create comparison graphs between baseline and progressive JPEGs."""
        
        # Filter only JPEG files
        jpeg_results = [r for r in analysis_results if r['jpeg_type'] in ['baseline', 'progressive']]
        
        if len(jpeg_results) < 2:
            print("Not enough JPEG files with detected types for comparison")
            return
        
        # Separate baseline and progressive JPEGs
        baseline_results = [r for r in jpeg_results if r['jpeg_type'] == 'baseline']
        progressive_results = [r for r in jpeg_results if r['jpeg_type'] == 'progressive']
        
        print(f"Creating JPEG type comparison: {len(baseline_results)} baseline, {len(progressive_results)} progressive")
        
        # Create comprehensive comparison figure
        fig, ((ax1, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(16, 12))
        fig.suptitle('JPEG Type Comparison: Baseline vs Progressive', fontsize=16, fontweight='bold')
        
        # Extract data for both types
        baseline_sizes = [r['original_size_mb'] for r in baseline_results]
        baseline_overhead = [r['overhead_percentage'] for r in baseline_results]
        baseline_efficiency = [r['original_size_mb'] / r['total_storage_mb'] * 100 for r in baseline_results]
        
        progressive_sizes = [r['original_size_mb'] for r in progressive_results]
        progressive_overhead = [r['overhead_percentage'] for r in progressive_results]
        progressive_efficiency = [r['original_size_mb'] / r['total_storage_mb'] * 100 for r in progressive_results]
        
        # Plot 1: Overhead Percentage Comparison
        ax1.scatter(baseline_sizes, baseline_overhead, alpha=0.7, s=80, color='blue', 
                   label=f'Baseline JPEG (n={len(baseline_results)})', edgecolors='black')
        ax1.scatter(progressive_sizes, progressive_overhead, alpha=0.7, s=80, color='red', 
                   label=f'Progressive JPEG (n={len(progressive_results)})', edgecolors='black')
        
        # Add trend lines
        if len(baseline_sizes) > 1:
            z_baseline = np.polyfit(baseline_sizes, baseline_overhead, 1)
            p_baseline = np.poly1d(z_baseline)
            ax1.plot(baseline_sizes, p_baseline(baseline_sizes), "b--", alpha=0.8, linewidth=2)
        
        if len(progressive_sizes) > 1:
            z_progressive = np.polyfit(progressive_sizes, progressive_overhead, 1)
            p_progressive = np.poly1d(z_progressive)
            ax1.plot(progressive_sizes, p_progressive(progressive_sizes), "r--", alpha=0.8, linewidth=2)
        
        ax1.set_xlabel('Original File Size (MB)', fontsize=12)
        ax1.set_ylabel('Storage Overhead (%)', fontsize=12)
        ax1.set_title('Storage Overhead: Baseline vs Progressive JPEGs', fontsize=14, fontweight='bold')
        ax1.grid(True, alpha=0.3)
        ax1.legend()
        
        # Plot 2: Storage Efficiency Comparison
        ax2.scatter(baseline_sizes, baseline_efficiency, alpha=0.7, s=80, color='blue', 
                   label=f'Baseline JPEG (n={len(baseline_results)})', edgecolors='black')
        ax2.scatter(progressive_sizes, progressive_efficiency, alpha=0.7, s=80, color='red', 
                   label=f'Progressive JPEG (n={len(progressive_results)})', edgecolors='black')
        
        # Add trend lines
        if len(baseline_sizes) > 1:
            z_baseline = np.polyfit(baseline_sizes, baseline_efficiency, 1)
            p_baseline = np.poly1d(z_baseline)
            ax2.plot(baseline_sizes, p_baseline(baseline_sizes), "b--", alpha=0.8, linewidth=2)
        
        if len(progressive_sizes) > 1:
            z_progressive = np.polyfit(progressive_sizes, progressive_efficiency, 1)
            p_progressive = np.poly1d(z_progressive)
            ax2.plot(progressive_sizes, p_progressive(progressive_sizes), "r--", alpha=0.8, linewidth=2)
        
        # Add 100% efficiency line
        ax2.axhline(y=100, color='green', linestyle='--', alpha=0.5, label='100% Efficiency')
        
        ax2.set_xlabel('Original File Size (MB)', fontsize=12)
        ax2.set_ylabel('Storage Efficiency (%)', fontsize=12)
        ax2.set_title('Storage Efficiency: Baseline vs Progressive JPEGs', fontsize=14, fontweight='bold')
        ax2.grid(True, alpha=0.3)
        ax2.legend()
        
        # Plot 3: Box plot comparison of overhead percentages
        overhead_data = [baseline_overhead, progressive_overhead]
        box_plot = ax3.boxplot(overhead_data, labels=['Baseline JPEG', 'Progressive JPEG'], 
                              patch_artist=True, showmeans=True)
        
        # Color the boxes
        box_plot['boxes'][0].set_facecolor('lightblue')
        box_plot['boxes'][1].set_facecolor('lightcoral')
        
        ax3.set_ylabel('Storage Overhead (%)', fontsize=12)
        ax3.set_title('Overhead Distribution: Baseline vs Progressive', fontsize=14, fontweight='bold')
        ax3.grid(True, alpha=0.3)
        
        # Add statistics
        baseline_mean = np.mean(baseline_overhead) if baseline_overhead else 0
        progressive_mean = np.mean(progressive_overhead) if progressive_overhead else 0
        ax3.text(1, baseline_mean, f'Mean: {baseline_mean:.1f}%', ha='center', va='bottom', fontweight='bold')
        ax3.text(2, progressive_mean, f'Mean: {progressive_mean:.1f}%', ha='center', va='bottom', fontweight='bold')
        
        # Plot 4: Box plot comparison of storage efficiency
        efficiency_data = [baseline_efficiency, progressive_efficiency]
        box_plot = ax4.boxplot(efficiency_data, labels=['Baseline JPEG', 'Progressive JPEG'], 
                              patch_artist=True, showmeans=True)
        
        # Color the boxes
        box_plot['boxes'][0].set_facecolor('lightblue')
        box_plot['boxes'][1].set_facecolor('lightcoral')
        
        ax4.set_ylabel('Storage Efficiency (%)', fontsize=12)
        ax4.set_title('Efficiency Distribution: Baseline vs Progressive', fontsize=14, fontweight='bold')
        ax4.grid(True, alpha=0.3)
        
        # Add statistics
        baseline_eff_mean = np.mean(baseline_efficiency) if baseline_efficiency else 0
        progressive_eff_mean = np.mean(progressive_efficiency) if progressive_efficiency else 0
        ax4.text(1, baseline_eff_mean, f'Mean: {baseline_eff_mean:.1f}%', ha='center', va='bottom', fontweight='bold')
        ax4.text(2, progressive_eff_mean, f'Mean: {progressive_eff_mean:.1f}%', ha='center', va='bottom', fontweight='bold')
        
        # Add 100% efficiency line
        ax4.axhline(y=100, color='green', linestyle='--', alpha=0.5)
        
        plt.tight_layout()
        
        # Save the comparison graph
        comparison_graph_path = self.graphs_dir / "jpeg_type_comparison.png"
        plt.savefig(comparison_graph_path, dpi=300, bbox_inches='tight')
        print(f"JPEG type comparison graph saved to: {comparison_graph_path}")
        plt.close()
        
        # Create detailed statistics comparison
        self.create_jpeg_type_statistics(baseline_results, progressive_results)
    
    def create_jpeg_type_statistics(self, baseline_results: List[Dict], progressive_results: List[Dict]):
        """Create detailed statistics comparison between JPEG types."""
        
        # Calculate statistics for baseline JPEGs
        baseline_overhead = [r['overhead_percentage'] for r in baseline_results]
        baseline_efficiency = [r['original_size_mb'] / r['total_storage_mb'] * 100 for r in baseline_results]
        baseline_sizes = [r['original_size_mb'] for r in baseline_results]
        
        # Calculate statistics for progressive JPEGs
        progressive_overhead = [r['overhead_percentage'] for r in progressive_results]
        progressive_efficiency = [r['original_size_mb'] / r['total_storage_mb'] * 100 for r in progressive_results]
        progressive_sizes = [r['original_size_mb'] for r in progressive_results]
        
        # Create comparison statistics
        stats = {
            'Baseline JPEG Statistics': {
                'Count': len(baseline_results),
                'Average File Size (MB)': np.mean(baseline_sizes) if baseline_sizes else 0,
                'Average Overhead (%)': np.mean(baseline_overhead) if baseline_overhead else 0,
                'Median Overhead (%)': np.median(baseline_overhead) if baseline_overhead else 0,
                'Average Efficiency (%)': np.mean(baseline_efficiency) if baseline_efficiency else 0,
                'Median Efficiency (%)': np.median(baseline_efficiency) if baseline_efficiency else 0,
                'Min Overhead (%)': np.min(baseline_overhead) if baseline_overhead else 0,
                'Max Overhead (%)': np.max(baseline_overhead) if baseline_overhead else 0,
                'Std Dev Overhead (%)': np.std(baseline_overhead) if baseline_overhead else 0
            },
            'Progressive JPEG Statistics': {
                'Count': len(progressive_results),
                'Average File Size (MB)': np.mean(progressive_sizes) if progressive_sizes else 0,
                'Average Overhead (%)': np.mean(progressive_overhead) if progressive_overhead else 0,
                'Median Overhead (%)': np.median(progressive_overhead) if progressive_overhead else 0,
                'Average Efficiency (%)': np.mean(progressive_efficiency) if progressive_efficiency else 0,
                'Median Efficiency (%)': np.median(progressive_efficiency) if progressive_efficiency else 0,
                'Min Overhead (%)': np.min(progressive_overhead) if progressive_overhead else 0,
                'Max Overhead (%)': np.max(progressive_overhead) if progressive_overhead else 0,
                'Std Dev Overhead (%)': np.std(progressive_overhead) if progressive_overhead else 0
            }
        }
        
        # Save statistics to file
        stats_file = self.graphs_dir / "jpeg_type_comparison_statistics.txt"
        with open(stats_file, 'w') as f:
            f.write("JPEG Type Comparison Statistics\n")
            f.write("=" * 50 + "\n\n")
            
            for category, data in stats.items():
                f.write(f"{category}\n")
                f.write("-" * len(category) + "\n")
                for key, value in data.items():
                    if isinstance(value, float):
                        f.write(f"{key}: {value:.2f}\n")
                    else:
                        f.write(f"{key}: {value}\n")
                f.write("\n")
            
            # Add comparison summary
            if baseline_overhead and progressive_overhead:
                f.write("Comparison Summary\n")
                f.write("-" * 18 + "\n")
                f.write(f"Overhead Difference: {np.mean(progressive_overhead) - np.mean(baseline_overhead):.2f}%\n")
                f.write(f"Efficiency Difference: {np.mean(progressive_efficiency) - np.mean(baseline_efficiency):.2f}%\n")
                if np.mean(progressive_overhead) < np.mean(baseline_overhead):
                    f.write("Progressive JPEGs have LOWER overhead on average\n")
                else:
                    f.write("Baseline JPEGs have LOWER overhead on average\n")
        
        print(f"JPEG type comparison statistics saved to: {stats_file}")
        
        # Also save as JSON
        stats_json = self.graphs_dir / "jpeg_type_comparison_statistics.json"
        with open(stats_json, 'w') as f:
            json.dump(stats, f, indent=2)
        
        print(f"JPEG type comparison statistics (JSON) saved to: {stats_json}")
    
    def create_storage_percentage_chart(self, analysis_results: List[Dict]):
        """Create a bar chart showing the percentage of each file's storage out of total storage."""
        
        if not analysis_results:
            print("No data available for storage percentage chart")
            return
        
        # Calculate total storage used
        total_storage_mb = sum(r['total_storage_mb'] for r in analysis_results)
        
        if total_storage_mb == 0:
            print("Total storage is zero, cannot create percentage chart")
            return
        
        # Sort files by total storage size (largest first)
        sorted_results = sorted(analysis_results, key=lambda x: x['total_storage_mb'], reverse=True)
        
        # Extract data for plotting
        filenames = [r['filename'] for r in sorted_results]
        storage_percentages = [(r['total_storage_mb'] / total_storage_mb) * 100 for r in sorted_results]
        storage_mb = [r['total_storage_mb'] for r in sorted_results]
        
        # Create the chart
        plt.figure(figsize=(16, 10))
        
        # Create bar chart
        bars = plt.bar(range(len(filenames)), storage_percentages, alpha=0.7, edgecolor='black')
        
        # Color bars by file type
        for i, filename in enumerate(filenames):
            ext = Path(filename).suffix.lower()
            if ext in ['.jpg', '.jpeg']:
                bars[i].set_color('blue')
            elif ext == '.png':
                bars[i].set_color('red')
            elif ext == '.bmp':
                bars[i].set_color('orange')
            else:
                bars[i].set_color('green')
        
        # Add value labels on bars
        for i, (x, y) in enumerate(zip(range(len(filenames)), storage_percentages)):
            plt.text(x, y + 0.1, f'{y:.1f}%', ha='center', va='bottom', fontweight='bold', fontsize=8)
            # Also show absolute size
            plt.text(x, y/2, f'{storage_mb[i]:.2f}MB', ha='center', va='center', 
                    fontweight='bold', fontsize=7, color='white')
        
        plt.xlabel('Files (sorted by storage size)', fontsize=14)
        plt.ylabel('Storage Percentage (%)', fontsize=14)
        plt.title(f'Storage Usage Breakdown - Each File as % of Total Storage ({total_storage_mb:.2f} MB)', 
                 fontsize=16, fontweight='bold')
        plt.xticks(range(len(filenames)), [f.split('.')[0] for f in filenames], rotation=45, ha='right')
        plt.grid(True, alpha=0.3, axis='y')
        
        # Add legend for file types
        from matplotlib.patches import Patch
        legend_elements = [
            Patch(facecolor='blue', label='JPEG'),
            Patch(facecolor='red', label='PNG'),
            Patch(facecolor='orange', label='BMP'),
            Patch(facecolor='green', label='Other')
        ]
        plt.legend(handles=legend_elements, loc='upper right')
        
        # Add cumulative percentage line
        cumulative_percentages = []
        cumulative = 0
        for pct in storage_percentages:
            cumulative += pct
            cumulative_percentages.append(cumulative)
        
        # Create secondary y-axis for cumulative percentage
        ax2 = plt.gca().twinx()
        ax2.plot(range(len(filenames)), cumulative_percentages, 'r-', linewidth=2, marker='o', markersize=4)
        ax2.set_ylabel('Cumulative Storage Percentage (%)', fontsize=14, color='red')
        ax2.tick_params(axis='y', labelcolor='red')
        ax2.grid(True, alpha=0.3)
        
        # Add horizontal line at 80% and 90% cumulative
        ax2.axhline(y=80, color='orange', linestyle='--', alpha=0.7, label='80% Cumulative')
        ax2.axhline(y=90, color='red', linestyle='--', alpha=0.7, label='90% Cumulative')
        
        plt.tight_layout()
        
        # Save the chart
        percentage_chart_path = self.graphs_dir / "storage_percentage_breakdown.png"
        plt.savefig(percentage_chart_path, dpi=300, bbox_inches='tight')
        print(f"Storage percentage breakdown chart saved to: {percentage_chart_path}")
        plt.close()
        
        # Create a summary table of top storage consumers
        self.create_storage_summary_table(sorted_results, total_storage_mb)
    
    def create_storage_summary_table(self, sorted_results: List[Dict], total_storage_mb: float):
        """Create a summary table of storage usage."""
        
        # Calculate statistics
        total_files = len(sorted_results)
        top_10_percent = max(1, total_files // 10)  # Top 10% of files
        top_25_percent = max(1, total_files // 4)   # Top 25% of files
        top_50_percent = max(1, total_files // 2)   # Top 50% of files
        
        # Calculate cumulative storage for different percentiles
        top_10_storage = sum(r['total_storage_mb'] for r in sorted_results[:top_10_percent])
        top_25_storage = sum(r['total_storage_mb'] for r in sorted_results[:top_25_percent])
        top_50_storage = sum(r['total_storage_mb'] for r in sorted_results[:top_50_percent])
        
        # Create summary statistics
        summary = {
            'Total Files': total_files,
            'Total Storage (MB)': total_storage_mb,
            'Average File Size (MB)': total_storage_mb / total_files,
            'Largest File (MB)': sorted_results[0]['total_storage_mb'] if sorted_results else 0,
            'Smallest File (MB)': sorted_results[-1]['total_storage_mb'] if sorted_results else 0,
            'Top 10% Files': {
                'Count': top_10_percent,
                'Storage (MB)': top_10_storage,
                'Percentage of Total': (top_10_storage / total_storage_mb) * 100
            },
            'Top 25% Files': {
                'Count': top_25_percent,
                'Storage (MB)': top_25_storage,
                'Percentage of Total': (top_25_storage / total_storage_mb) * 100
            },
            'Top 50% Files': {
                'Count': top_50_percent,
                'Storage (MB)': top_50_storage,
                'Percentage of Total': (top_50_storage / total_storage_mb) * 100
            }
        }
        
        # Save summary to file
        summary_file = self.graphs_dir / "storage_usage_summary.txt"
        with open(summary_file, 'w') as f:
            f.write("Storage Usage Summary\n")
            f.write("=" * 30 + "\n\n")
            
            f.write("Overall Statistics:\n")
            f.write(f"Total Files: {summary['Total Files']}\n")
            f.write(f"Total Storage: {summary['Total Storage (MB)']:.2f} MB\n")
            f.write(f"Average File Size: {summary['Average File Size (MB)']:.2f} MB\n")
            f.write(f"Largest File: {summary['Largest File (MB)']:.2f} MB\n")
            f.write(f"Smallest File: {summary['Smallest File (MB)']:.2f} MB\n\n")
            
            f.write("Storage Distribution:\n")
            for percentile in ['Top 10% Files', 'Top 25% Files', 'Top 50% Files']:
                data = summary[percentile]
                f.write(f"{percentile}:\n")
                f.write(f"  Count: {data['Count']} files\n")
                f.write(f"  Storage: {data['Storage (MB)']:.2f} MB\n")
                f.write(f"  Percentage: {data['Percentage of Total']:.1f}%\n\n")
            
            f.write("Top 10 Largest Files:\n")
            for i, result in enumerate(sorted_results[:10], 1):
                percentage = (result['total_storage_mb'] / total_storage_mb) * 100
                f.write(f"{i:2d}. {result['filename']:<30} {result['total_storage_mb']:8.2f} MB ({percentage:5.1f}%)\n")
        
        print(f"Storage usage summary saved to: {summary_file}")
        
        # Also save as JSON
        summary_json = self.graphs_dir / "storage_usage_summary.json"
        with open(summary_json, 'w') as f:
            json.dump(summary, f, indent=2)
        
        print(f"Storage usage summary (JSON) saved to: {summary_json}")
    
    def create_stacked_storage_composition_chart(self, analysis_results: List[Dict]):
        """Create a stacked bar chart showing the composition of each file's storage."""
        
        if not analysis_results:
            print("No data available for stacked storage composition chart")
            return
        
        # Calculate total storage used
        total_storage_mb = sum(r['total_storage_mb'] for r in analysis_results)
        
        if total_storage_mb == 0:
            print("Total storage is zero, cannot create composition chart")
            return
        
        # Sort files by total storage size (largest first)
        sorted_results = sorted(analysis_results, key=lambda x: x['total_storage_mb'], reverse=True)
        
        # Extract data for plotting
        filenames = [r['filename'] for r in sorted_results]
        
        # Calculate storage composition for each file
        crit_sizes = []
        noncrit_sizes = []
        mapping_sizes = []
        other_sizes = []
        
        for result in sorted_results:
            crit_size = 0
            noncrit_size = 0
            mapping_size = 0
            other_size = 0
            
            # Analyze each related file to determine its type and size
            for file_info in result['related_files']:
                filename = file_info['filename']
                size_mb = file_info['size_mb']
                
                if '.crit' in filename:
                    crit_size += size_mb
                elif '.noncrit' in filename or '.ac.noncrit' in filename:
                    noncrit_size += size_mb
                elif '.mapping' in filename:
                    mapping_size += size_mb
                else:
                    other_size += size_mb
            
            crit_sizes.append(crit_size)
            noncrit_sizes.append(noncrit_size)
            mapping_sizes.append(mapping_size)
            other_sizes.append(other_size)
        
        # Convert to percentages of total storage
        crit_percentages = [(size / total_storage_mb) * 100 for size in crit_sizes]
        noncrit_percentages = [(size / total_storage_mb) * 100 for size in noncrit_sizes]
        mapping_percentages = [(size / total_storage_mb) * 100 for size in mapping_sizes]
        other_percentages = [(size / total_storage_mb) * 100 for size in other_sizes]
        
        # Create the stacked bar chart
        fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(16, 12))
        
        # Plot 1: Stacked bar chart showing composition
        x_pos = range(len(filenames))
        
        # Create stacked bars
        p1 = ax1.bar(x_pos, crit_percentages, alpha=0.8, color='blue', label='Critical (.crit)')
        p2 = ax1.bar(x_pos, noncrit_percentages, bottom=crit_percentages, alpha=0.8, color='red', label='Non-Critical (.noncrit)')
        
        # Calculate bottom positions for mapping and other
        bottom_crit_noncrit = [crit_percentages[i] + noncrit_percentages[i] for i in range(len(filenames))]
        p3 = ax1.bar(x_pos, mapping_percentages, bottom=bottom_crit_noncrit, alpha=0.8, color='green', label='Mapping (.mapping)')
        
        bottom_crit_noncrit_mapping = [bottom_crit_noncrit[i] + mapping_percentages[i] for i in range(len(filenames))]
        p4 = ax1.bar(x_pos, other_percentages, bottom=bottom_crit_noncrit_mapping, alpha=0.8, color='orange', label='Other')
        
        # Add value labels on bars (show total percentage for each file)
        total_percentages = [crit_percentages[i] + noncrit_percentages[i] + mapping_percentages[i] + other_percentages[i] for i in range(len(filenames))]
        for i, (x, y) in enumerate(zip(x_pos, total_percentages)):
            if y > 1:  # Only show label if bar is large enough
                ax1.text(x, y + 0.1, f'{y:.1f}%', ha='center', va='bottom', fontweight='bold', fontsize=8)
        
        ax1.set_xlabel('Files (sorted by storage size)', fontsize=12)
        ax1.set_ylabel('Storage Percentage of Total (%)', fontsize=12)
        ax1.set_title(f'Storage Composition Breakdown - Each File as % of Total Storage ({total_storage_mb:.2f} MB)', 
                     fontsize=14, fontweight='bold')
        ax1.set_xticks(x_pos)
        ax1.set_xticklabels([f.split('.')[0] for f in filenames], rotation=45, ha='right')
        ax1.legend(loc='upper right')
        ax1.grid(True, alpha=0.3, axis='y')
        
        # Plot 2: Horizontal stacked bar chart for better readability
        y_pos = range(len(filenames))
        
        # Create horizontal stacked bars
        p1_h = ax2.barh(y_pos, crit_percentages, alpha=0.8, color='blue', label='Critical (.crit)')
        p2_h = ax2.barh(y_pos, noncrit_percentages, left=crit_percentages, alpha=0.8, color='red', label='Non-Critical (.noncrit)')
        
        # Calculate left positions for mapping and other
        left_crit_noncrit = [crit_percentages[i] + noncrit_percentages[i] for i in range(len(filenames))]
        p3_h = ax2.barh(y_pos, mapping_percentages, left=left_crit_noncrit, alpha=0.8, color='green', label='Mapping (.mapping)')
        
        left_crit_noncrit_mapping = [left_crit_noncrit[i] + mapping_percentages[i] for i in range(len(filenames))]
        p4_h = ax2.barh(y_pos, other_percentages, left=left_crit_noncrit_mapping, alpha=0.8, color='orange', label='Other')
        
        # Add value labels on horizontal bars
        for i, (y, total_pct) in enumerate(zip(y_pos, total_percentages)):
            if total_pct > 0.5:  # Only show label if bar is large enough
                ax2.text(total_pct + 0.1, y, f'{total_pct:.1f}%', va='center', fontweight='bold', fontsize=8)
        
        ax2.set_ylabel('Files (sorted by storage size)', fontsize=12)
        ax2.set_xlabel('Storage Percentage of Total (%)', fontsize=12)
        ax2.set_title('Storage Composition Breakdown - Horizontal View', fontsize=14, fontweight='bold')
        ax2.set_yticks(y_pos)
        ax2.set_yticklabels([f.split('.')[0] for f in filenames])
        ax2.legend(loc='lower right')
        ax2.grid(True, alpha=0.3, axis='x')
        
        plt.tight_layout()
        
        # Save the chart
        composition_chart_path = self.graphs_dir / "stacked_storage_composition.png"
        plt.savefig(composition_chart_path, dpi=300, bbox_inches='tight')
        print(f"Stacked storage composition chart saved to: {composition_chart_path}")
        plt.close()
        
        # Create detailed composition statistics
        self.create_storage_composition_statistics(sorted_results, total_storage_mb, 
                                                 crit_sizes, noncrit_sizes, mapping_sizes, other_sizes)
    
    def create_storage_composition_statistics(self, sorted_results: List[Dict], total_storage_mb: float,
                                            crit_sizes: List[float], noncrit_sizes: List[float], 
                                            mapping_sizes: List[float], other_sizes: List[float]):
        """Create detailed statistics about storage composition."""
        
        # Calculate totals for each component
        total_crit_mb = sum(crit_sizes)
        total_noncrit_mb = sum(noncrit_sizes)
        total_mapping_mb = sum(mapping_sizes)
        total_other_mb = sum(other_sizes)
        
        # Calculate percentages
        crit_percentage = (total_crit_mb / total_storage_mb) * 100
        noncrit_percentage = (total_noncrit_mb / total_storage_mb) * 100
        mapping_percentage = (total_mapping_mb / total_storage_mb) * 100
        other_percentage = (total_other_mb / total_storage_mb) * 100
        
        # Create composition statistics
        composition_stats = {
            'Total Storage (MB)': total_storage_mb,
            'Storage Composition': {
                'Critical Files (.crit)': {
                    'Size (MB)': total_crit_mb,
                    'Percentage': crit_percentage,
                    'Average per File (MB)': total_crit_mb / len(sorted_results) if sorted_results else 0
                },
                'Non-Critical Files (.noncrit)': {
                    'Size (MB)': total_noncrit_mb,
                    'Percentage': noncrit_percentage,
                    'Average per File (MB)': total_noncrit_mb / len(sorted_results) if sorted_results else 0
                },
                'Mapping Files (.mapping)': {
                    'Size (MB)': total_mapping_mb,
                    'Percentage': mapping_percentage,
                    'Average per File (MB)': total_mapping_mb / len(sorted_results) if sorted_results else 0
                },
                'Other Files': {
                    'Size (MB)': total_other_mb,
                    'Percentage': other_percentage,
                    'Average per File (MB)': total_other_mb / len(sorted_results) if sorted_results else 0
                }
            },
            'File-by-File Breakdown': []
        }
        
        # Add individual file breakdowns
        for i, result in enumerate(sorted_results):
            file_breakdown = {
                'filename': result['filename'],
                'total_storage_mb': result['total_storage_mb'],
                'total_percentage': (result['total_storage_mb'] / total_storage_mb) * 100,
                'components': {
                    'critical_mb': crit_sizes[i],
                    'noncritical_mb': noncrit_sizes[i],
                    'mapping_mb': mapping_sizes[i],
                    'other_mb': other_sizes[i]
                }
            }
            composition_stats['File-by-File Breakdown'].append(file_breakdown)
        
        # Save statistics to file
        stats_file = self.graphs_dir / "storage_composition_statistics.txt"
        with open(stats_file, 'w') as f:
            f.write("Storage Composition Statistics\n")
            f.write("=" * 40 + "\n\n")
            
            f.write(f"Total Storage: {total_storage_mb:.2f} MB\n")
            f.write(f"Total Files: {len(sorted_results)}\n\n")
            
            f.write("Storage Composition:\n")
            for component, data in composition_stats['Storage Composition'].items():
                f.write(f"{component}:\n")
                f.write(f"  Size: {data['Size (MB)']:.2f} MB\n")
                f.write(f"  Percentage: {data['Percentage']:.1f}%\n")
                f.write(f"  Average per File: {data['Average per File (MB)']:.2f} MB\n\n")
            
            f.write("Top 10 Files by Storage Size:\n")
            for i, result in enumerate(sorted_results[:10], 1):
                total_pct = (result['total_storage_mb'] / total_storage_mb) * 100
                f.write(f"{i:2d}. {result['filename']:<30} {result['total_storage_mb']:8.2f} MB ({total_pct:5.1f}%)\n")
                f.write(f"    Critical: {crit_sizes[i-1]:6.2f} MB, Non-Critical: {noncrit_sizes[i-1]:6.2f} MB, Mapping: {mapping_sizes[i-1]:6.2f} MB\n")
        
        print(f"Storage composition statistics saved to: {stats_file}")
        
        # Also save as JSON
        stats_json = self.graphs_dir / "storage_composition_statistics.json"
        with open(stats_json, 'w') as f:
            json.dump(composition_stats, f, indent=2)
        
        print(f"Storage composition statistics (JSON) saved to: {stats_json}")
    
    def create_file_type_statistics(self, file_ext: str, file_results: List[Dict]):
        """Create summary statistics for a specific file type."""
        
        # Calculate statistics
        overhead_percentages = [r['overhead_percentage'] for r in file_results]
        overhead_mb = [r['overhead_mb'] for r in file_results]
        original_sizes = [r['original_size_mb'] for r in file_results]
        total_storage = [r['total_storage_mb'] for r in file_results]
        
        stats = {
            'File Type': file_ext.upper(),
            'Total Files Analyzed': len(file_results),
            'Total Original Size (MB)': sum(original_sizes),
            'Total Storage Used (MB)': sum(total_storage),
            'Total Overhead (MB)': sum(overhead_mb),
            'Average Overhead (%)': np.mean(overhead_percentages),
            'Median Overhead (%)': np.median(overhead_percentages),
            'Min Overhead (%)': np.min(overhead_percentages),
            'Max Overhead (%)': np.max(overhead_percentages),
            'Std Dev Overhead (%)': np.std(overhead_percentages),
            'Average File Size (MB)': np.mean(original_sizes),
            'Storage Efficiency (%)': (sum(original_sizes) / sum(total_storage)) * 100
        }
        
        # Save statistics to file
        safe_ext = file_ext.replace('.', '_')
        stats_file = self.graphs_dir / f"summary_statistics_{safe_ext}_files.txt"
        with open(stats_file, 'w') as f:
            f.write(f"CriticalFUSE Storage Overhead Analysis - {file_ext.upper()} Files\n")
            f.write("=" * 60 + "\n\n")
            
            for key, value in stats.items():
                if isinstance(value, float):
                    f.write(f"{key}: {value:.2f}\n")
                else:
                    f.write(f"{key}: {value}\n")
        
        print(f"Summary statistics for {file_ext} files saved to: {stats_file}")
        
        # Also save as JSON
        stats_json = self.graphs_dir / f"summary_statistics_{safe_ext}_files.json"
        with open(stats_json, 'w') as f:
            json.dump(stats, f, indent=2)
        
        print(f"Summary statistics (JSON) for {file_ext} files saved to: {stats_json}")
    
    def create_summary_statistics(self, analysis_results: List[Dict]):
        """Create a summary statistics table."""
        if not analysis_results:
            return
        
        # Calculate statistics
        overhead_percentages = [r['overhead_percentage'] for r in analysis_results]
        overhead_mb = [r['overhead_mb'] for r in analysis_results]
        original_sizes = [r['original_size_mb'] for r in analysis_results]
        total_storage = [r['total_storage_mb'] for r in analysis_results]
        
        # Count JPEG types
        jpeg_types = [r.get('jpeg_type', 'unknown') for r in analysis_results if r.get('jpeg_type')]
        baseline_count = jpeg_types.count('baseline')
        progressive_count = jpeg_types.count('progressive')
        unknown_jpeg_count = jpeg_types.count('unknown')
        
        stats = {
            'Total Files Analyzed': len(analysis_results),
            'Total Original Size (MB)': sum(original_sizes),
            'Total Storage Used (MB)': sum(total_storage),
            'Total Overhead (MB)': sum(overhead_mb),
            'Average Overhead (%)': np.mean(overhead_percentages),
            'Median Overhead (%)': np.median(overhead_percentages),
            'Min Overhead (%)': np.min(overhead_percentages),
            'Max Overhead (%)': np.max(overhead_percentages),
            'Std Dev Overhead (%)': np.std(overhead_percentages),
            'Average File Size (MB)': np.mean(original_sizes),
            'Storage Efficiency (%)': (sum(original_sizes) / sum(total_storage)) * 100,
            'JPEG Type Breakdown': {
                'Baseline JPEGs': baseline_count,
                'Progressive JPEGs': progressive_count,
                'Unknown JPEGs': unknown_jpeg_count
            }
        }
        
        # Save statistics to file
        stats_file = self.graphs_dir / "summary_statistics.txt"
        with open(stats_file, 'w') as f:
            f.write("CriticalFUSE Storage Overhead Analysis - Summary Statistics\n")
            f.write("=" * 60 + "\n\n")
            
            for key, value in stats.items():
                if isinstance(value, dict):
                    f.write(f"{key}:\n")
                    for sub_key, sub_value in value.items():
                        if isinstance(sub_value, float):
                            f.write(f"  {sub_key}: {sub_value:.2f}\n")
                        else:
                            f.write(f"  {sub_key}: {sub_value}\n")
                elif isinstance(value, float):
                    f.write(f"{key}: {value:.2f}\n")
                else:
                    f.write(f"{key}: {value}\n")
        
        print(f"Summary statistics saved to: {stats_file}")
        
        # Also save as JSON
        stats_json = self.graphs_dir / "summary_statistics.json"
        with open(stats_json, 'w') as f:
            json.dump(stats, f, indent=2)
        
        print(f"Summary statistics (JSON) saved to: {stats_json}")
    
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
            # Remove files from mounted folder
            for file_path in self.mounted_folder.glob("*"):
                if file_path.is_file():
                    file_path.unlink()
                    print(f"Removed: {file_path.name}")
            
            print(f"Cleaned up mounted folder: {self.mounted_folder}")
            print(f"Graphs preserved in: {self.graphs_dir}")
                
        except Exception as e:
            print(f"Warning: Could not clean up files: {e}")
    
    def run_analysis(self):
        """Run the complete storage overhead analysis."""
        print("=== CriticalFUSE Storage Overhead Analysis ===")
        print(f"Source folder: {self.source_folder}")
        print(f"Mounted folder: {self.mounted_folder}")
        print(f"Storage folder: {self.storage_folder}")
        if self.max_files:
            print(f"Max files to analyze: {self.max_files}")
        
        # Find source files
        source_files = self.find_source_files()
        
        if not source_files:
            print("No files found in the source folder!")
            return
        
        # Batch detect JPEG types for efficiency
        jpeg_files = [f for f in source_files if f.suffix.lower() in ['.jpg', '.jpeg']]
        if jpeg_files:
            print(f"\nDetecting JPEG types for {len(jpeg_files)} files...")
            jpeg_types = self.batch_detect_jpeg_types(jpeg_files)
            print(f"JPEG type detection complete: {sum(1 for t in jpeg_types.values() if t == 'progressive')} progressive, {sum(1 for t in jpeg_types.values() if t == 'baseline')} baseline")
        else:
            jpeg_types = {}
        
        # Analyze each file
        for i, source_file in enumerate(source_files, 1):
            print(f"\nAnalyzing file {i}/{len(source_files)}: {source_file.name}")
            
            # Use pre-detected JPEG type if available
            if source_file.name in jpeg_types:
                jpeg_type = jpeg_types[source_file.name]
            else:
                jpeg_type = 'unknown'
            
            file_results = self.calculate_storage_overhead(source_file, jpeg_type)
            
            if file_results:
                self.results['tests'].append(file_results)
        
        # Generate graphs
        if self.results['tests']:
            print("\n=== Generating Storage Overhead Analysis Graphs ===")
            self.create_storage_overhead_graphs(self.results['tests'])
        
        # Save results
        self.save_results()
        
        # Cleanup
        self.cleanup()


def main():
    parser = argparse.ArgumentParser(description='Analyze CriticalFUSE Storage Overhead')
    parser.add_argument('source_folder', help='Path to folder containing original files')
    parser.add_argument('mounted_folder', help='Path to mounted FUSE folder')
    parser.add_argument('storage_folder', help='Path to storage folder where FUSE creates files')
    parser.add_argument('--output-dir', '-o', required=True, help='Path to output directory for results')
    parser.add_argument('--output', help='Output JSON file for results')
    parser.add_argument('--max-files', type=int, help='Maximum number of files to analyze (default: all)')
    parser.add_argument('--no-cleanup', action='store_true', help='Skip cleanup after testing')
    
    args = parser.parse_args()
    
    # Validate paths
    if not os.path.exists(args.source_folder):
        print(f"Error: Source folder '{args.source_folder}' does not exist!")
        return 1
    
    if not os.path.exists(args.mounted_folder):
        print(f"Error: Mounted folder '{args.mounted_folder}' does not exist!")
        return 1
    
    if not os.path.exists(args.storage_folder):
        print(f"Error: Storage folder '{args.storage_folder}' does not exist!")
        return 1
    
    # Create analyzer and run analysis
    analyzer = StorageOverheadAnalyzer(args.source_folder, args.mounted_folder, 
                                      args.storage_folder, args.output_dir, args.output, args.max_files)
    
    try:
        analyzer.run_analysis()
        
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
