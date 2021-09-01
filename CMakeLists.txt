cmake_minimum_required(VERSION 3.19)
project(fetchmail)
INCLUDE (CheckIncludeFiles)
INCLUDE (CheckSymbolExists)
INCLUDE (FindOpenSSL)
INCLUDE (FindIntl)
find_package(FLEX  REQUIRED)
find_package(BISON REQUIRED)
find_package(OpenSSL 1.1.0)
find_library(RESOLV_LIBRARY resolv)

set(CMAKE_CXX_STANDARD 14)
execute_process(COMMAND ./getversion.sh 
	WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
	OUTPUT_VARIABLE VERSION 
	OUTPUT_STRIP_TRAILING_WHITESPACE)

set(fetchmail_SOURCES
        base64.c
        checkalias.c
        conf.c
        cram.c
        daemon.c
        driver.c
        env.c
        etrn.c
        fetchmail.c
        fetchmail.h
        fm_getaddrinfo.c
        fm_md5.h
        fm_realpath.c
        fm_strl.h
        getpass.c
        gettext.h
        gssapi.c
        i18n.h
        idle.c
        idlist.c
        imap.c
        interface.c
        kerberos.c
        kerberos.h
        lock.c
        lock.h
        md5c.c
        md5ify.c
        mx.h
        mxget.c
        netrc.c
        netrc.h
        ntlm.h
        ntlmsubr.c
        odmr.c
        opie.c
        options.c
        pop2.c
        pop3.c
        rcfile_l.c
        rcfile_y.c
        report.c
        rfc2047e.c
        rfc822.c
        rfc822valid.c
        rpa.c
        sdump.c
        sdump.h
        servport.c
        sink.c
        smb.h
        smbbyteorder.h
        smbdes.c
        smbdes.h
        smbencrypt.c
        smbencrypt.h
        smbmd4.c
        smbmd4.h
        smbtypes.h
        smbutil.c
        smtp.c
        smtp.h
        socket.c
        socket.h
        starttls.c
        stpcpy.c
        strlcat.c
        strlcpy.c
        tls-aux.c
        tls-aux.h
        transact.c
        tunable.h
        uid.c
        uid_db.c
        uid_db.h
        unmime.c
        x509_name_match.c
        xmalloc.c
        xmalloc.h
	ucs/norm_charmap.h
	ucs/norm_charmap.c)

flex_target(rcfile_l rcfile_l.l ${CMAKE_CURRENT_BINARY_DIR}/rcfile_l.c)
bison_target(rcfile_y rcfile_y.y ${CMAKE_CURRENT_BINARY_DIR}/rcfile_y.c
	DEFINES_FILE ${CMAKE_CURRENT_BINARY_DIR}/rcfile_y.h
	COMPILE_FLAGS "-d -t")
ADD_FLEX_BISON_DEPENDENCY(rcfile_l rcfile_y)

CHECK_INCLUDE_FILES(resolv.h HAVE_RESOLV_H)
CHECK_INCLUDE_FILES(arpa/nameser.h HAVE_ARPA_NAMESER_H)
CHECK_INCLUDE_FILES(libintl.h NLS_ENABLE)
check_symbol_exists(inet_aton arpa/inet.h HAVE_INET_ATON)
check_symbol_exists(TLS1_3_VERSION openssl/ssl.h HAVE_DECL_TLS1_3_VERSION)
check_symbol_exists(TLS_MAX_VERSION openssl/ssl.h HAVE_DECL_TLS_MAX_VERSION)
check_symbol_exists(res_search resolv.h HAVE_RES_SEARCH)
check_symbol_exists(getopt_long getopt.h HAVE_GETOPT_LONG)
check_symbol_exists(vsyslog syslog.h HAVE_VSYSLOG)

if(NOT HAVE_GETOPT_LONG)
	set(fetchmail_SOURCES ${fetchmail_SOURCES} getopt.c getopt1.c getopt.h)
endif()

add_executable(fetchmail ${fetchmail_SOURCES})
target_include_directories(fetchmail PUBLIC "${PROJECT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}")
target_link_libraries(fetchmail OpenSSL::SSL)

CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)
if(RESOLV_LIBRARY)
	target_link_libraries(fetchmail ${RESOLV_LIBRARY})
endif()
if(Intl_LIBRARY)
	target_link_libraries(fetchmail ${Intl_LIBRARY})
endif()
