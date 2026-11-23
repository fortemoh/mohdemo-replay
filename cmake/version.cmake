execute_process(
	COMMAND git rev-parse --is-inside-work-tree
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	OUTPUT_VARIABLE GIT_IS_INSIDE_WORK
	OUTPUT_STRIP_TRAILING_WHITESPACE
)

if ("${GIT_IS_INSIDE_WORK}" STREQUAL "true")
	message(VERBOSE "Git was successfully detected in the source directory. Relevant Git information will be included in the game version.")

	# Current branch
	execute_process(
		COMMAND git branch --show-current
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		OUTPUT_VARIABLE GIT_BRANCH_NAME
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)

	# Revision
	execute_process(
		COMMAND git show -s --format=%H HEAD
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		OUTPUT_VARIABLE GIT_REVISION_HASH
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)

	# Revision
	execute_process(
		COMMAND git show -s --format=%h HEAD
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		OUTPUT_VARIABLE GIT_REVISION_HASH_ABBREVIATED
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)

	# Commit date
	execute_process(
		COMMAND git show -s --format=%ct
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		OUTPUT_VARIABLE GIT_REVISION_DATE
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
else()
	message(WARNING "The repository was not detected as a Git working directory. Git information will not be available in the game version number.")
endif()

string(TIMESTAMP GIT_REVISION_DATE_TIME UTC)

set(GIT_REVISION_DATE ${GIT_REVISION_DATE_TIME})
string(TIMESTAMP GIT_REVISION_DATE "%b %d %Y UTC" UTC)

if(NOT GIT_REVISION_BUILD_NUMBER)
	set(GIT_REVISION_BUILD_NUMBER 0)
endif()

# A release build (CI's builds of a version tag): -DPRODUCT_RELEASE_TAG=v1.0.0, which must be v<PROJECT_VERSION>
# (cmake/identity.cmake). Its version has no stage: 1.0.0+61.c2f07c2, not 1.0.0-unstable+61.c2f07c2. The run and the
# commit stay, for the launcher, which tells by the commit whether an update is newer.
set(PRODUCT_RELEASE_TAG "" CACHE STRING "The version tag of a release build (v${PROJECT_VERSION}); empty for other builds")

if(PRODUCT_RELEASE_TAG)
	if(NOT PRODUCT_RELEASE_TAG STREQUAL "v${PROJECT_VERSION}")
		message(FATAL_ERROR "The release tag ${PRODUCT_RELEASE_TAG} isn't this version's, v${PROJECT_VERSION} (cmake/identity.cmake)")
	endif()
	set(PRODUCT_VERSION_STAGE "")
elseif(NOT PRODUCT_VERSION_STAGE)
	set(PRODUCT_VERSION_STAGE "unstable")
endif()

message(VERBOSE "Git branch: ${GIT_BRANCH_NAME}")
message(VERBOSE "Git revision hash: ${GIT_REVISION_HASH}")
message(VERBOSE "Git revision date: ${GIT_REVISION_DATE}")
message(VERBOSE "Git revision build: ${GIT_REVISION_BUILD_NUMBER}")
message(VERBOSE "Stage: ${PRODUCT_VERSION_STAGE}")

configure_file("cmake/q_version.generated.h.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/q_version.generated.h")

list(APPEND CLIENT_INCLUDE_DIRS "${CMAKE_CURRENT_BINARY_DIR}/generated")
list(APPEND SERVER_INCLUDE_DIRS "${CMAKE_CURRENT_BINARY_DIR}/generated")
list(APPEND RENDERER_INCLUDE_DIRS "${CMAKE_CURRENT_BINARY_DIR}/generated")
