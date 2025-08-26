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
    
    def calculate_storage_overhead(self, source_file: Path) -> Dict:
        """
        Calculate storage overhead for a single file.
        
        Args:
            source_file: Path to source file
            
        Returns:
            Dictionary with storage overhead analysis
        """
        print(f"Analyzing storage overhead for: {source_file.name}")
        
        # Get original file size
        original_size = source_file.stat().st_size
        
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
            'file_count': len(related_files)
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
        
        # Create file-by-file breakdown
        plt.figure(figsize=(14, 8))
        
        # Create bar chart of overhead percentages
        x_pos = range(len(filenames))
        plt.bar(x_pos, overhead_percentages, alpha=0.7, color='orange', edgecolor='black')
        
        # Add value labels on bars
        for i, (x, y) in enumerate(zip(x_pos, overhead_percentages)):
            plt.text(x, y + 0.5, f'{y:.1f}%', ha='center', va='bottom', fontweight='bold', fontsize=8)
        
        plt.xlabel('Files', fontsize=14)
        plt.ylabel('Storage Overhead (%)', fontsize=14)
        plt.title(f'Storage Overhead by File - {file_ext.upper()} Files', fontsize=16, fontweight='bold')
        plt.xticks(x_pos, [f.split('.')[0] for f in filenames], rotation=45, ha='right')
        plt.grid(True, alpha=0.3, axis='y')
        
        # Add statistics line
        mean_overhead = np.mean(overhead_percentages)
        plt.axhline(y=mean_overhead, color='red', linestyle='--', alpha=0.8, 
                   label=f'Mean: {mean_overhead:.1f}%')
        plt.legend()
        
        plt.tight_layout()
        
        breakdown_graph_path = self.graphs_dir / f"file_breakdown_{safe_ext}_files.png"
        plt.savefig(breakdown_graph_path, dpi=300, bbox_inches='tight')
        print(f"File breakdown analysis for {file_ext} files saved to: {breakdown_graph_path}")
        plt.close()
        
        # Create summary statistics for this file type
        self.create_file_type_statistics(file_ext, file_results)
    
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
            'Storage Efficiency (%)': (sum(original_sizes) / sum(total_storage)) * 100
        }
        
        # Save statistics to file
        stats_file = self.graphs_dir / "summary_statistics.txt"
        with open(stats_file, 'w') as f:
            f.write("CriticalFUSE Storage Overhead Analysis - Summary Statistics\n")
            f.write("=" * 60 + "\n\n")
            
            for key, value in stats.items():
                if isinstance(value, float):
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
        
        # Analyze each file
        for i, source_file in enumerate(source_files, 1):
            print(f"\nAnalyzing file {i}/{len(source_files)}: {source_file.name}")
            file_results = self.calculate_storage_overhead(source_file)
            
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
