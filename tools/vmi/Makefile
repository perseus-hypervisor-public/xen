TARGET := vmi

CFLAGS   := -Werror -Wall -Wstrict-prototypes -Wno-pointer-arith
CFLAGS   += -pipe -g -O2 -fPIC

CXXFLAGS := -Werror -Wall -Wno-pointer-arith -std=gnu++11
CXXFLAGS += -pipe -g -O2 -fPIC

include ../rules.mk

OBJS := main.o \
		error.o \
		option.o \
		session.o \
		fs.o \
		mainloop.o \
		eventfd.o

.PHONY: build
build: $(OBJS)
	$(CXX) $(LDFLAGS) -o $(TARGET) $^


.PHONY: clean
clean:
	rm -f $(TARGET)
	@find . \( -name '*.[oasd]' -o -name '*.tmp' -o -name '*.gcbo' -o -name '*.dtb' -o -name '*.dto' \) \
    -type f -print | xargs rm -f
