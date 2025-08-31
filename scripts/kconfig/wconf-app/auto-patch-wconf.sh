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
