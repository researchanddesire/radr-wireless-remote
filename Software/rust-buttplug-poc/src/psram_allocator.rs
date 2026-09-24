use core::{
    alloc::{GlobalAlloc, Layout},
    cmp,
    ffi::c_void,
    ptr,
};

use esp_idf_svc::sys::{
    MALLOC_CAP_8BIT, MALLOC_CAP_SPIRAM, heap_caps_aligned_alloc, heap_caps_aligned_calloc,
    heap_caps_free,
};

/// Keeps Rust-owned application data in PSRAM so ESP-IDF's internal heap stays
/// available for NimBLE, FreeRTOS, and peripherals that require internal RAM.
struct PsramAllocator;

const RUST_HEAP_CAPS: u32 = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
const MINIMUM_ALIGNMENT: usize = core::mem::align_of::<usize>();

#[global_allocator]
static GLOBAL_ALLOCATOR: PsramAllocator = PsramAllocator;

unsafe impl GlobalAlloc for PsramAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        let alignment = cmp::max(layout.align(), MINIMUM_ALIGNMENT);
        // SAFETY: `layout` supplies a power-of-two alignment and ESP-IDF owns
        // the configured PSRAM heap before Rust's `main` is entered.
        unsafe { heap_caps_aligned_alloc(alignment, layout.size(), RUST_HEAP_CAPS) as *mut u8 }
    }

    unsafe fn alloc_zeroed(&self, layout: Layout) -> *mut u8 {
        let alignment = cmp::max(layout.align(), MINIMUM_ALIGNMENT);
        // SAFETY: Same allocation contract as `alloc`; one element of
        // `layout.size()` bytes avoids an intermediate multiplication.
        unsafe { heap_caps_aligned_calloc(alignment, 1, layout.size(), RUST_HEAP_CAPS) as *mut u8 }
    }

    unsafe fn dealloc(&self, allocation: *mut u8, _layout: Layout) {
        // SAFETY: Every pointer returned above comes from an ESP-IDF
        // capability-aware heap allocation routine.
        unsafe { heap_caps_free(allocation.cast::<c_void>()) };
    }

    unsafe fn realloc(&self, allocation: *mut u8, old_layout: Layout, new_size: usize) -> *mut u8 {
        let new_layout =
            // SAFETY: `GlobalAlloc::realloc` guarantees that `new_size` is
            // non-zero and valid for the existing alignment.
            unsafe { Layout::from_size_align_unchecked(new_size, old_layout.align()) };

        // Allocate-copy-free deliberately instead of using ESP-IDF's realloc.
        // This keeps every Rust allocation in one heap capability class.
        // SAFETY: `new_layout` obeys the allocator contract.
        let replacement = unsafe { self.alloc(new_layout) };
        if replacement.is_null() {
            return ptr::null_mut();
        }

        // SAFETY: Both allocations are valid and non-overlapping, and the
        // copied span cannot exceed either allocation.
        unsafe {
            ptr::copy_nonoverlapping(
                allocation,
                replacement,
                cmp::min(old_layout.size(), new_size),
            );
            self.dealloc(allocation, old_layout);
        }
        replacement
    }
}
