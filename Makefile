# Head-to-head speed test for FujiNet on the Palm (see README.md).
#
#   make            the three Palm apps, in the palmdev Docker image: palm/build/*.prc
#   make server     the target server on this Mac, port 8080
#   make report     the comparison of every run the server has, as text
#   make test       host tests: the server, and the suite run on this Mac
#   make install-iiic    queue FN H2H and FI H2H on the classic FujiNet (FUJINET) for HotSync
#   make install-visor   HotSync FI H2H RAM (the Springboard app, for RAM) to the Visor over USB
#   make dist            the testers' zip: build/h2h-testers-<commit>.zip (apps, NioLib, README.txt)
#   make server-release  the server with its version fixed: build/h2h-server-<commit>.zip
#
# H2H_TARGET is the target the apps start with (changeable on the Palm): the
# shared server on fujinet.online. The FujiNet, not the Palm, connects to it.
# For a run against make server on this Mac instead:
#   make H2H_TARGET=http://$(H2H_HOST):8080/h2h
IMAGE    ?= palmdev
REPOS    ?= $(abspath ../nio/fujinet-nio-workspace/repos)
CLASSIC  ?= $(abspath ../classic/fujinet-palm)
PALMSYNC ?= $(abspath ../shared/palm-sync)
PORT     ?= 8080
FUJINET  ?= 192.168.1.252
H2H_HOST ?= $(shell ipconfig getifaddr en1 2>/dev/null || ipconfig getifaddr en0 2>/dev/null || echo 192.168.1.2)
H2H_TARGET ?= http://fujinet.online:8199/h2h
DOCKER = docker run --rm --platform linux/amd64 -v $(CURDIR):/h2h -v $(REPOS):/repos \
         -v $(CLASSIC):/classic -w /h2h/palm $(IMAGE)
PRCS = palm/build/FN_H2H.prc palm/build/FI_H2H.prc palm/build/FI_H2H_SB.prc palm/build/FI_H2H_RAM.prc \
       palm/build/FI_H2H_NL.prc
NIOLIB = $(REPOS)/fujinet-nio-lib/build/palmos-shared-3.5

all: palm/icon.bmp niolib
	$(DOCKER) make H2H_TARGET='$(H2H_TARGET)'
	@ls -l $(PRCS)

# NioLib (fujinet-nio-lib's palmos-shared target) for Palm OS 3.5, in the
# palmdev-py image, without the USB transport (no Handspring SDK here).
niolib:
	docker run --rm --platform linux/amd64 -v $(REPOS):/repos -w /repos/fujinet-nio-lib palmdev-py \
	  make -s palmos-shared PALMOS_SDK_VERSION=3.5 >/dev/null

palm/icon.bmp: tools/make_icon.py
	python3 tools/make_icon.py $@

server:
	python3 server/h2h_server.py --port $(PORT) --data results

report:
	python3 server/h2h_server.py --data results --report

test:
	python3 -m unittest discover -s tests -p 'test_*.py' -v

# The Palm IIIc gets both serial apps (one for each firmware) through the
# classic FujiNet's HotSync queue: copy them over WebDAV, then press the
# cradle's HotSync button. The Visor gets the Springboard app over USB: run
# make install-visor, then press HotSync on its cradle.
install-iiic: $(PRCS)
	curl -fsS -T palm/build/FN_H2H.prc http://$(FUJINET)/dav/palm/install/
	curl -fsS -T palm/build/FI_H2H.prc http://$(FUJINET)/dav/palm/install/
	@echo "Queued on $(FUJINET): press HotSync on the IIIc's cradle."

# FI H2H RAM is the Springboard app under its own name, for RAM next to the
# ROM copy that flash-visor puts on the module.
install-visor: $(PRCS)
	cd $(PALMSYNC) && node dist/bin/cli.js push --usb $(abspath palm/build/FI_H2H_RAM.prc)

