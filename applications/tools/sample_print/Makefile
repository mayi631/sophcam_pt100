include $(SRCTREE)/common_files/base.mk

MANAGER := $(SRCTREE)/applications/manager
TARGET := $(OUTPUT)/bin/sample_print
OBJECT := $(CURDIR)/obj/sample_print.$(TARGET_MACHINE).o
ARCHIVE := $(MANAGER)/lib/libapp_mng.a

.PHONY: all install clean clean_all
all: $(TARGET)

$(OBJECT): sample_print.c $(MANAGER)/include/printmng.h $(MANAGER)/include/motormng.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(MANAGER)/include -pthread -c $< -o $@

$(TARGET): $(OBJECT) $(ARCHIVE)
	@mkdir -p $(dir $@)
	$(CC) $(ELFFLAGS) -o $@ $^ -pthread

install: $(TARGET)
	install -D -m 755 $(TARGET) $(INSTALL_PATH)/bin/sample_print

clean:
	rm -f $(TARGET)
	rm -rf $(CURDIR)/obj

clean_all: clean

-include $(OBJECT:.o=.d)
