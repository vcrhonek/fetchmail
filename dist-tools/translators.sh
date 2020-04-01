#!/bin/sh

set -eu
cd "$(realpath $(dirname $0))/.."

ver=$(sed -n '/Project-Id-Version:/s/.*fetchmail \([^\\]\+\).*/\1/p' po/fetchmail.pot )
git log --oneline --all-match --grep "Update .* translation to fetchmail.$ver" --pretty=format:%ad:%an:%s --date=unix | \
sed 's/Update <\([^>]\+\)> \(.*\) translation to.*/\1:\2/' | \
sort -n | \
while IFS=: read date author code lang ; do
  printf '* %-6s %s [%s]\n' "$code:" "$author" "$lang"
done
