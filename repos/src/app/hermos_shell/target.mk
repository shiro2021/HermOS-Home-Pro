TARGET   = hermos_shell
SRC_CC   = main.cc
SRC_BIN  = emblem.rgba default.tff wallpaper.rgb
INC_DIR += $(REP_DIR)/include $(REP_DIR)/src/server/nitpicker
LIBS     = base

vpath %.tff $(REP_DIR)/src/server/nitpicker
