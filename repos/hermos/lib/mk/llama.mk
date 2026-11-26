#
# llama.cpp (ggml CPU backend) for Genode: pthreads and C++17 come from the libc and stdcxx
# libraries. The accelerator backends, the examples and the server are not built.
#
LLAMA_PORT_DIR := $(call select_from_ports,llama)/src/lib/llama
LLAMA_REP_DIR  := $(REP_DIR)/src/lib/llama

LIBS += libc libm stdcxx

# every file below keeps a unique base name; the five wrappers sit in LLAMA_REP_DIR
SRC_C  := w_ggml_c.c w_ggml_cpu_c.c w_x86_quants.c ggml-alloc.c ggml-quants.c quants.c
SRC_CC := w_x86_repack.cpp w_model_llama.cpp w_ggml_cpp.cpp w_ggml_cpu_cpp.cpp ggml-backend.cpp ggml-backend-reg.cpp \
          ggml-backend-meta.cpp ggml-opt.cpp ggml-threading.cpp gguf.cpp \
          repack.cpp traits.cpp binary-ops.cpp unary-ops.cpp vec.cpp ops.cpp hbm.cpp \
          cpu-feats.cpp \
          $(filter-out llama.cpp,$(notdir $(wildcard $(LLAMA_PORT_DIR)/src/*.cpp))) \
          $(notdir $(filter-out %/llama.cpp,$(wildcard $(LLAMA_PORT_DIR)/src/models/*.cpp)))

INC_DIR += $(LLAMA_REP_DIR) $(LLAMA_PORT_DIR)/ggml/src $(LLAMA_PORT_DIR)/ggml/src/ggml-cpu \
           $(LLAMA_PORT_DIR)/src $(LLAMA_PORT_DIR)/include $(LLAMA_PORT_DIR)/ggml/include

# the generic (non-SIMD) kernels run everywhere; SIMD is enabled per CPU later
CC_OPT += -D_GNU_SOURCE -D_XOPEN_SOURCE=600 -DGGML_USE_CPU -DGGML_SCHED_MAX_COPIES=4 -DNDEBUG
CC_C_OPT   += -std=gnu11
CC_CXX_OPT += -std=gnu++17 -fexceptions -frtti

CC_WARN =
CC_CXX_WARN_STRICT =

vpath w_%.c    $(LLAMA_REP_DIR)
vpath w_%.cpp  $(LLAMA_REP_DIR)
vpath %.c      $(LLAMA_PORT_DIR)/ggml/src $(LLAMA_PORT_DIR)/ggml/src/ggml-cpu
vpath %.cpp    $(LLAMA_PORT_DIR)/ggml/src $(LLAMA_PORT_DIR)/ggml/src/ggml-cpu $(LLAMA_PORT_DIR)/ggml/src/ggml-cpu/arch/x86 \
               $(LLAMA_PORT_DIR)/src $(LLAMA_PORT_DIR)/src/models

SHARED_LIB = yes
