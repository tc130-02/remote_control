#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <cmake-build-directory> <output.deb>" >&2
    exit 1
fi

build_dir=$(cd "$1" && pwd)
output=$(realpath -m "$2")
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
package_root=$(mktemp -d)
trap 'rm -rf "$package_root"' EXIT

mkdir -p \
    "$package_root/DEBIAN" \
    "$package_root/opt/remote-control" \
    "$package_root/usr/bin" \
    "$package_root/usr/share/applications"

cp "$script_dir/linux/DEBIAN/control" "$package_root/DEBIAN/control"
cp "$build_dir/remote_control" "$package_root/opt/remote-control/remote_control"
cp "$build_dir/linux_server" "$package_root/opt/remote-control/linux_server"
cp "$script_dir/linux/remote-control" "$package_root/usr/bin/remote-control"
cp "$script_dir/linux/remote-control.desktop" \
    "$package_root/usr/share/applications/remote-control.desktop"

chmod 0755 \
    "$package_root/opt/remote-control/remote_control" \
    "$package_root/opt/remote-control/linux_server" \
    "$package_root/usr/bin/remote-control"

mkdir -p "$(dirname -- "$output")"
dpkg-deb --build --root-owner-group "$package_root" "$output"
