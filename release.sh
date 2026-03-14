#!/bin/bash
#-------------------------------------------------------------------------------------------------
#
#  release.sh
#  ss (spreadsheet)
#
#  Copyright 2022-2026 Todd Vernon. All rights reserved.
#  Licensed under the Apache License, Version 2.0
#  See LICENSE file for details.
#
#  Release script - builds, packages, and publishes an ss release.
#
#  Usage: ./release.sh 1.0
#
#  Steps:
#    1. Updates SsVersion.h
#    2. Pauses for Linux build (Dropbox sync)
#    3. Builds macOS binary
#    4. Creates platform tarballs
#    5. Commits, tags, pushes
#    6. Creates GitHub release with assets
#
#-------------------------------------------------------------------------------------------------

set -e

if [ -z "$1" ]; then
    echo "Usage: ./release.sh <version>"
    echo "Example: ./release.sh 1.0"
    exit 1
fi

VERSION=$1
STAGING="/tmp/ss-release-$$"

echo "=== ss release v$VERSION ==="
echo ""

#-------------------------------------------------------------------------------------------------
# 1. Update SsVersion.h
#-------------------------------------------------------------------------------------------------

cat > SsVersion.h << EOF
//-------------------------------------------------------------------------------------------------
//
//  SsVersion.h
//  ss (spreadsheet)
//
//  Copyright 2022-2026 Todd Vernon. All rights reserved.
//  Licensed under the Apache License, Version 2.0
//  See LICENSE file for details.
//
//  Version string for ss. Updated by release.sh script.
//
//-------------------------------------------------------------------------------------------------

#ifndef _SsVersion_h_
#define _SsVersion_h_

#define SS_VERSION "$VERSION"

#endif
EOF

echo "Updated SsVersion.h to $VERSION"
echo ""

#-------------------------------------------------------------------------------------------------
# 2. Pause for Linux build
#-------------------------------------------------------------------------------------------------

echo "SsVersion.h has been updated. Build on Linux now:"
echo "  cd ~/dev/cx/cx_apps/ss && make clean && make"
echo ""
echo "Press Enter when the Linux build is complete..."
read -r

#-------------------------------------------------------------------------------------------------
# 3. Build macOS
#-------------------------------------------------------------------------------------------------

echo "Building macOS..."
make clean && make
echo ""

#-------------------------------------------------------------------------------------------------
# 4. Verify binaries
#-------------------------------------------------------------------------------------------------

if [ ! -f darwin_arm64/ss ]; then
    echo "Error: darwin_arm64/ss not found"
    exit 1
fi

if [ ! -f linux_x86_64/ss ]; then
    echo "Warning: linux_x86_64/ss not found - Linux tarball will be skipped"
fi

#-------------------------------------------------------------------------------------------------
# 5. Create tarballs
#-------------------------------------------------------------------------------------------------

rm -rf "$STAGING"

# macOS tarball (version-less name so releases/latest/download URLs always work)
MACDIR="$STAGING/ss-macos"
mkdir -p "$MACDIR"
cp darwin_arm64/ss "$MACDIR/ss"
cp install.sh "$MACDIR/install.sh"
cp ss_help.md "$MACDIR/ss_help.md"
tar czf "ss-macos.tar.gz" -C "$STAGING" "ss-macos"
echo "Created ss-macos.tar.gz"

# Linux tarball
if [ -f linux_x86_64/ss ]; then
    LINDIR="$STAGING/ss-linux"
    mkdir -p "$LINDIR"
    cp linux_x86_64/ss "$LINDIR/ss"
    cp install.sh "$LINDIR/install.sh"
    cp ss_help.md "$LINDIR/ss_help.md"
    tar czf "ss-linux.tar.gz" -C "$STAGING" "ss-linux"
    echo "Created ss-linux.tar.gz"
fi

rm -rf "$STAGING"
echo ""

#-------------------------------------------------------------------------------------------------
# 6. Commit, tag, push
#-------------------------------------------------------------------------------------------------

git add SsVersion.h
git commit -m "Release v$VERSION"
git tag "v$VERSION"
git push
git push --tags
echo ""

#-------------------------------------------------------------------------------------------------
# 7. Create GitHub release
#-------------------------------------------------------------------------------------------------

ASSETS="ss-macos.tar.gz"
if [ -f "ss-linux.tar.gz" ]; then
    ASSETS="$ASSETS ss-linux.tar.gz"
fi

RELEASE_BODY="$(cat <<EOF
## ss $VERSION

Pre-built binaries for macOS and Linux.

### Install

\`\`\`bash
# Download and extract
tar xzf ss-macos.tar.gz   # or ss-linux.tar.gz
cd ss-macos

# Install to /usr/local
./install.sh
\`\`\`

This installs:
- \`ss\` binary to \`/usr/local/bin/\`
- Help file to \`/usr/local/share/ss/\`

### Build from source

See the [README](https://github.com/toddvernon/ss#getting-started) for build instructions.
EOF
)"

gh release create "v$VERSION" \
    --title "ss $VERSION" \
    --notes "$RELEASE_BODY" \
    $ASSETS

echo ""
echo "=== Release v$VERSION published ==="
echo "https://github.com/toddvernon/ss/releases/tag/v$VERSION"

# Clean up tarballs from working directory
rm -f "ss-macos.tar.gz" "ss-linux.tar.gz"
