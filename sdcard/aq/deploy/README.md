# Manual Deployment Notes

This directory is for local deployment records. It exists because firmware is
uploaded manually over USB.

Recommended workflow:

1. Build firmware locally.
2. Upload manually from PlatformIO.
3. Copy the active SD layout to the card.
4. Record the branch, commit, upload time, and observed boot result in
   `last-upload.txt`.

The firmware does not read this directory. It is for operator notes only.
