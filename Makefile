.PHONY: help build build-arm64ec build-x86 build-all deploy launch run kill clean check-portable test win-test \
	mac-build mac-local mac-smoke mac-demo mac-restore render ui-smoke win11-arm win11-arm-deploy win11-arm-test win11-arm-smoke

help:
	@echo "foo_js_panel — make targets"
	@echo ""
	@echo "  build           cross-compile build/foo_js_panel.dll (build.sh, Windows x64)"
	@echo "  build-arm64ec   cross-compile build-arm64ec/foo_js_panel.dll (Windows on ARM, native ARM64EC)"
	@echo "  build-x86       cross-compile build-x86/foo_js_panel.dll (32-bit foobar2000)"
	@echo "  build-all       the three Windows builds + the macOS bundle"
	@echo "  deploy          build, then install into the local Wine foobar2000 (Linux)"
	@echo "  run             build, deploy, and (re)launch the Wine foobar2000"
	@echo "  launch          kill + relaunch the Wine foobar2000 without rebuilding"
	@echo "  kill            kill any running foobar2000.exe (Wine)"
	@echo "  clean           remove build/, build-arm64ec/, build-x86/ and build-mac/"
	@echo "  check-portable  compile src/core for a non-Windows host (no Windows headers)"
	@echo "  test            build + run the host-side unit tests (tests/, ASan/UBSan; Linux and macOS)"
	@echo "  win-test        the same unit tests built for Windows with clang-cl, run under wine (Linux)"
	@echo "  mac-build       build the macOS bundle (build-mac/foo_js_panel.component, universal)"
	@echo "  mac-local       mac-build, then install it into this Mac's foobar2000 and launch it"
	@echo "  mac-smoke       mac-build, then the smoke test in this Mac's foobar2000 (scripts/ui-test.sh mac smoke)"
	@echo "  mac-demo        mac-build here and in ../foo_ui_panels, install both in this Mac's foobar2000 with Panels UI's"
	@echo "                  wizard template TEMPLATE=albums-js (default), playlist-js or library-js as the active skin, launch (undo: mac-restore)"
	@echo "  mac-restore     undo mac-local / mac-demo: previous skin, default script and components back"
	@echo "  render          (macOS) build build/tools/render_script; SCRIPT=x.js OUT=x.png ARGS='--size 600x56 --playing ...' renders one frame"
	@echo ""
	@echo "  Windows 11 ARM VM on an Apple Silicon Mac (../foo_navidrome/scripts/win11-arm.sh, set up from foo_navidrome):"
	@echo "  win11-arm         start the VM and open its screen (VNC=0: no browser)"
	@echo "  win11-arm-deploy  build ARCH=arm64ec (default), x64 or x86, install the DLL in the guest, restart foobar2000, screenshot"
	@echo "  win11-arm-smoke   build ARCH=..., then the smoke test in the guest (scripts/ui-test.sh win11-arm smoke)"
	@echo "  win11-arm-test    unit tests built for Windows, run in the guest"
	@echo "  win11-arm-<cmd>   any win11-arm.sh command: -up, -down, -status, -shot, -ssh, -fb2k ARGS=start, -fb2k ARGS='log 60 ERROR', ..."

build:
	./build.sh

build-arm64ec:
	WIN_ARCH=arm64ec ./build.sh

build-x86:
	WIN_ARCH=x86 ./build.sh

build-all: build build-arm64ec build-x86 mac-build

deploy: build
	./scripts/deploy.sh

run: build
	./scripts/deploy.sh --launch

launch:
	./scripts/deploy.sh --launch

kill:
	pkill -f 'foobar2000[.]exe' 2>/dev/null || true

clean:
	rm -rf build build-arm64ec build-x86 build-mac

