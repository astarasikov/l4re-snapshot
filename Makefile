

all: 
	@if [ -d obj ]; then                                           \
	  $(MAKE) build_all;                                           \
	else                                                           \
	  echo "Call 'make setup' once for initial setup." ;           \
	  exit 1;                                                      \
	fi

clean:
	@$(RM) -r obj

setup:
	@if [ -d obj ]; then                                                            \
	  echo "Snapshot already setup. Call make without 'setup' or with 'clean'.";    \
	else                                                                            \
	  export PATH=$$(pwd)/bin:$$PATH;                                               \
	  chmod +x ./bin/setup.d/*;                                                     \
	  for binary in bin/setup.d/*; do                                               \
	    ./$$binary config || exit 1;                                                \
	  done;                                                                         \
	  for binary in bin/setup.d/*; do                                               \
	    ./$$binary setup || exit 1;                                                 \
	  done;                                                                         \
	  echo ====================================================================;    \
	  echo ;                                                                        \
	  echo Your L4Re tree is set up now. Type 'make' to build the tree. This;       \
	  echo will take some time \(depending on the speed of your host system, of;    \
	  echo course\).;                                                               \
	  echo ;                                                                        \
	  echo Boot-images for ARM targets will be placed into obj/l4/arm-*/images.;    \
	  echo Check src/l4/Makeconf.local for path configuration during image builds.; \
	  echo ;                                                                        \
	  echo NOTE: You should add the bin directory to your path:;                    \
	  echo    export PATH=$$(pwd)/bin:\$$PATH;                                      \
	  echo ;                                                                        \
	fi

j_level := $(shell test -f /proc/cpuinfo \
                   && grep -qw "^processor" /proc/cpuinfo \
                   && grep -cw "^processor" /proc/cpuinfo)
j_level := $(if $(j_level),$(j_level),1)

build_all:
	@echo "=============== Building all Fiasco configurations ============"
	@export PATH=$$(pwd)/bin:$$PATH;                                       \
	for f in obj/fiasco/*; do                                              \
	  if [ -d "$$f" ]; then                                                \
	    echo " ============ Building in $$f ========= ";                   \
	    if ! $(MAKE) -C $$f -j$(j_level) V=0; then                         \
	      echo "Error building the Fiasco '$$f' variant.";                 \
	      echo "Press RETURN to continue with other variants.";            \
	      read ;                                                           \
	    fi                                                                 \
	  fi                                                                   \
	done
	@echo "=============== Building all L4Re   configurations ============"
	@export PATH=$$(pwd)/bin:$$PATH;                                       \
	for f in obj/l4/*; do                                                  \
	  if [ -d "$$f" ]; then                                                \
	    echo " ============ Building in $$f ========= ";                   \
	    if ! $(MAKE) -C $$f -j$(j_level) V=0; then                         \
	      echo "Error building the L4Re '$$f' variant.";                   \
	      echo "Press RETURN to continue with other variants.";            \
	      read ;                                                           \
	    fi                                                                 \
	  fi                                                                   \
	done
	@echo "=============== Building all L4Linux configurations ==========="
	@export PATH=$$(pwd)/bin:$$PATH;                                       \
	[ -e obj/.config ] && . obj/.config;                                   \
	for f in obj/l4linux/*; do                                             \
	  if [ -d "$$f" ]; then                                                \
	    echo " ============ Building in $$f ========= ";                   \
	    tmp=$${f##*/};                                                     \
	    build_for_arch=x86;                                                \
	    x_prefix=$(CROSS_COMPILE);                                         \
	    [ "$${tmp#arm}" != "$$tmp" ] && build_for_arch=arm;                \
	    [ "$$build_for_arch" = "arm" -a -n "$$SKIP_L4LINUX_ARM_BUILD" ]    \
	       && continue;                                                    \
	    [ "$${tmp#arm}" != "$$tmp" ] && x_prefix=arm-linux-;               \
	    if ! $(MAKE) L4ARCH=$$build_for_arch CROSS_COMPILE=$$x_prefix -C $$f -j$(j_level); then \
	      echo "Error building the L4Linux '$$f' variant.";                \
	      echo "Press RETURN to continue with other variants.";            \
	      read ;                                                           \
	    fi                                                                 \
	  fi                                                                   \
	done
	@echo "=============== Building Image ================================"
	@export PATH=$$(pwd)/bin:$$PATH;                                       \
	[ -e obj/.config ] && . obj/.config;                                   \
	for d in obj/l4/*; do                                                  \
	  if [ -d "$$d" -a $${d#obj/l4/arm-} != "$$d" ]; then                  \
	    $(MAKE) -C $$d image E=hello ;                                     \
	    $(MAKE) -C $$d image E=hello-shared ;                              \
	    [ -n "$$SKIP_L4LINUX_ARM_BUILD" ]                                  \
	      || $(MAKE) -C $$d image E="L4Linux ARM" ;                        \
	    grep -vq lobahello src/l4/conf/modules.list                        \
	      || $(MAKE) -C $$d image E=lobahello ;                            \
	  fi                                                                   \
	done	
	@echo "=============== Build done ===================================="

.PHONY: setup all build_all clean
