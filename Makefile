PROFILE ?= debug

.PHONY: all linux-image test-airp clean
all: linux-image

linux-image:
	bash build/build-linux-image.sh $(PROFILE)

test-airp:
	sh tests/airp-package-test.sh

clean:
	rm -rf build-out
