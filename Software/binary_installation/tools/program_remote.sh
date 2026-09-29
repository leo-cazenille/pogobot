#!/bin/bash
set -euo pipefail

package_dir=$(cd "$(dirname "$0")" && pwd)
marker_file=$(mktemp)
trap 'rm -f "$marker_file"' EXIT
printf 'FlashIsOK\n' > "$marker_file"

echo "Using iceprog to program the remote"
# iceprog -o takes physical flash offsets, not CPU-mapped addresses.
iceprog -o 0x0 "$package_dir/bootloader/bootloader_pogobotv3/bootloader.bin"
iceprog -o 0x40000 "$package_dir/remocon/remocon_pogobotv3/gateware/remocon_pogobotv3.bin"
iceprog -o 0x60000 "$package_dir/remocon/remocon_pogobotv3/software/pogobios/pogobios.bin"
iceprog -o 0x88000 "$marker_file"
