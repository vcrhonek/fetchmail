#!/usr/bin/env python
#
# Collect statistics on current release.
# This needs a halfway recent Python 3.

import sys
import subprocess
import tempfile
import re
from datetime import date

# Get version and date
with tempfile.TemporaryDirectory() as tmpdirname:
    subprocess.check_call("git archive --format=tar HEAD | ( cd {} && tar -xf -)".format(tmpdirname), shell=True)
    LOC = subprocess.getoutput("cat {}/*.[chly] 2>/dev/null | wc -l".format(tmpdirname)).strip()
    try:
        with open("configure.ac") as f:
            AC_INIT = list(filter(lambda x: re.match('AC_INIT', x),
                                  f.readlines()))[0]
            VERS = re.search(r'AC_INIT\(\[.+\],\[(.+)\],\[.*\]', AC_INIT).group(1)
            DATE = date.today().isoformat()
            print("fetchmail-{} (released {}, {} LoC):".format(VERS, DATE, LOC))

    except:
        print("Cannot extract version from configure.ac!", file=sys.stderr)
        raise

# end of getstats.py
