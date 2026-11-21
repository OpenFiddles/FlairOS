PROFILE ?= debug

.PHONY: all linux-image aarch64-image test-airp clean
all: linux-image

linux-image:
	bash build/build-linux-image.sh $(PROFILE)

aarch64-image:
	bash build/build-aarch64-image.sh $(PROFILE)

test-airp:
	sh tests/airp-package-test.sh

clean:
	rm -rf build-out build-out-aarch64
