#!/bin/sh
#
# ss installer
# Installs ss binary and help files
#

PREFIX="${PREFIX:-/usr/local}"
BIN_DIR="$PREFIX/bin"
SHARE_DIR="$PREFIX/share/ss"

echo "Installing ss to $PREFIX..."

# Install binary
if [ -f ss ]; then
    sudo cp ss "$BIN_DIR/ss"
    sudo chmod 755 "$BIN_DIR/ss"
    # Clear quarantine on macOS
    if [ "$(uname -s)" = "Darwin" ]; then
        sudo xattr -cr "$BIN_DIR/ss" 2>/dev/null
    fi
    echo "  $BIN_DIR/ss"
else
    echo "Error: ss binary not found"
    exit 1
fi

# Install help file and example
sudo mkdir -p "$SHARE_DIR"
if [ -f ss_help.md ]; then
    sudo cp ss_help.md "$SHARE_DIR/ss_help.md"
    sudo chmod 644 "$SHARE_DIR/ss_help.md"
    echo "  $SHARE_DIR/ss_help.md"
fi
if [ -f CapTableExample.sheet ]; then
    sudo cp CapTableExample.sheet "$SHARE_DIR/CapTableExample.sheet"
    sudo chmod 644 "$SHARE_DIR/CapTableExample.sheet"
    echo "  $SHARE_DIR/CapTableExample.sheet"
fi

echo "Done. Run 'ss' to start."
echo ""
echo "Press Enter to close..."
read dummy
