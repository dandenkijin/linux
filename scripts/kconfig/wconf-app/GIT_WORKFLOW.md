# Git Workflow for Tauri-based Kernel Configuration Tool (wconf)

## Overview

This document describes the git workflow and repository management for the Tauri-based kernel configuration tool (wconf) development.

## Repository Setup

### Initialize Git Repository
```bash
cd /home/denkijin/workspace/linux-6.16.4
git init
git config user.name "Your Name"
git config user.email "your.email@example.com"
```

### Add Upstream Remote
```bash
git remote add upstream https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git
```

### Create Initial Commit
```bash
git add .
git commit -m "Linux kernel 6.16.4 (patched from 6.16)
Current state with distribution patches applied."
```

## Development Workflow

### Add wconf Implementation to Git
```bash
git add scripts/kconfig/wconf-app/
git add scripts/kconfig/wconf.c
git add scripts/kconfig/Makefile
git add Documentation/kbuild/kconfig-gui.rst
git commit -m "Add Tauri-based kernel configuration tool (wconf)"
```

## Verification Approaches

### Compare Against Clean Upstream
```bash
# Clone clean 6.16 for comparison
git clone --branch v6.16 --depth 1 https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git /tmp/linux-6.16-clean

# Verify our files don't exist in clean 6.16 (no conflicts)
ls -la /tmp/linux-6.16-clean/scripts/kconfig/wconf* 2>/dev/null || echo "Files don't exist in clean 6.16 (good!)"
```

### Create Clean Patches
```bash
# Create patch of only our changes
git diff HEAD~1 -- scripts/kconfig/wconf-app/ scripts/kconfig/wconf.c scripts/kconfig/Makefile Documentation/kbuild/kconfig-gui.rst > wconf-implementation.patch

# Or create formatted patch for submission
git format-patch -1 --stdout > wconf-gui-config.patch
```

## Alternative Approaches

### Working with Branches
If you need to maintain separate branches:
```bash
# Create branches for different versions
git checkout -b base-6.16 upstream/v6.16
git checkout -b feature/wconf-gui-config base-6.16
```

### Reverting Patches (if needed)
```bash
# Revert specific patch commits
git revert <commit-hash>
```

## Verification Commands

### Check Repository Validity
```bash
# Verify kernel identity
ls -la Makefile Kconfig COPYING README MAINTAINERS
grep -E "VERSION|PATCHLEVEL|SUBLEVEL" Makefile

# Check for kernel directories
ls -la arch drivers fs kernel lib mm net
```

### Confirm No Conflicts
```bash
# Compare with clean upstream
diff -r /tmp/linux-6.16-clean/scripts/kconfig/ scripts/kconfig/ | grep -E "(wconf|wconfig)" || echo "No differences in our files"
```

## Patch Creation for Upstream Submission

### Clean Patch Against 6.16
```bash
# Create patch suitable for upstream
diff -ruN /tmp/linux-6.16-clean/scripts/kconfig/wconf* scripts/kconfig/wconf* > wconf-additions.patch
diff -u /tmp/linux-6.16-clean/scripts/kconfig/Makefile scripts/kconfig/Makefile | grep wconfig > makefile-changes.patch
```

## Automated Patch Comparison Script

For automated comparison and patching, you can use the following script:

```bash
#!/bin/bash
# auto-patch-wconf.sh - Automated patch comparison script
 
CLEAN_KERNEL="/tmp/linux-6.16-clean"
WORKING_DIR="$(pwd)"
 
# Function to compare directories
compare_directories() {
    local clean_dir="$1"
    local working_dir="$2"
    local pattern="$3"
     
    echo "Comparing $pattern files..."
    diff -ruN "$clean_dir" "$working_dir" | grep -E "$pattern" || echo "No differences found for $pattern"
}
 
# Function to create patches
create_patches() {
    echo "Creating patches..."
     
    # Create wconf additions patch
    diff -ruN "$CLEAN_KERNEL/scripts/kconfig/wconf-app/" "scripts/kconfig/wconf-app/" > wconf-additions.patch 2>/dev/null || true
     
    # Create Makefile changes patch
    diff -u "$CLEAN_KERNEL/scripts/kconfig/Makefile" "scripts/kconfig/Makefile" | grep wconfig > makefile-wconfig-changes.patch
     
    # Create wconf.c patch
    diff -u "$CLEAN_KERNEL/scripts/kconfig/wconf.c" "scripts/kconfig/wconf.c" > wconf-c-wrapper.patch 2>/dev/null || true
     
    # Create documentation patch
    diff -u "$CLEAN_KERNEL/Documentation/kbuild/kconfig-gui.rst" "Documentation/kbuild/kconfig-gui.rst" > kconfig-gui-doc.patch 2>/dev/null || true
     
    echo "Patches created successfully!"
}
 
# Main execution
main() {
    # Check if clean kernel exists
    if [ ! -d "$CLEAN_KERNEL" ]; then
        echo "Clean kernel not found. Cloning..."
        mkdir -p /tmp
        git clone --branch v6.16 --depth 1 https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git "$CLEAN_KERNEL"
    fi
     
    # Compare specific components
    compare_directories "$CLEAN_KERNEL/scripts/kconfig/" "scripts/kconfig/" "(wconf|wconfig)"
    compare_directories "$CLEAN_KERNEL/Documentation/kbuild/" "Documentation/kbuild/" "kconfig-gui"
     
    # Create patches if requested
    if [ "$1" = "--patch" ]; then
        create_patches
    fi
}
 
# Run main function
main "$@"
```

Usage:
```bash
# Compare differences only
./auto-patch-wconf.sh
 
# Create patches
./auto-patch-wconf.sh --patch
```

This workflow ensures our changes can be cleanly applied to upstream kernel versions while maintaining compatibility with the existing patched environment.