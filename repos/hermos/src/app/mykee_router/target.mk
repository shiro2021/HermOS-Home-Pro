TARGET = mykee_router
LIBS   = base libc stdcxx curl
SRC_CC = main.cc

# vendored single-header nlohmann/json (json.hpp)
INC_DIR += $(PRG_DIR)/vendor

# libc's <sys/types.h> (pulled in by <unistd.h>/<string>) triggers
# -Wconversion on the __major/__minor helpers; disable strict warnings like gcov.
CC_CXX_WARN_STRICT =
