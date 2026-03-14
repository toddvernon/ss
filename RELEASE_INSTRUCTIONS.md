# ss Release Instructions

## Prerequisites

- `gh` CLI authenticated with GitHub
- Linux machine accessible (shares this directory via Dropbox)

## Steps

1. Run the release script from macOS:
   ```bash
   ./release.sh 1.1
   ```

2. The script updates `SsVersion.h` and pauses. SSH to the Linux machine and build:
   ```bash
   cd ~/dev/cx/cx_apps/ss && make clean && make
   ```

3. Wait for Dropbox to sync the linux binary over

4. Come back to the Mac and press Enter. The script handles the rest:
   - Builds macOS binary
   - Creates `ss-macos.tar.gz` and `ss-linux.tar.gz`
   - Commits, tags, and pushes
   - Creates the GitHub release with both tarballs

## Notes

- The website download links use version-less URLs (`releases/latest/download/ss-macos.tar.gz`), so the site never needs updating for new releases.
- If the Linux binary isn't found, the script warns and skips the Linux tarball.
- Tarballs contain: `ss`, `install.sh`, `ss_help.md`.