# Or put FI H2H SB in the Springboard module's own ROM (64 KB at most), next to
# springboard-nio's SB NIO, NIO Net and FI Config; its ISS Tracker and Weather
# make way. flash-visor builds the module firmware with that ROM, flashes it
# and restarts NIO (springboard-nio's host/flash.sh); then reset the Visor.
#
# The ROM holds SB NIO, FI H2H SB and FI H2H NL (57 of 64 KB); each copies
# itself to RAM with Options > Copy to RAM. FI H2H NL needs NioLib in RAM:
# for a Visor without it, put NioLib in the ROM instead of FI H2H SB (as an
# inert NLrm database, which FI H2H NL's Options > Install NioLib copies into
# RAM as the real libr/NiL1):
#   make flash-visor SB_ROM_APPS="$(SBNIO)/palm/build/sbnio.prc \
#     $(abspath palm/build/FI_H2H_NL.prc) $(abspath build/NioLib-rom.prc)"
SBNIO ?= $(abspath ../../springboard-nio)
SB_ROM_APPS ?= $(SBNIO)/palm/build/sbnio.prc $(abspath palm/build/FI_H2H_SB.prc) \
               $(abspath palm/build/FI_H2H_NL.prc)

build/NioLib-rom.prc: $(NIOLIB)/NioLib.prc tools/retype_prc.py
	@mkdir -p build
	python3 tools/retype_prc.py $< $@ --name "NioLib (ROM copy)" --type NLrm

build/h2h-sb.rom: palm/build/FI_H2H_SB.prc palm/build/FI_H2H_NL.prc build/NioLib-rom.prc
	@mkdir -p build
	docker run --rm --platform linux/amd64 -v $(SBNIO):$(SBNIO) -v $(CURDIR):$(CURDIR) sbnio-palm \
	  $(SBNIO)/palm/build/Palm-MakeROM -op create -hdr 0x08000000 -romBlock 0x08000000 0x10000 \
	  -chChecksum -chName "Springboard NIO" -chManuf "FujiNet" -chVersion 0x0100 \
	  -chZCrDate -romName "SBNIO 0.4 H2H" -autoSize -tokStr HsAT 330 \
	  $(patsubst %,-romDB %,$(SB_ROM_APPS)) -o $(abspath $@)
	@ls -l $@
	@test $$(wc -c < $@) -le 65536 || { echo "$@ is over the module's 64 KB" >&2; rm -f $@; exit 1; }

flash-visor: build/h2h-sb.rom
	SBNIO_ROM=$(abspath build/h2h-sb.rom) $(SBNIO)/host/flash.sh

# The testers' zip, named by this checkout's commit (-dirty with uncommitted
# changes, which a zip for testers should not have). Its README.txt is
# dist/README-testers.txt with the commits it was built from filled in.
DIST_PRCS = palm/build/FN_H2H.prc palm/build/FI_H2H.prc palm/build/FI_H2H_SB.prc \
            palm/build/FI_H2H_NL.prc $(NIOLIB)/NioLib.prc
describe = $$(git -C $(1) describe --always --dirty --abbrev=7)

dist: all
	@mkdir -p build
	@v=$(call describe,.); \
	python3 tools/make_dist.py build/h2h-testers-$$v.zip dist/README-testers.txt VERSION=$$v \
	  NIOLIB="$(call describe,$(REPOS)/fujinet-nio-lib) ($$(git -C $(REPOS)/fujinet-nio-lib branch --show-current))" \
	  CLASSIC=$(call describe,$(CLASSIC)) -- $(DIST_PRCS); \
	case $$v in *-dirty) echo "warning: built from uncommitted changes" >&2;; esac

# The server as one file whose version is fixed (python3 h2h_server.py
# --version shows it), for a host where it runs from a copy, not a checkout.
server-release:
	@mkdir -p build/server
	@v=$$(python3 server/h2h_server.py --version); rm -f build/server/h2h_server.py; \
	python3 server/h2h_server.py --stamp build/server/h2h_server.py && \
	rm -f build/h2h-server-$$v.zip && (cd build/server && zip -q -X ../h2h-server-$$v.zip h2h_server.py) && \
	echo "build/h2h-server-$$v.zip: h2h_server.py, version $$v"

clean:
	$(DOCKER) make clean
	rm -f palm/icon.bmp build/h2h-sb.rom

.PHONY: all niolib server report test install-iiic install-visor flash-visor dist server-release clean
