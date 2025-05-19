#!/bin/bash
#
# SLDMKVCacheStore Python gRPC Stub Generator
# ===========================================
#
# This script generates Python gRPC stubs from the proto definitions
# used by the SLDMKVCacheStore service. The generated stubs are used
# by the Python client to communicate with the service.
#
# Design Principles:
# -----------------
# 1. Robustness - Exits on any error and validates all prerequisites
# 2. Discoverability - Uses clear, colorful output to show progress and results
# 3. Safety - Verifies all directories exist and creates them if necessary
# 4. Completeness - Processes all .proto files in the proto directory
# 5. Maintainability - Organized with functions and clear variable names
#
# The script finds all .proto files in the specified proto directory,
# generates Python code for them using the grpcio-tools package,
# and fixes import statements in the generated files if needed.

# Exit on any error, undefined variable usage, or pipe failure
set -euo pipefail

# Define colors for better output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Define directories
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROTO_DIR="${SCRIPT_DIR}/../proto"
OUTPUT_DIR="${SCRIPT_DIR}/sldmkvcachestore/proto"

# ====================================
# Utility functions
# ====================================

info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
}

handle_error() {
    error "$1"
    exit 1
}

check_requirements() {
    # Check if the proto directory exists
    if [ ! -d "$PROTO_DIR" ]; then
        handle_error "Proto directory not found: $PROTO_DIR"
    fi

    # Check if protoc compiler is available through grpcio-tools
    if ! python -c "import grpc_tools.protoc" 2>/dev/null; then
        error "Python grpcio-tools package not found."
        echo "Please install it with: pip install grpcio-tools"
        exit 1
    fi

    info "All requirements satisfied."
}

ensure_output_directory() {
    # Create the output directory if it doesn't exist
    if [ ! -d "$OUTPUT_DIR" ]; then
        info "Creating output directory: $OUTPUT_DIR"
        mkdir -p "$OUTPUT_DIR"
    fi

    # Create an __init__.py file in the output directory if it doesn't exist
    if [ ! -f "${OUTPUT_DIR}/__init__.py" ]; then
        info "Creating __init__.py in output directory"
        touch "${OUTPUT_DIR}/__init__.py"
    fi
}

fix_imports() {
    info "Fixing import statements in generated files..."
    count=0
    
    # Find all Python files in the output directory
    find "$OUTPUT_DIR" -name "*.py" | while read -r file; do
        # Skip __init__.py files
        if [[ "$(basename "$file")" == "__init__.py" ]]; then
            continue
        fi
        
        # Replace relative imports with absolute imports
        sed -i 's/from \([^ ]*\) import/from sldmkvcachestore.proto.\1 import/g' "$file"
        ((count++))
    done
    
    info "Fixed imports in $count files."
}

generate_stubs() {
    info "Generating Python gRPC stubs..."
    
    # Ensure the output directory exists
    ensure_output_directory
    
    # Get a list of all .proto files
    proto_files=()
    while IFS= read -r -d '' file; do
        proto_files+=("$file")
    done < <(find "$PROTO_DIR" -name "*.proto" -print0)
    
    if [ ${#proto_files[@]} -eq 0 ]; then
        handle_error "No .proto files found in $PROTO_DIR"
    fi
    
    info "Found ${#proto_files[@]} .proto files to process."
    
    # Generate Python stubs for each proto file
    for proto_file in "${proto_files[@]}"; do
        proto_name=$(basename "$proto_file")
        info "Generating stubs for $proto_name..."
        
        python -m grpc_tools.protoc \
            -I"$PROTO_DIR" \
            --python_out="$OUTPUT_DIR" \
            --grpc_python_out="$OUTPUT_DIR" \
            "$proto_file" || handle_error "Failed to generate stubs for $proto_name"
            
        success "Generated stubs for $proto_name"
    done
    
    # Fix import statements in generated files
    fix_imports
    
    success "All stubs generated successfully"
    info "Output directory: $OUTPUT_DIR"
}

# ====================================
# Main script execution
# ====================================

info "Starting Python gRPC stub generation for SLDMKVCacheStore"
info "Proto directory: $PROTO_DIR"
info "Output directory: $OUTPUT_DIR"

# Check requirements
check_requirements

# Generate the stubs
generate_stubs

success "Python gRPC stubs generated successfully!"
echo -e "\nYou can now use the Python client with:"
echo -e "  pip install -e .\n" 