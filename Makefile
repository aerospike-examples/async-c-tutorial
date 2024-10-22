###############################################################################
##  SETTINGS                                                                 ##
###############################################################################

OS = $(shell uname)
ARCH = $(shell uname -m)
PLATFORM = $(OS)-$(ARCH)

CFLAGS = -std=gnu99 -g -Wall -fPIC -O3
CFLAGS += -fno-common -fno-strict-aliasing
CFLAGS += -D_FILE_OFFSET_BITS=64 -D_REENTRANT -D_GNU_SOURCE
CFLAGS += -I/usr/local/include

LDFLAGS = /usr/local/lib/libaerospike.a 

ifeq ($(OS),Darwin)
  CFLAGS += -D_DARWIN_UNLIMITED_SELECT
  ifneq ($(wildcard /opt/homebrew/include),)
    # Mac new homebrew external include path
    CFLAGS += -I/opt/homebrew/include
  else ifneq ($(wildcard /usr/local/opt/libevent/include),)
    # Mac old homebrew libevent include path
    CFLAGS += -I/usr/local/opt/libevent/include
  endif

  ifneq ($(wildcard /opt/homebrew/opt/openssl/include),)
    # Mac new homebrew openssl include path
    CFLAGS += -I/opt/homebrew/opt/openssl/include
  else ifneq ($(wildcard /usr/local/opt/openssl/include),)
    # Mac old homebrew openssl include path
    CFLAGS += -I/usr/local/opt/openssl/include
  else ifneq ($(wildcard /opt/local/include/openssl),)
    # macports openssl include path
    CFLAGS += -I/opt/local/include
  endif

  ifneq ($(wildcard /opt/homebrew/lib),)
    # Mac new homebrew external lib path
    LDFLAGS += -L/opt/homebrew/lib
  else
    # Mac old homebrew external lib path
    LDFLAGS += -L/usr/local/lib

    ifeq ($(EVENT_LIB),libevent)
      LDFLAGS += -L/usr/local/opt/libevent/lib
    endif
  endif

  ifneq ($(wildcard /opt/homebrew/opt/openssl/lib),)
    # Mac new homebrew openssl lib path
    LDFLAGS += -L/opt/homebrew/opt/openssl/lib
  else
    # Mac old homebrew openssl lib path
    LDFLAGS += -L/usr/local/opt/openssl/lib
  endif
else ifeq ($(OS),FreeBSD)
  CFLAGS += -finline-functions
else
  CFLAGS += -finline-functions -rdynamic

  ifneq ($(wildcard /etc/alpine-release),)
    CFLAGS += -DAS_ALPINE
  endif
endif

ifeq ($(EVENT_LIB),libuv)
  CFLAGS += -DAS_USE_LIBUV
  LDFLAGS += -luv
  TARGETS = target/single_thread_libuv
else ifeq ($(EVENT_LIB),libevent)
  CFLAGS += -DAS_USE_LIBEVENT
  LDFLAGS += -levent_core -levent_pthreads
  TARGETS = target/single_thread_libevent
else
  CFLAGS += -DAS_USE_LIBEV
  LDFLAGS += -lev
  TARGETS = target/single_thread_libev
endif

LDFLAGS += -lssl -lcrypto -lpthread -lm -lz

ifneq ($(OS),Darwin)
  LDFLAGS += -lrt -ldl
endif

###############################################################################
##  OBJECTS                                                                  ##
###############################################################################

OBJECTS = async_tutorial.o

###############################################################################
##  MAIN TARGETS                                                             ##
###############################################################################

all: build

.PHONY: build
build: target/async_tutorial $(TARGETS)

.PHONY: clean
clean:
	@rm -rf target

target:
	mkdir $@

target/%.o: %.c | target
	cc $(CFLAGS) -o $@ -c $^

target/async_tutorial: $(addprefix target/,$(OBJECTS)) | target
	cc -o $@ $^ $(LDFLAGS)

