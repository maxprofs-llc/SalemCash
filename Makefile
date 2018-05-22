all:
	$(MAKE) -C .. salemcash_test
clean:
	$(MAKE) -C .. salemcash_test_clean
check:
	$(MAKE) -C .. salemcash_test_check
