all:
	gcc -std=c99 -g netlink_printer.c -o netlink_printer_x86

arm64:
	aarch64-none-linux-gnu-gcc -std=c99 -g netlink_printer.c -o netlink_printer_arm64
	~/make_gnos_elf_crypt.sh ./netlink_printer_arm64
	cp ./netlink_printer_arm64_crypted /share/344272/aarch64-none-linux-gnu/tools/netlink_printer

mips:
	mips-rtl83xx-linux-gcc -std=c99 -g netlink_printer.c -o netlink_printer_mips
	~/make_ib_elf_crypt.sh ./netlink_printer_mips
	cp ./netlink_printer_mips_crypted /share/344272/mips-rtl83xx-linux/tools/netlink_printer

clean:
	rm -f netlink_printer_x86 netlink_printer_arm64 netlink_printer_mips netlink_printer_arm64_crypted netlink_printer_mips_crypted