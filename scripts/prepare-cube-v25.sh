#!/usr/bin/env bash
set -euo pipefail
# Only runs in the disposable Actions checkout; never resets a user's checkout.
if grep -q 'set(PROJECT_VER "2.5.0")' CMakeLists.txt; then
    exit 0
fi
stage_dir=$(mktemp -d)
cp -a .github "$stage_dir/github"
cp -a .migration-v25 "$stage_dir/overlay"
cp -a main/scanner "$stage_dir/scanner"
cp -a tests "$stage_dir/tests"
git fetch --no-tags --depth=1 https://github.com/78/xiaozhi-esp32.git ac6deed3d8e75348475364bf40ad953c6cd48054
git read-tree --reset -u FETCH_HEAD
rsync -a --delete "$stage_dir/github/" .github/
cp -a "$stage_dir/scanner" main/scanner
cp -a "$stage_dir/tests" tests
cp -a "$stage_dir/overlay" .migration-v25
cp -a .migration-v25/source/. .
echo 'CUBE_MIGRATED=true' >> "$GITHUB_ENV"
