## Makefile: ss (spreadsheet) #################################

CPP=        g++
SHELL=		/bin/sh
MOVE=		mv
COPY=		ln -s
RM=			rm -rf
TOUCH=		touch
LIBRARIAN=	ar rvu
INC=        -I../..

## Platform ###################################################

# get the OS
UNAME_S := $(shell uname -s | tr '[A-Z]' '[a-z]' )

# if this is linux
ifeq ($(UNAME_S),linux)
	ARCH := $(shell uname -m | tr '[A-Z]' '[a-z]' )
   	CPPFLAGS = -D _LINUX_  -g -Wno-deprecated
endif

#if this is OSX
ifeq ($(UNAME_S), darwin)
	ARCH := $(shell uname -m | tr '[A-Z]' '[a-z]' )
	CPPFLAGS = -D _OSX_ -g -Wno-deprecated
endif


## Developer mode (enables Claude AI) ########################

ifdef DEVELOPER
ifeq ($(UNAME_S), darwin)
    BUILD_CLAUDE=1
endif
ifeq ($(UNAME_S),linux)
    BUILD_CLAUDE=1
endif
endif

ifdef BUILD_CLAUDE
    CPPFLAGS += -DSS_CLAUDE_ENABLED
endif


## Object & Libraries #########################################


LIB_CX_PLATFORM_LIB_DIR=../../lib/$(UNAME_S)_$(ARCH)

APP_OBJECT_DIR=$(UNAME_S)_$(ARCH)

LIB_CX_BASE_NAME=libcx_base.a
LIB_CX_SCREEN_NAME=libcx_screen.a
LIB_CX_KEYBOARD_NAME=libcx_keyboard.a
LIB_CX_EDITBUFFER_NAME=libcx_editbuffer.a
LIB_CX_JSON_NAME=libcx_json.a
LIB_CX_COMPLETER_NAME=libcx_commandcompleter.a
LIB_CX_EXPRESSION_NAME=libcx_expression.a
LIB_CX_SHEETMODEL_NAME=libcx_sheetmodel.a

CX_LIBS = \
	$(LIB_CX_PLATFORM_LIB_DIR)/$(LIB_CX_KEYBOARD_NAME)   \
	$(LIB_CX_PLATFORM_LIB_DIR)/$(LIB_CX_SCREEN_NAME)     \
	$(LIB_CX_PLATFORM_LIB_DIR)/$(LIB_CX_EDITBUFFER_NAME) \
	$(LIB_CX_PLATFORM_LIB_DIR)/$(LIB_CX_COMPLETER_NAME)  \
	$(LIB_CX_PLATFORM_LIB_DIR)/$(LIB_CX_SHEETMODEL_NAME) \
	$(LIB_CX_PLATFORM_LIB_DIR)/$(LIB_CX_EXPRESSION_NAME) \
	$(LIB_CX_PLATFORM_LIB_DIR)/$(LIB_CX_JSON_NAME)

# Base library must come last - other libs depend on it
CX_LIBS_BASE = $(LIB_CX_PLATFORM_LIB_DIR)/$(LIB_CX_BASE_NAME)

OBJECTS = \
	$(APP_OBJECT_DIR)/Ss.o                   \
	$(APP_OBJECT_DIR)/SheetEditor.o          \
	$(APP_OBJECT_DIR)/SheetView.o            \
	$(APP_OBJECT_DIR)/CommandLineView.o      \
	$(APP_OBJECT_DIR)/MessageLineView.o      \
	$(APP_OBJECT_DIR)/CommandTable.o         \
	$(APP_OBJECT_DIR)/SpreadsheetDefaults.o  \
	$(APP_OBJECT_DIR)/HelpView.o

ifdef BUILD_CLAUDE
OBJECTS += \
	$(APP_OBJECT_DIR)/ClaudeHandler.o        \
	$(APP_OBJECT_DIR)/ClaudeView.o
CX_LIBS += $(LIB_CX_PLATFORM_LIB_DIR)/libcx_buildoutput.a
CX_LIBS += $(LIB_CX_PLATFORM_LIB_DIR)/libcx_process.a
endif

ALL_LIBS = $(CX_LIBS) $(CX_LIBS_BASE)

ALL_OBJECTS = $(OBJECTS)

## Targets ####################################################


ALL: MAKE_OBJ_DIR $(APP_OBJECT_DIR)/ss

MAKE_OBJ_DIR:
	test -d $(APP_OBJECT_DIR) || mkdir $(APP_OBJECT_DIR)

cleanupmac:
	$(RM) ._*

