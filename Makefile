# Builds every Palm app in the palmdev Docker image (see docker/Dockerfile).
APPS  = FujiConfig FujiNetTest BounceWorld FujiLobby Holdem FujiPet
IMAGE = palmdev
# fujinet-nio-lib, for the apps that talk to FujiNet NIO (FI Test and the
# others). Its palmos-serial target is built first.
NIO_LIB ?= $(abspath ../fujinet-nio-workspace/repos/fujinet-nio-lib)
DOCKER = docker run --rm --platform linux/amd64 -v $(CURDIR):/src -v $(NIO_LIB):/nio-lib -w /src $(IMAGE)

all: $(APPS:%=apps/%/icon.bmp)
	$(DOCKER) sh -c 'make -C /nio-lib palmos-serial && for app in $(APPS); do make -C apps/$$app NIO_LIB=/nio-lib || exit 1; done'

# Icons are drawn on the host; the toolchain image has no Python.
apps/%/icon.bmp: tools/make_icon.py
	python3 tools/make_icon.py $@

# FI FujiPet draws its own icon, with its sprites.
apps/FujiPet/icon.bmp apps/FujiPet/petart.h: tools/make_pet_art.py
	python3 tools/make_pet_art.py

image:
	docker build --platform linux/amd64 -t $(IMAGE) docker

test:
	$(MAKE) -C tests

# Palm OS calls newer than 3.1 in each built app (see tools/palm_traps.py).
traps: all
	@mkdir -p build
	@$(DOCKER) sh -c 'cat /opt/palmdev/sdk-3.5/include/Core/CoreTraps.h > build/CoreTraps.h; \
		for app in $(APPS); do m68k-palmos-objdump -d apps/$$app/$$app > build/$$app.dis; done'
	@python3 tools/palm_traps.py build/CoreTraps.h $(APPS:%=build/%.dis)

clean:
	$(DOCKER) sh -c 'for app in $(APPS); do make -C apps/$$app clean; done'
	rm -f $(APPS:%=apps/%/icon.bmp)
	$(MAKE) -C tests clean
	rm -rf build

.PHONY: all image test traps clean
