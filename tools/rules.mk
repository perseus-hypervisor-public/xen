# Get gcc to generate the dependencies for us.
depflags = -MMD -MF .$(@F).d
cflags   = $(CFLAGS)   $(depflags)
cxxflags = $(CXXFLAGS) $(depflags)

DEPS = .*.d

%.o: %.c
	$(CC) $(cflags) -c -o $@ $< $(APPEND_CFLAGS)

%.o: %.cxx
	$(CXX) $(cxxflags) -c -o $@ $< $(APPEND_CFLAGS)

%.opic: %.c
	$(CC) -DPIC $(cflags) -c -o $@ $< $(APPEND_CFLAGS)

%.opic: %.cxx
	$(CXX) -DPIC $(cxxflags) -c -o $@ $< $(APPEND_CFLAGS)
