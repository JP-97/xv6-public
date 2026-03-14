import gdb

"""
This script dumps a graphical representation of the page dir / table.
"""

_KERNBASE = 0x80000000
_PHYSTOP = 0xE000000
_PHY_LOC_MASK = 0xffffffff & ~0xfff
_PRESENT_MASK = 0x001
_WRITABLE_MASK = 0x002
_USER_MASK = 0x004
_FLAGS_MASK = 0xfff

int32 = gdb.lookup_type("uint")

def print_page_dir_entry(dir_index: int, dir_virt_addr: int, dir_entry: gdb.Value) -> None:
    print(f"Page Dir Index: {dir_index}\n \
            Page Dir Virt Addr: {hex(dir_virt_addr)}\n \
            Dir Entry Flags: {hex(dir_entry & _FLAGS_MASK)}\n \
            Dir Entry Edit: {'R/W' if (dir_entry & _WRITABLE_MASK) else 'R'}\n \
            Dir Entry Owner: {'U' if (dir_entry & _USER_MASK) else 'S'}\n")


def print_page_table_entry(table_index: int, table_virt_addr: int, table_entry: gdb.Value) -> None:
    print(f"Page Table Index: {table_index}\n \
            Page Table Virt Addr: {hex(table_virt_addr)}\n \
            Page Entry Flags: {hex(table_entry & _FLAGS_MASK)}\n \
            Page Entry Edit: {'R/W' if (table_entry & _WRITABLE_MASK) else 'R'}\n \
            Page Entry Owner: {'U' if (table_entry & _USER_MASK) else 'S'}\n")


class dumpAll(gdb.Command):
    """Dump contents of every page dir and page table entry."""
    
    def __init__(self):
        super(dumpAll, self).__init__("dump-all", gdb.COMMAND_DATA) 

    def invoke(self, argument, from_tty):
        my_frame = gdb.selected_frame()
        int32 = gdb.lookup_type("uint")
        page_dir_phy_addr = hex(int(my_frame.read_register('cr3')))
        page_dir_virt_addr = int(page_dir_phy_addr, 16) + _KERNBASE

        page_dir_pointer = gdb.Value(page_dir_virt_addr).cast(int32.pointer())

        # Dump all PDEs
        for dir_index in range(2**10):
            page_dir_entry = page_dir_pointer[dir_index]
            
            if not (page_dir_entry & _PRESENT_MASK):
                continue
            
            print(hex(page_dir_entry & _PHY_LOC_MASK))
            page_table_virt_addr = (page_dir_entry & _PHY_LOC_MASK) + _KERNBASE

            print_page_dir_entry(dir_index,  page_dir_virt_addr, page_dir_entry)

            # Dump all PTEs from each page dir entry that's mapped
            page_table_ptr = gdb.Value(page_table_virt_addr).cast(int32.pointer())

            for table_index in range(2**10):
                page_table_entry = page_table_ptr[table_index]
                print(hex(page_table_entry))

                if not (page_table_entry & _PRESENT_MASK):
                    continue

                print_page_table_entry(table_index, page_table_ptr, page_table_entry)


class dumpForAddr(gdb.Command):
    """Dump contents of every page dir and page table entry."""
    
    def __init__(self):
        super(dumpForAddr, self).__init__("dump-addr", gdb.COMMAND_DATA) 

    def invoke(self, argument, from_tty):
        my_frame = gdb.selected_frame()
        page_dir_phy_addr = hex(int(my_frame.read_register('cr3')))
        page_dir_virt_addr = int(page_dir_phy_addr, 16) + _KERNBASE
        page_dir_pointer = gdb.Value(page_dir_virt_addr).cast(int32.pointer())

        addr_to_eval = int(argument, 16)

        if addr_to_eval < 0 or addr_to_eval > _KERNBASE + _PHYSTOP:
            print("Invalid memory address!")
            return
        
        page_dir_index = (addr_to_eval >> 22) & 0x3ff

        # Dump Page Dir entry for provided VA
        page_dir_entry = page_dir_pointer[page_dir_index]
            
        if not (page_dir_entry & _PRESENT_MASK):
            print("This addresses' page dir isn't mapped!")
            return
        
        print_page_dir_entry(page_dir_index, page_dir_virt_addr, page_dir_entry)
        
        # Dump PTE for provided VA
        page_table_virt_addr = (page_dir_entry & _PHY_LOC_MASK) + _KERNBASE
        page_table_ptr = gdb.Value(page_table_virt_addr).cast(int32.pointer())
        page_table_index = (addr_to_eval >> 12) & 0x3ff 

        page_table_entry = page_table_ptr[page_table_index]

        if not (page_table_entry & _PRESENT_MASK):
            print("This addresses' page table isn't mapped!")
            return

        print_page_table_entry(page_table_index, page_table_virt_addr, page_table_entry)


# Register commands
dumpAll()
dumpForAddr()