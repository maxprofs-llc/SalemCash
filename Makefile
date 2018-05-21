.PHONY: FORCE
all: FORCE
	$(MAKE) -C .. salemcash_qt test_salemcash_qt
clean: FORCE
	$(MAKE) -C .. salemcash_qt_clean test_salemcash_qt_clean
check: FORCE
	$(MAKE) -C .. test_salemcash_qt_check
salemcash-qt salemcash-qt.exe: FORCE
	 $(MAKE) -C .. salemcash_qt
