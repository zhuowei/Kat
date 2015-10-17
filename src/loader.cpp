#include <vector>
#include <cstdio>
#include <elf.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "pageutils.h"
#include "vita-toolchain/sce-elf.h"

#define LOG(format, ...) fprintf(stderr, format "\n", ##__VA_ARGS__)

class SElfLibrary {
public:
	const char* path;
	int fd;
	Elf32_Ehdr* ehdr;
	Elf32_Phdr* phdrs;
	void** segments;
	sce_module_info_raw* self_info;
	sce_module_imports_raw* import_top;
	sce_module_imports_raw* import_end;
	bool load(const char* path);
	bool loadEhdrPhdr();
	bool mapSections();
	bool loadSelfHeader();
	void* mapping;
};

bool SElfLibrary::load(const char* path) {
	this->path = path;
	if (!loadEhdrPhdr()) return false;
	if (!mapSections()) return false;
	if (!loadSelfHeader()) return false;
	return true;
}

bool SElfLibrary::loadEhdrPhdr() {
	fd = open(path, O_RDONLY);
	if (fd == -1) {
		LOG("cannot open %s", path);
		return false;
	}
	ehdr = new Elf32_Ehdr;
	if (read(fd, ehdr, sizeof(Elf32_Ehdr)) != sizeof(Elf32_Ehdr)) {
		LOG("cannot read ehdr of %s", path);
		return false;
	}
	int phnum = ehdr->e_phnum;
	phdrs = new Elf32_Phdr[phnum];
	for (int i = 0; i < phnum; i++) {
		unsigned int off = ehdr->e_phoff + (i*ehdr->e_phentsize);
		lseek(fd, off, SEEK_SET);
		Elf32_Phdr* phdr = &phdrs[i];
		if (read(fd, phdr, sizeof(Elf32_Phdr)) != sizeof(Elf32_Phdr)) {
			LOG("cannot read phdr %d of %s", i, path);
			return false;
		}
	}
	return true;
}

bool SElfLibrary::mapSections() {
	// find out amount of memory needed to map in all the sections
	unsigned int minAddr = 0xffffffff;
	unsigned int maxAddr = 0;
	int phnum = ehdr->e_phnum;
	for (int i = 0; i < phnum; i++) {
		Elf32_Phdr& phdr = phdrs[i];
		if (phdr.p_type != PT_LOAD) continue;
		if (phdr.p_vaddr < minAddr) minAddr = phdr.p_vaddr;
		if (phdr.p_vaddr + phdr.p_memsz > maxAddr) maxAddr = phdr.p_vaddr + phdr.p_memsz;
	}
	unsigned int spaceRequired = PAGE_END(maxAddr - minAddr);
	mapping = mmap(NULL, spaceRequired, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED) {
		LOG("Failed to map memory of size %x", spaceRequired);
		return false;
	}
	segments = new void*[phnum]();
	uintptr_t mappingInt = (uintptr_t) mapping;
	for (int i = 0; i < phnum; i++) {
		Elf32_Phdr& phdr = phdrs[i];
		if (phdr.p_type != PT_LOAD) continue;
		uintptr_t startAddr = (phdr.p_vaddr - minAddr) + mappingInt;
		void* pm = mmap((void*) PAGE_START(startAddr),
			PAGE_END(phdr.p_memsz),
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_FIXED, fd, PAGE_START(phdr.p_offset));
		if (pm == MAP_FAILED) {
			LOG("Failed to map section %d", i);
			return false;
		}
		segments[i] = (void*) (startAddr + PAGE_OFFSET(phdr.p_offset));
		LOG("Mapped section %x at %p", i, segments[i]);
	}
	return true;
}

#define G(t, b) ((t) (((uintptr_t) segments[0]) + b));
#define GG(t, b) ((t) (((uintptr_t) segments[0]) + (b - phdrs[0].p_vaddr)));

bool SElfLibrary::loadSelfHeader() {
	self_info = (sce_module_info_raw*) (((uintptr_t) segments[0]) + ehdr->e_entry);
	LOG("library name: %p %p %s", mapping, self_info, self_info->name);
	import_top = G(sce_module_imports_raw*, self_info->import_top);
	import_end = G(sce_module_imports_raw*, self_info->import_end);
	
	for (sce_module_imports_raw* import = import_top; import < import_end; import++) {
		const char* name = GG(const char*, import->module_name);
		LOG("import name: %s", name);
	}
	return true;
};
#undef G
#undef GG

int main(int argc, char** argv) {
	SElfLibrary lib;
	bool success = lib.load(argv[1]);
	if (!success) {
		LOG("FAIL");
	}
	return 0;
}
