project('fetchmail', 'c', version: '6.5.0.beta7',
  default_options : ['warning_level=2',
                     'c_std=gnu99'])

cc = meson.get_compiler('c')
cdata = configuration_data()

ac_ver = run_command('./getversion.sh', check:true).stdout().strip()
if ac_ver != meson.project_version()
  error('version mismatch between configure.ac and meson.build')
endif

common_arguments = []
c_arguments = []
# cpp_arguments = []

if cc.get_id() == 'clang' or cc.get_id() == 'gcc'
  if get_option('optimization') != '0'
    common_arguments += [ '-D_FORTIFY_SOURCE=2' ]
  endif
endif

c_arguments += common_arguments
# cpp_arguments += common_arguments

add_project_arguments(c_arguments,   language: 'c')
add_project_link_arguments(c_arguments, language: 'c')
#add_project_arguments(cpp_arguments, language: 'cpp')
#add_project_link_arguments(cpp_arguments, language: 'cpp')

flex = find_program('flex')
bison = find_program('bison')

OpenSSL_dep = dependency('openssl', version: '>=1.1.1', modules: 'SSL')

link_deps = [OpenSSL_dep]

resolv_library = cc.find_library('resolv', required: false)
if resolv_library.found()
  link_deps += resolv_library
endif

# localisation (l10n) and internationalization (i18n)
subdir('po')

# cmake_cxx_standard = '14'
# execute_process(['COMMAND', './getversion.sh', 'WORKING_DIRECTORY', project_source_dir, 'OUTPUT_VARIABLE', 'VERSION', 'OUTPUT_STRIP_TRAILING_WHITESPACE'])
fetchmail_sources = ['base64.c', 'checkalias.c', 'conf.c', 'cram.c', 'daemon.c', 'driver.c', 'env.c', 'etrn.c', 'fetchmail.c', 'fetchmail.h', 'fm_getaddrinfo.c', 'fm_md5.h', 'fm_realpath.c', 'fm_strl.h', 'getpass.c', 'gettext.h', 'gssapi.c', 'i18n.h', 'idle.c', 'idlist.c', 'imap.c', 'interface.c', 'kerberos.c', 'kerberos.h', 'lock.c', 'lock.h', 'md5c.c', 'md5ify.c', 'mx.h', 'mxget.c', 'netrc.c', 'netrc.h', 'ntlm.h', 'ntlmsubr.c', 'odmr.c', 'opie.c', 'options.c', 'pop2.c', 'pop3.c', 'report.c', 'rfc2047e.c', 'rfc822.c', 'rfc822valid.c', 'rpa.c', 'sdump.c', 'sdump.h', 'servport.c', 'sink.c', 'smb.h', 'smbbyteorder.h', 'smbdes.c', 'smbdes.h', 'smbencrypt.c', 'smbencrypt.h', 'smbmd4.c', 'smbmd4.h', 'smbtypes.h', 'smbutil.c', 'smtp.c', 'smtp.h', 'socket.c', 'socket.h', 'starttls.c', 'stpcpy.c', 'strlcat.c', 'strlcpy.c', 'tls-aux.c', 'tls-aux.h', 'transact.c', 'tunable.h', 'uid.c', 'uid_db.c', 'uid_db.h', 'unmime.c', 'x509_name_match.c', 'xmalloc.c', 'xmalloc.h', 'ucs/norm_charmap.h', 'ucs/norm_charmap.c']

lgen = generator(flex, output: ['@BASENAME@.c', '@BASENAME@.h'], arguments: ['-o', '@OUTPUT0@', '@INPUT@'])
lexer = lgen.process('rcfile_l.l')
pgen = generator(bison, output: ['@BASENAME@.c', '@BASENAME@.h'],
  arguments:['-d', '-t', '@INPUT@', '--defines=@OUTPUT1@', '--output=@OUTPUT0@'])
parser = pgen.process('rcfile_y.y')
#
# target_include_directories(['fetchmail', 'PUBLIC', '${PROJECT_BINARY_DIR}', 
# '${PROJECT_SOURCE_DIR}'])
# target_link_libraries(['fetchmail', 'OpenSSL::SSL'])
# configure_file([cmake_current_source_dir, '/config.h.cmake.in', cmake_current_binary_dir, '/config.h'])
#if 'RESOLV_LIBRARY'
  # target_link_libraries(['fetchmail', resolv_library])
#endif

#if 'Intl_LIBRARY'
#  # target_link_libraries(['fetchmail', intl_library])
#endif
#
#  ######## autoconf stuff

