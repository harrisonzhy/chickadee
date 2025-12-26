#include "u-lib.hh"
#define ALLOC_SLOWDOWN 4

extern uint8_t end[];

uint8_t* heap_top;
uint8_t* stack_bottom;

uint8_t* allocs[200];

void _sys_free(int idx) {
    if (allocs[idx] != nullptr) {
        sys_free(allocs[idx]);
        heap_top = allocs[idx];
        allocs[idx] = nullptr;
    }
}

void process_main() {
    sys_consoletype(CONSOLE_MEMVIEWER);
    sys_map_console(console);
    for (int i = 0; i < CONSOLE_ROWS * CONSOLE_COLUMNS; ++i) {
        console[i] = '*' | 0x3000;
    }

    // Fork three new copies. (But ignore failures.)
    (void) sys_fork();
    (void) sys_fork();
    
    pid_t p = sys_getpid();
    srand(p);

    heap_top = reinterpret_cast<uint8_t*>(round_up(reinterpret_cast<uintptr_t>(end), PAGESIZE));
    stack_bottom = reinterpret_cast<uint8_t*>(round_down(rdrsp() - 1, PAGESIZE));

    while (true) {
        if (heap_top >= stack_bottom) {
            heap_top = reinterpret_cast<uint8_t*>(round_up(reinterpret_cast<uintptr_t>(end), PAGESIZE));
        }
        
        for (auto i = 0; i < 200; ++i) {
            _sys_free(i);
        }
        heap_top = reinterpret_cast<uint8_t*>(round_up(reinterpret_cast<uintptr_t>(end), PAGESIZE));
        
        auto const n = rand(0, 400);
        if (n >= 300) {
            _sys_free(5);
            assert(sys_npage_alloc(heap_top, PAGESIZE << 1) == 0);
            allocs[5] = heap_top;
            heap_top += PAGESIZE << 1;
        } else if (n >= 220) {
            if (n >= 240) {
                for (auto i = 0; i < 6 && heap_top < stack_bottom; ++i) {
                    assert(sys_npage_alloc(heap_top, PAGESIZE) == 0);
                    allocs[i] = heap_top;
                    heap_top += PAGESIZE;
                }
            } else {
                assert(sys_npage_alloc(heap_top, 1 << 17) == 0);
                allocs[0] = heap_top;
                heap_top += (1 << 17);
            }
        }
        
        if (n >= 200) {
            _sys_free(0);
        } else {
            _sys_free(n);
            assert(!allocs[n]);    
            size_t const size = (1 << rand(12, 18));
            if (sys_npage_alloc(heap_top, size) == 0) {
                // sys_free(heap_top);
                // allocs[n] = nullptr;
                allocs[n] = heap_top;
                heap_top += size;
            } else {
                allocs[n] = nullptr;
            }
        }

        _sys_free(0);
        if (sys_npage_alloc(heap_top, 1 << 17) == 0) {
            allocs[0] = heap_top;
            heap_top += (1 << 17);
        }
    }
}