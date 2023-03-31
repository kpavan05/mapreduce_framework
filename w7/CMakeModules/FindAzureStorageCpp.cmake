# Find Azure Blob Storage Cpp  package
#
# Tries to find the Azure Blob Storage (C++ REST SDK) library
#

include(LibFindMacros)

# Include dir
find_path(AZURE_STORAGE_INCLUDE_DIR
  NAMES
    stdafx.h
    targetver.h
    was/auth.h
    was/blob.h
    was/common.h
    was/core.h
    was/error_code_strings.h
    was/queue.h
    was/retry_policies.h
    was/service_client.h
    was/storage_account.h
    was/table.h
    wascore/async_semaphore.h
    wascore/basic_types.h
    wascore/blobstreams.h
    wascore/constants.h
    wascore/executor.h
    wascore/hashing.h
    wascore/logging.h
    wascore/protocol.h
    wascore/protocol_json.h
    wascore/protocol_xml.h
    wascore/resources.h
    wascore/streambuf.h
    wascore/streams.h
    wascore/util.h
    wascore/xmlhelpers.h
    wascore/xmlstream.h
  PATHS 
    ${AZURE_STORAGE_PKGCONF_INCLUDE_DIRS}
    ${AZURE_STORAGE_DIR}
    $ENV{AZURE_STORAGE_DIR}
    /usr/local/include
    /usr/include
    ../../azure-storage-cpp
  PATH_SUFFIXES 
    Release/include
    include
)

# Library
find_library(AZURE_STORAGE_LIBRARY
  NAMES 
    azurestorage
  PATHS 
    ${AZURE_STORAGE_PKGCONF_LIBRARY_DIRS}
    ${AZURE_STORAGE_DIR}
    ${AZURE_STORAGE_DIR}
    $ENV{AZURE_STORAGE_DIR}
    /usr/local
    /usr
    ../../casablanca
  PATH_SUFFIXES
    lib
    Release/build.release/Binaries/
    build.release/Binaries/
)

set(AZURE_STORAGE_PROCESS_LIBS AZURE_STORAGE_LIBRARY)
set(AZURE_STORAGE_PROCESS_INCLUDES AZURE_STORAGE_INCLUDE_DIR)
libfind_process(AZURE_STORAGE)
