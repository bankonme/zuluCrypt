cmake_minimum_required(VERSION 2.6)
cmake_policy(SET CMP0017 OLD)

set( SRC
	../dialogmsg.cpp
	../userfont.cpp
	../tablewidget.cpp
	../utility.cpp
	../readonlywarning.cpp
)

set( MOC_FILES
	../dialogmsg.h
	../readonlywarning.h
)

set( UI_FILES ../dialogmsg.ui ../readonlywarning.ui )

add_definitions( -D_FILE_OFFSET_BITS=64 -Wextra -Wall -pedantic -std=c++11 -I${PROJECT_BINARY_DIR}/zuluCrypt-gui/sharedObjects -I${PROJECT_BINARY_DIR})

if( QT5 )
	Qt5_WRAP_UI( UI ${UI_FILES} )
	Qt5_WRAP_CPP( MOC ${MOC_FILES} )
else()
	INCLUDE( ${QT_USE_FILE} )
	Qt4_WRAP_UI( UI ${UI_FILES} )
	Qt4_WRAP_CPP( MOC ${MOC_FILES} )
endif()

add_library( sharedObject STATIC ${MOC} ${SRC} ${UI_FILES} )

if( BUILDKWALLET )
	target_link_libraries( sharedObject lxqtwallet ${kwallet_library} -lblkid )
else()
	target_link_libraries( sharedObject lxqtwallet -lblkid Process )
endif()

set_target_properties( sharedObject PROPERTIES COMPILE_FLAGS "-D_FILE_OFFSET_BITS=64 -Wall -s -fPIC -pthread -pedantic" )
set_target_properties( sharedObject PROPERTIES LINK_FLAGS "-pie" )
