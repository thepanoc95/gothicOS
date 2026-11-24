savedcmd_/home/milo/gothicOS/nullk/ports/linux/nkabi3.o := ld -m elf_x86_64 -z noexecstack --no-warn-rwx-segments   -r -o /home/milo/gothicOS/nullk/ports/linux/nkabi3.o @/home/milo/gothicOS/nullk/ports/linux/nkabi3.mod  ; ./tools/objtool/objtool --hacks=jump_label --hacks=noinstr --hacks=skylake --ibt --orc --retpoline --rethunk --static-call --uaccess --prefix=16  --link  --module /home/milo/gothicOS/nullk/ports/linux/nkabi3.o

/home/milo/gothicOS/nullk/ports/linux/nkabi3.o: $(wildcard ./tools/objtool/objtool)