SDK_ROOT ?= $(shell ./scripts/toolchain.sh | sed -n 's/^SDK_ROOT=//p')
PANELS_UI_SRC ?= $(shell ./scripts/toolchain.sh | sed -n 's/^PANELS_UI_SRC=//p')
CXX_HOST ?= clang++
CC_HOST ?= clang
check-portable:
	@set -e; for f in src/core/*.cpp; do \
	  $(CXX_HOST) -std=c++20 -fsyntax-only -DNDEBUG -isystem $(SDK_ROOT) -isystem $(SDK_ROOT)/foobar2000 \
	    -Isrc -I$(PANELS_UI_SRC) -Ithird_party/quickjs $$f; \
	done; echo "portable: ok"

test:
	CXX_HOST=$(CXX_HOST) CC_HOST=$(CC_HOST) ./scripts/run-unit-tests.sh host

win-test:
	./scripts/run-unit-tests.sh win

mac-build:
	./scripts/mac-build.sh

mac-local: mac-build
	./scripts/mac-local.sh --launch

mac-smoke: mac-build
	./scripts/ui-test.sh mac smoke

PUI_ROOT = $(abspath $(PANELS_UI_SRC)/..)
TEMPLATES_BIN = build/tools/install_templates
$(TEMPLATES_BIN): $(PUI_ROOT)/tools/install_templates.cpp $(PANELS_UI_SRC)/core/skin_templates.cpp
	@mkdir -p build/tools
	$(CXX_HOST) -std=c++20 -O1 -I$(PANELS_UI_SRC) $(PUI_ROOT)/tools/install_templates.cpp \
	  $(addprefix $(PANELS_UI_SRC)/core/,skin_templates.cpp skin_config.cpp script_util.cpp i18n.cpp i18n_table.cpp) -o $(TEMPLATES_BIN)

TEMPLATE ?= albums-js
mac-demo: mac-build $(TEMPLATES_BIN)
	$(MAKE) -C $(PUI_ROOT) mac-build
	rm -rf build/demo-skins
	@dir="$$(./$(TEMPLATES_BIN) build/demo-skins | awk -F'\t' -v id="$(TEMPLATE)" '$$1 == id { print $$2 }')"; \
	  test -n "$$dir" || { echo "Panels UI has no $(TEMPLATE) template"; exit 1; }; \
	  ./scripts/mac-local.sh --panels-ui --skin "$$dir" --launch

mac-restore:
	./scripts/mac-local.sh --restore

RENDER_BIN = build/tools/render_script
QJS_C = third_party/quickjs/dtoa.c third_party/quickjs/libregexp.c third_party/quickjs/libunicode.c third_party/quickjs/quickjs.c
$(RENDER_BIN): tools/render_script.mm src/core/js_engine.cpp src/core/js_engine.h $(PANELS_UI_SRC)/platform/mac/mac_canvas.mm
	@mkdir -p build/tools/qjs
	@for f in $(QJS_C); do $(CC_HOST) -std=gnu11 -O2 -w -D_GNU_SOURCE -c $$f -o build/tools/qjs/$$(basename $$f .c).o; done
	$(CXX_HOST) -std=c++20 -O1 -fobjc-arc -Isrc -I$(PANELS_UI_SRC) -Ithird_party/quickjs tools/render_script.mm src/core/js_engine.cpp \
	  $(PANELS_UI_SRC)/platform/mac/mac_canvas.mm build/tools/qjs/*.o -framework CoreGraphics -framework CoreText -framework ImageIO \
	  -framework CoreFoundation -framework Foundation -o $(RENDER_BIN)
render: $(RENDER_BIN)
	@if [ -n "$(SCRIPT)" ]; then ./$(RENDER_BIN) $(SCRIPT) $(OUT) $(ARGS); fi

ui-smoke: mac-smoke

WIN11_ARM ?= $(abspath ../foo_navidrome/scripts/win11-arm.sh)
VNC ?= 1
ARCH ?= arm64ec
WIN11_ARM_DLL = $(if $(filter x64,$(ARCH)),build,build-$(ARCH))/foo_js_panel.dll
win11-arm-%: export FB2K_ARCH = $(if $(filter x86,$(ARCH)),x86,arm64ec)

win11-arm:
	$(WIN11_ARM) up
	@if [ "$(VNC)" != 0 ]; then $(WIN11_ARM) open; fi

win11-arm-deploy:
	WIN_ARCH=$(ARCH) ./build.sh
	$(WIN11_ARM) deploy $(WIN11_ARM_DLL)
	@sleep 8; $(WIN11_ARM) shot $(CURDIR)/build/ui-test/win11-arm-$(ARCH).png

win11-arm-smoke:
	WIN_ARCH=$(ARCH) ./build.sh
	WIN11_ARM=$(WIN11_ARM) ARCH=$(ARCH) ./scripts/ui-test.sh win11-arm smoke

win11-arm-test:
	WIN11_ARM=$(WIN11_ARM) ./scripts/run-unit-tests.sh win11-arm

win11-arm-%:
	COMPONENT=foo_js_panel $(WIN11_ARM) $* $(ARGS)
