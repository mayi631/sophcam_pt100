include $(SRCTREE)/common_files/base.mk
TOP_CONFIG_FILE ?=
#
include $(TOP_CONFIG_FILE)

all:
check:
	sh -n sample_motor
	sh -n tests/test_sample_motor.sh
	sh tests/test_sample_motor.sh
clean:
clean_all:
install:
	chmod 777 *.sh
	mkdir -p $(INSTALL_PATH)/bin/test_scripts
	cp *.sh $(INSTALL_PATH)/bin/test_scripts
	install -m 755 sample_motor $(INSTALL_PATH)/bin/test_scripts/sample_motor
