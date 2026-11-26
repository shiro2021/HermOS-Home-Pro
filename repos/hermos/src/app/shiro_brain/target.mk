TARGET = shiro_brain
LIBS   = base libc stdcxx libm llama
SRC_CC = main.cc

# libc's <sys/types.h> triggers -Wconversion in strict mode; same as the other libc components
CC_CXX_WARN_STRICT =
