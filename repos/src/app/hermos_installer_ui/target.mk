TARGET   = hermos_installer_ui
SRC_CC   = main.cc
SRC_BIN  = default.tff
INC_DIR += $(REP_DIR)/include $(REP_DIR)/src/server/nitpicker
LIBS     = base

vpath %.tff $(REP_DIR)/src/app/hermos_shell
