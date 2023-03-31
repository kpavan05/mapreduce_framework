CMAKE_MINIMUM_REQUIRED(VERSION 2.8.7 FATAL_ERROR)

INCLUDE(FindPackageHandleStandardArgs)
FIND_LIBRARY(CONSERVATOR_LIBRARY NAMES conservator-framework HINTS /usr/local/lib/)
FIND_PATH(CONSERVATOR_INCLUDE_DIR "conservator/Statable.h"
 "conservator/ExistsBuilderImpl.h"
 "conservator/Pathable.h"
 "conservator/GetACLBuilderImpl.h"
 "conservator/SetACLBuilderImpl.h"
 "conservator/GetDataBuilder.h"
 "conservator/CreateBuilder.h"
 "conservator/GetACLBuilder.h"
 "conservator/ExistsBuilder.h"
 "conservator/VectorPathableAndWatchable.h"
 "conservator/GetChildrenBuilder.h"
 "conservator/ConservatorFrameworkFactory.h"
 "conservator/ChildrenDeletable.h"
 "conservator/PathableAndWatchable.h"
 "conservator/GetChildrenBuilderImpl.h"
 "conservator/Flagable.h"
 "conservator/StatablePathable.h"
 "conservator/PathableAndWriteable.h"
 "conservator/SetDataBuilderImpl.h"
 "conservator/SetACLBuilder.h"
 "conservator/DeleteBuilder.h"
 "conservator/ACLVersionable.h"
 "conservator/DeleteBuilderImpl.h"
 "conservator/SetDataBuilder.h"
 "conservator/CreateBuilderImpl.h"
 "conservator/Versionable.h"
 "conservator/ConservatorFramework.h"
 "conservator/Watchable.h"
 "conservator/GetDataBuilderImpl.h"
 "conservator/VectorPathable.h"
 HINTS /usr/local/include/ 
)
message("Library ${CONSERVATOR_LIBRARY}")
SET(CONSERVATOR_LIBRARIES ${CONSERVATOR_LIBRARY})

FIND_PACKAGE_HANDLE_STANDARD_ARGS(Conservator REQUIRED_ARGS CONSERVATOR_INCLUDE_DIR CONSERVATOR_LIBRARIES)
