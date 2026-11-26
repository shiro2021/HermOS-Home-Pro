LLAMA_PORT_DIR := $(call select_from_ports,llama)/src/lib/llama
INC_DIR += $(LLAMA_PORT_DIR)/include $(LLAMA_PORT_DIR)/ggml/include
