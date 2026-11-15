TARGET   = desktop_builder
SRC_CC   = main.cc
SRC_BIN  = emblem.rgba wallpaper.rgb
INC_DIR += $(REP_DIR)/include $(PRG_DIR)/vendor/stb
LIBS     = base vfs libm
CC_CXX_WARN_STRICT =

vpath %.rgba $(REP_DIR)/src/app/hermos_shell
vpath %.rgb  $(REP_DIR)/src/app/hermos_shell
