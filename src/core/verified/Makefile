# ─── Configuration ────────────────────────────────────────────────────
PULSE_HOME ?= $(HOME)/pulse
KRML_HOME  ?= $(HOME)/karamel
FSTAR_EXE  ?= fstar.exe
KRML_EXE   ?= $(KRML_HOME)/_build/default/src/Karamel.native

KRML_INC   = -I $(KRML_HOME)/include -I $(KRML_HOME)/krmllib/dist/minimal
CFLAGS     = -O2 $(KRML_INC) -I . -I .. -include verified_support.h -w
LDFLAGS    = -static-libgcc

# ─── Source files ─────────────────────────────────────────────────────
C_SOURCES = bench_recv_buffer.c verified_recv_buffer.c krmlinit.c \
            krmlinit_rv.c verified_prims_support.c

PULSE_DIR  = pulse
PULSE_SRCS = $(wildcard $(PULSE_DIR)/*.fst $(PULSE_DIR)/*.fsti)

# Modules to extract (one fstar invocation each)
EXTRACT_MODULES = Pulse.Lib.Vector Pulse.Lib.RangeMap.Spec Pulse.Lib.RangeVec \
                  Pulse.Lib.CircularBuffer.Pow2 Pulse.Lib.CircularBuffer.Modular \
                  Pulse.Lib.CircularBuffer.GapTrack Pulse.Lib.CircularBuffer.Spec \
                  Pulse.Lib.CircularBuffer RecvBufferWrapper

# ─── Extraction dirs ─────────────────────────────────────────────────
KRML_OUTPUT = _krml_output
KRML_COUTPUT = _krml_c_output

# F* flags
FSTAR_CACHE  = $(PULSE_HOME)/build/lib.pulse.checked
FSTAR_FLAGS  = --cache_dir $(FSTAR_CACHE) \
               --include $(PULSE_HOME)/out/lib/pulse \
               --include $(PULSE_HOME)/build/lib.common.checked \
               --include $(PULSE_HOME)/lib/common \
               --include $(PULSE_HOME)/lib/pulse/lib \
               --warn_error -321 --warn_error -242 --warn_error -250 \
               --ext optimize_let_vc --ext fly_deps \
               --load_cmxs pulse --already_cached ',*' \
               --codegen krml --odir $(KRML_OUTPUT)

# krml flags
KRML_BUNDLE = RecvBufferWrapper=RecvBufferWrapper,Pulse.Lib.CircularBuffer,\
Pulse.Lib.CircularBuffer.Spec,Pulse.Lib.CircularBuffer.GapTrack,\
Pulse.Lib.CircularBuffer.Modular,Pulse.Lib.CircularBuffer.Pow2,\
Pulse.Lib.RangeVec,Pulse.Lib.RangeMap.Spec
KRML_FLAGS = -skip-compilation -skip-makefiles -skip-linking \
             -bundle '$(KRML_BUNDLE)' \
             -library Pulse.Lib.Vector \
             -warn-error -15 -warn-error -4 \
             -tmpdir $(KRML_COUTPUT)

# ─── Default: build benchmark from existing extracted C ───────────────
all: bench_recv_buffer

bench_recv_buffer: $(C_SOURCES) verified_support.h verified_recv_buffer.h
	$(CC) $(CFLAGS) $(C_SOURCES) -o $@ $(LDFLAGS)

# ─── Run benchmark ───────────────────────────────────────────────────
bench: bench_recv_buffer
	./bench_recv_buffer 200

# ─── Generate gnuplot charts ─────────────────────────────────────────
plot: bench_recv_buffer
	./plot.sh

# ─── Full extraction pipeline: Pulse → krml → C → copy to source ────
extract: extract-krml extract-c install-extracted
	@echo "Extraction complete. Run 'make' to build."

extract-krml: $(PULSE_SRCS)
	@mkdir -p $(KRML_OUTPUT)
	@echo "=== Extracting .krml from Pulse sources ==="
	@for mod in $(EXTRACT_MODULES); do \
		src=$$(echo $$mod | tr '.' '/'); \
		fst="$(PULSE_DIR)/$$mod.fst"; \
		if [ ! -f "$$fst" ]; then \
			fst="$(PULSE_HOME)/lib/pulse/lib/$$mod.fst"; \
		fi; \
		echo "  $$mod"; \
		$(FSTAR_EXE) $(FSTAR_FLAGS) --extract_module $$mod $$fst || exit 1; \
	done

extract-c: extract-krml
	@mkdir -p $(KRML_COUTPUT)
	@echo "=== Running krml to produce C ==="
	$(KRML_EXE) $(KRML_FLAGS) \
		$(KRML_HOME)/krmllib/.extract/*.krml $(KRML_OUTPUT)/*.krml

install-extracted: extract-c
	@echo "=== Installing extracted C into source tree ==="
	cp $(KRML_COUTPUT)/RecvBufferWrapper.c verified_recv_buffer.c
	cp $(KRML_COUTPUT)/RecvBufferWrapper.h verified_recv_buffer.h
	cp $(KRML_COUTPUT)/krmlinit.c krmlinit.c
	cp $(KRML_COUTPUT)/krmlinit.h krmlinit.h
	@mkdir -p internal
	cp $(KRML_COUTPUT)/internal/RecvBufferWrapper.h internal/RecvBufferWrapper.h
	@# Symlink for internal header references
	@ln -sf verified_recv_buffer.h RecvBufferWrapper.h
	@# Rename stdlib free collision
	sed -i 's/^static void free(/static void verified_free_cb(/g' verified_recv_buffer.c
	sed -i 's/  free(/  verified_free_cb(/g' verified_recv_buffer.c
	@echo "Done. Extracted files installed."

# ─── Clean ────────────────────────────────────────────────────────────
clean:
	rm -f bench_recv_buffer bench.dat sequential.png ooo.png

clean-extract:
	rm -rf $(KRML_OUTPUT) $(KRML_COUTPUT)

distclean: clean clean-extract

.PHONY: all bench plot extract extract-krml extract-c install-extracted \
        clean clean-extract distclean
