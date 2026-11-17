CC = /usr/bin/clang
CFLAGS = -O2 -Wall -Wextra -Wno-unused-parameter

# Windows ARM64 cross compiler for the test PE files
PECC = PATH=/opt/homebrew/opt/lld/bin:$$PATH /opt/homebrew/opt/llvm/bin/clang --target=aarch64-w64-mingw32 \
       -O2 -ffreestanding -fno-builtin -fno-stack-protector -nostdlib -fuse-ld=lld

all: hvproto pe/ntdll.dll pe/hello.exe

hvproto: hvproto.c guest.S entitlements.plist
	$(CC) $(CFLAGS) -o $@ hvproto.c guest.S -framework Hypervisor -lpthread
	codesign --force --sign - --entitlements entitlements.plist $@

# ntdll.dll prefers a low (Windows-style) base, hello.exe keeps the default
# 0x140000000, which collides with nothing we own and must be relocated.
pe/ntdll.dll pe/libntdll.a: pe/ntdll.c
	$(PECC) -shared -o pe/ntdll.dll $< -Wl,--image-base,0x10000000 \
	    -Wl,-e,DllMainCRTStartup -Wl,--out-implib,pe/libntdll.a

pe/hello.exe: pe/hello.c pe/libntdll.a
	$(PECC) -o $@ $< -Lpe -lntdll -Wl,-e,mainCRTStartup -Wl,--dynamicbase

# phase 5 step 4: page types of memory shared with Metal; no VM, no hv_vm_map
gpumem: gpumem.m
	$(CC) $(CFLAGS) -fobjc-arc -o $@ gpumem.m -framework Foundation -framework Metal -framework IOSurface

# phase 5 step 4 point 3: maps Metal memory into a VM. CAN PANIC THE MAC:
# one case per run, only with the user's OK (see gpumap.m)
gpumap: gpumap.m entitlements.plist
	$(CC) $(CFLAGS) -fobjc-arc -o $@ gpumap.m -framework Foundation -framework Metal -framework IOSurface -framework Hypervisor
	codesign --force --sign - --entitlements entitlements.plist $@

run: all
	./hvproto
	./hvproto pe/hello.exe

clean:
	rm -f hvproto gpumem gpumap pe/*.dll pe/*.exe pe/*.a

.PHONY: all run clean
