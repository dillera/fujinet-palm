# Builds every Palm app in the palmdev Docker image (see docker/Dockerfile).
APPS  = FujiConfig FujiNetTest
IMAGE = palmdev
DOCKER = docker run --rm --platform linux/amd64 -v $(CURDIR):/src -w /src $(IMAGE)

all: $(APPS:%=apps/%/icon.bmp)
	$(DOCKER) sh -c 'for app in $(APPS); do make -C apps/$$app || exit 1; done'

# Icons are drawn on the host; the toolchain image has no Python.
apps/%/icon.bmp: tools/make_icon.py
	python3 tools/make_icon.py $@

image:
	docker build --platform linux/amd64 -t $(IMAGE) docker

test:
	$(MAKE) -C tests

clean:
	$(DOCKER) sh -c 'for app in $(APPS); do make -C apps/$$app clean; done'
	rm -f $(APPS:%=apps/%/icon.bmp)
	$(MAKE) -C tests clean

.PHONY: all image test clean
