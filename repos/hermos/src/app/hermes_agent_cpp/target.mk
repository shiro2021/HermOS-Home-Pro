TARGET = hermes_agent_cpp
LIBS   = base libc stdcxx
SRC_CC = main.cc

# libc's <sys/types.h> (pulled in by <unistd.h>/<string>) triggers
# -Wconversion on the __major/__minor helpers; disable strict warnings like gcov.
CC_CXX_WARN_STRICT =
