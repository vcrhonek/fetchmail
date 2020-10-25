#!/bin/sh

set -eu
cd "$(realpath $(dirname $0))/.."

ver=$(sed -n '/Project-Id-Version:/s/.*fetchmail \([^\\]\+\).*/\1/p' po/fetchmail.pot )
expver=${VERSION:-$ver}
git log --oneline --all-match --grep ".* translation ...\\? fetchmail.$expver" --pretty=format:%ad:%an:%s --date=unix | \
	sed 's/\(Update\|Add new\) <\([^>]\+\)> \(.*\) translation .*/\2:\3/' | \
sort -n | \
while IFS=: read date author code lang ; do
  printf '* %-6s %s [%s]\n' "$code:" "$author" "$lang"
done

echo >&2 $'\nIf this is not what you expected, try overriding f. i.' "VERSION=$ver"
