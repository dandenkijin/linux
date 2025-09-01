#!/usr/bin/env python3

import subprocess
import sys
import os

def test_kconfig_parser():
    # Change to the wconf-app directory
    os.chdir('/home/denkijin/workspace/linux/scripts/kconfig/wconf-app')
    
    # Try to run the parser on the fs/Kconfig file
    try:
        result = subprocess.run([
            'cargo', 'run', '--bin', 'test_parser', '--', '../fs/Kconfig'
        ], capture_output=True, text=True, timeout=30)
        
        print("=== STDOUT ===")
        print(result.stdout)
        print("\n=== STDERR ===")
        print(result.stderr)
        print(f"\n=== Return Code: {result.returncode} ===")
        
    except subprocess.TimeoutExpired:
        print("Parser timed out after 30 seconds")
    except Exception as e:
        print(f"Error running parser: {e}")

if __name__ == "__main__":
    test_kconfig_parser()