clean:
	$(RM) \
	$(APP_OBJECT_DIR)/ss    \
	$(APP_OBJECT_DIR)/*.o   \
	$(APP_OBJECT_DIR)/*.dbx \
	$(APP_OBJECT_DIR)/*.i   \
	$(APP_OBJECT_DIR)/*.ixx \
	$(APP_OBJECT_DIR)/core  \
	$(APP_OBJECT_DIR)/a.out \
	$(APP_OBJECT_DIR)/*.a

install:
ifeq ($(UNAME_S), linux)
	sudo cp $(APP_OBJECT_DIR)/ss /usr/local/bin/ss
	sudo chmod 755 /usr/local/bin/ss
endif
ifeq ($(UNAME_S), darwin)
	sudo cp $(APP_OBJECT_DIR)/ss /usr/local/bin/ss
	sudo chmod 755 /usr/local/bin/ss
	sudo xattr -cr /usr/local/bin/ss
endif

install-help:
ifeq ($(UNAME_S), linux)
	sudo mkdir -p /usr/local/share/ss
	sudo cp ss_help.md /usr/local/share/ss/ss_help.md
	sudo chmod 644 /usr/local/share/ss/ss_help.md
	sudo cp docs/CapTableExample.sheet /usr/local/share/ss/CapTableExample.sheet
	sudo chmod 644 /usr/local/share/ss/CapTableExample.sheet
endif
ifeq ($(UNAME_S), darwin)
	sudo mkdir -p /usr/local/share/ss
	sudo cp ss_help.md /usr/local/share/ss/ss_help.md
	sudo chmod 644 /usr/local/share/ss/ss_help.md
	sudo cp docs/CapTableExample.sheet /usr/local/share/ss/CapTableExample.sheet
	sudo chmod 644 /usr/local/share/ss/CapTableExample.sheet
endif

install-all: install install-help


$(APP_OBJECT_DIR)/ss: $(ALL_OBJECTS)
	$(CPP) -v $(CPPFLAGS) $(INC) $(ALL_OBJECTS) -o $(APP_OBJECT_DIR)/ss $(ALL_LIBS)
ifeq ($(UNAME_S), darwin)
	xattr -cr $(APP_OBJECT_DIR)/ss
endif


## Conversions ################################################

$(APP_OBJECT_DIR)/Ss.o 				: Ss.cpp
$(APP_OBJECT_DIR)/SheetEditor.o		: SheetEditor.cpp
$(APP_OBJECT_DIR)/SheetView.o		: SheetView.cpp
$(APP_OBJECT_DIR)/CommandLineView.o : CommandLineView.cpp
$(APP_OBJECT_DIR)/MessageLineView.o : MessageLineView.cpp
$(APP_OBJECT_DIR)/CommandTable.o	: CommandTable.cpp
$(APP_OBJECT_DIR)/SpreadsheetDefaults.o : SpreadsheetDefaults.cpp
$(APP_OBJECT_DIR)/HelpView.o : HelpView.cpp

ifdef BUILD_CLAUDE
$(APP_OBJECT_DIR)/ClaudeHandler.o : ClaudeHandler.cpp
$(APP_OBJECT_DIR)/ClaudeView.o : ClaudeView.cpp
endif

.PRECIOUS: $(CX_LIBS)
.SUFFIXES: .cpp .C .cc .cxx .o


$(OBJECTS):
	$(CPP) $(CPPFLAGS) $(INC) -c $? -o $@


########################################################################################
# Create tar archive for distribution
#
########################################################################################

archive:
	@echo "Creating ss_unix.tar..."
	@echo "  (extracts to cx_apps/ss/ when untarred from parent directory)"
	@test -d ../../ARCHIVE || mkdir ../../ARCHIVE
	@cd ../.. && tar cvf ARCHIVE/ss_unix.tar \
		--exclude='*.o' \
		--exclude='*.a' \
		--exclude='.git' \
		--exclude='.claude' \
		--exclude='.DS_Store' \
		--exclude='darwin_*' \
		--exclude='linux_*' \
		--exclude='sunos_*' \
		--exclude='irix_*' \
		--exclude='netbsd_*' \
		--exclude='nextstep_*' \
		--exclude='*.xcodeproj' \
		--exclude='*.xcworkspace' \
		--exclude='xcuserdata' \
		--exclude='DerivedData' \
		--exclude='*.pbxuser' \
		--exclude='*.mode1v3' \
		--exclude='*.mode2v3' \
		--exclude='*.perspectivev3' \
		--exclude='*.xcuserstate' \
		cx_apps/ss
	@echo "Archive created: ../../ARCHIVE/ss_unix.tar"