check_headers = [
  ['HAVE_ARPA_NAMESER_H', 'arpa/nameser.h'],
  ['HAVE_GSSAPI_GSSAPI_GENERIC_H', 'gssapi/gssapi/generic.h'],
  ['HAVE_GSSAPI_GSSAPI_H', 'gssapi/gssapi.h'],
  ['HAVE_GSSAPI_H', 'gssapi.h'],
  ['HAVE_GSS_H', 'gss.h'],
  ['HAVE_HESIOD_H', 'hesiod.h'],
  ['HAVE_INTTYPES_H', 'inttypes.h'],
  ['HAVE_LANGINFO_H', 'langinfo.h'],
  ['HAVE_MEMORY_H', 'memory.h'],
  ['HAVE_RESOLV_H', 'resolv.h'],
  ['HAVE_STDINT_H', 'stdint.h'],
  ['HAVE_STDLIB_H', 'stdlib.h'],
  ['HAVE_STRINGS_H', 'strings.h'],
  ['HAVE_STRING_H', 'string.h'],
  ['HAVE_SYS_FCNTL_H', 'sys/fcntl.h'],
  ['HAVE_SYS_ITIMER_H', 'sys/itimer.h'],
  ['HAVE_SYS_STAT_H', 'sys/stat.h'],
  ['HAVE_SYS_TYPES_H', 'sys/types.h'],
  ['HAVE_UNISTD_H', 'unistd.h'],
  ['HAVE_WOLFSSL_OPTIONS_H', 'wolfssl/options.h'],
  ['ENABLE_NLS', 'libintl.h'],
]

foreach h : check_headers
  if cc.has_header(h.get(1))
    cdata.set(h.get(0), 1)
  endif
endforeach

check_declarations = [
# check token ['HAVE_DECL_GSS_C_NT_HOSTBASED_SERVICE']
  ['h_errno',                 'netdb.h'],
  ['LIBRESSL_VERSION_NUMBER', 'openssl/ssl.h'],
  ['TLS1_2_VERSION',          'openssl/ssl.h'],
  ['TLS1_3_VERSION',          'openssl/ssl.h'],
  ['TLS_MAX_VERSION',         'openssl/ssl.h'],
]

foreach f : check_declarations
  cdata.set10('HAVE_DECL_'+f.get(0).to_upper(),
              cc.has_header_symbol(f.get(1), f.get(0)))
endforeach

check_symbols = [
  ['getopt_long', 'getopt.h'],
]

foreach f : check_symbols
  if cc.has_header_symbol(f.get(1), f.get(0))
    cdata.set('HAVE_'+f.get(0).to_upper(), 1)
  endif
endforeach

check_functions = [
# check token ['HAVE_CFLOCALECOPYCURRENT']
# check token ['HAVE_CFPREFERENCESCOPYAPPVALUE']
  ['dcgettext', '#include<libintl.h>'],
  ['gettext', '#include <libintl.h>'],
# check token ['HAVE_ICONV']
  ['inet_aton', '#include<arpa/inet.h>'],
# check token ['HAVE_LIB45']
# check token ['HAVE_LIBCRYPTO']
# check token ['HAVE_LIBINET']
# check token ['HAVE_LIBKRB5']
# check token ['HAVE_LIBOPIE']
# check token ['HAVE_LIBSOCKET']
# check token ['HAVE_LIBSOCKS']
# check token ['HAVE_LIBSOCKS5']
# check token ['HAVE_PKG_hesiod']
# check token ['HAVE_SOCKS']
  ['res_search', '#include <resolv.h>'],
  ['stpcpy',    '#include <string.h>'],
  ['strlcat',   '#include <string.h>'],
  ['strlcpy',   '#include <string.h>'],
  ['strsignal', '#include <string.h>'],
  ['vsyslog',   '#include <syslog.h>'],
]

foreach f : check_functions
  if cc.has_function(f.get(0), prefix : f.get(1))
    cdata.set('HAVE_'+f.get(0).to_upper(), 1)
  endif
endforeach

cdata.set('SIZEOF_INT', cc.sizeof('int'))
cdata.set('SIZEOF_LONG', cc.sizeof('long'))
cdata.set('SIZEOF_LONG_LONG', cc.sizeof('long long'))
cdata.set('SIZEOF_SHORT', cc.sizeof('short'))

cdata.set('PROJECT_NAME', meson.project_name())
cdata.set('VERSION', meson.project_version())
cdata.set('MESON_INSTALL_PREFIX', get_option('prefix'))

configure_file(input : 'config.h.meson',
  output : 'config.h',
  configuration : cdata)

if not (1 == cdata.get('HAVE_GETOPT_LONG'))
  fetchmail_sources = [fetchmail_sources, 'getopt.c', 'getopt1.c', 'getopt.h']
  message('adding local getopt.*')
endif
fetchmail_exe = executable('fetchmail',
  fetchmail_sources, lexer, parser,
  dependencies: link_deps,
  install: true)
#manpage = custom_target('manpage', output: 'fetchmail.1', input: 
#'fetchmail.man', command: ['cp', '-p', '@INPUT@', '@OUTPUT@'])
manpage = configure_file(input: 'fetchmail.man', output: 'fetchmail.1', configuration: cdata)
install_man(manpage)
