# Minimal Makefile for ISO bootable kernel with GRUB

all: amxos.iso

build:
	mkdir -p build

isodir:
	mkdir -p isodir/boot/grub

build/boot.o: src/boot.asm | build
	nasm -f elf32 src/boot.asm -o build/boot.o

build/kernel.o: src/kernel.c | build
	i686-elf-gcc -m32 -ffreestanding -g -nostdlib -fno-pie -Wall -Wextra -c src/kernel.c -o build/kernel.o

build/kernel.elf: build/boot.o build/kernel.o linker.ld
	i686-elf-ld -T linker.ld -o build/kernel.elf build/boot.o build/kernel.o

isodir/boot/kernel.elf: build/kernel.elf | isodir
	cp build/kernel.elf isodir/boot/kernel.elf

isodir/boot/grub/grub.cfg: | isodir
	echo 'menuentry "AMXOS" { multiboot /boot/kernel.elf; }' > isodir/boot/grub/grub.cfg

amxos.iso: isodir/boot/kernel.elf isodir/boot/grub/grub.cfg
	grub-mkrescue -o amxos.iso isodir

clean:
	rm -rf build isodir amxos.iso
