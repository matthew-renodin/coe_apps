#include <lockwrapper/lockvspace.h>
#include <lockwrapper/helpers.h>

void * lockvspace_new_pages(vspace_t *vspace, seL4_CapRights_t rights, size_t num_pages, size_t size_bits) {
    LOCKVSPACE_CALL_RETURN(void *, vspace, new_pages, rights, num_pages, size_bits);
}

void * lockvspace_map_pages(vspace_t *vspace,
                          seL4_CPtr caps[], uintptr_t cookies[], seL4_CapRights_t rights,
                          size_t num_pages, size_t size_bits, int cacheable) {
    LOCKVSPACE_CALL_RETURN(void *, vspace, map_pages, caps, cookies, rights, num_pages, size_bits, cacheable);
}

int lockvspace_new_pages_at_vaddr(vspace_t *vspace, void *vaddr, size_t num_pages,
                                  size_t size_bits, reservation_t reservation, bool can_use_dev) {
    LOCKVSPACE_CALL_RETURN(int, vspace, new_pages_at_vaddr, vaddr, num_pages, size_bits, reservation, can_use_dev);
}

int lockvspace_map_pages_at_vaddr(vspace_t *vspace, seL4_CPtr caps[], uintptr_t cookies[],
                                  void *vaddr, size_t num_pages,
                                  size_t size_bits, reservation_t reservation) {
    LOCKVSPACE_CALL_RETURN(int, vspace, map_pages_at_vaddr, caps, cookies, vaddr, num_pages, size_bits, reservation);
}

void lockvspace_unmap_pages(vspace_t *vspace, void *vaddr, size_t num_pages,
                            size_t size_bits, vka_t *free) {
    LOCKVSPACE_CALL_VOID(vspace, unmap_pages, vaddr, num_pages, size_bits, free);
}

void lockvspace_tear_down(vspace_t *vspace, vka_t *free){
    LOCKVSPACE_CALL_VOID(vspace, tear_down, free);
}

reservation_t lockvspace_reserve_range_aligned(vspace_t *vspace, size_t bytes, size_t size_bits,
                                               seL4_CapRights_t rights, int cacheable, void **vaddr) {
    LOCKVSPACE_CALL_RETURN(reservation_t, vspace, reserve_range_aligned, bytes, size_bits, rights, cacheable, vaddr);
}

reservation_t lockvspace_reserve_range_at(vspace_t *vspace, void *vaddr,
                                          size_t bytes, seL4_CapRights_t rights, int cacheable) {
    LOCKVSPACE_CALL_RETURN(reservation_t, vspace, reserve_range_at, vaddr, bytes, rights, cacheable);
}

void lockvspace_free_reservation(vspace_t *vspace, reservation_t reservation){
    LOCKVSPACE_CALL_VOID(vspace, free_reservation, reservation);
}

void lockvspace_free_reservation_by_vaddr(vspace_t *vspace, void *vaddr){
    LOCKVSPACE_CALL_VOID(vspace, free_reservation_by_vaddr, vaddr);
}

seL4_CPtr lockvspace_get_cap(vspace_t *vspace, void *vaddr) {
    LOCKVSPACE_CALL_RETURN(seL4_CPtr, vspace, get_cap, vaddr);
}

uintptr_t lockvspace_get_cookie(vspace_t *vspace, void *vaddr) {
    LOCKVSPACE_CALL_RETURN(uintptr_t, vspace, get_cookie, vaddr);
}

void lockvspace_allocated_object(void *allocated_object_cookie, vka_object_t object) {
    lockvspace_t *lockvspace = (lockvspace_t *) allocated_object_cookie;
    lockvspace->lock.mutex_lock(&(lockvspace->lock));
    vspace_maybe_call_allocated_object(&(lockvspace->parent_vspace), object);
    lockvspace->lock.mutex_unlock(&(lockvspace->lock));
}

seL4_CPtr lockvspace_get_root(vspace_t *vspace) {
    LOCKVSPACE_CALL_RETURN_NOARG(seL4_CPtr, vspace, get_root);
}

int lockvspace_share_mem_at_vaddr(vspace_t *from, vspace_t *to, void *start, int num_pages, size_t size_bits, void *vaddr, reservation_t res) {
    LOCKVSPACE_CALL_RETURN(int, from, share_mem_at_vaddr, to, start, num_pages, size_bits, vaddr, res);
}

void lockvspace_make_vspace(vspace_t *out_vspace, lockvspace_t *lockvspace) {
    assert(out_vspace);
    assert(lockvspace);

    out_vspace->data = (void *) lockvspace->parent_vspace.data;
    out_vspace->new_pages = &lockvspace_new_pages;
    out_vspace->map_pages = &lockvspace_map_pages;
    out_vspace->new_pages_at_vaddr = &lockvspace_new_pages_at_vaddr;
    out_vspace->map_pages_at_vaddr = &lockvspace_map_pages_at_vaddr;
    out_vspace->unmap_pages = &lockvspace_unmap_pages;
    out_vspace->tear_down = &lockvspace_tear_down;
    out_vspace->reserve_range_aligned = &lockvspace_reserve_range_aligned;
    out_vspace->reserve_range_at = &lockvspace_reserve_range_at;
    out_vspace->free_reservation = &lockvspace_free_reservation;
    out_vspace->free_reservation_by_vaddr = &lockvspace_free_reservation_by_vaddr;
    out_vspace->get_cap = &lockvspace_get_cap;
    out_vspace->get_root = &lockvspace_get_root;
    out_vspace->get_cookie = &lockvspace_get_cookie;
    out_vspace->share_mem_at_vaddr = &lockvspace_share_mem_at_vaddr;
    out_vspace->allocated_object = &lockvspace_allocated_object;
    out_vspace->allocated_object_cookie = (void *) lockvspace;
    out_vspace->sync_data = (void *) lockvspace;
}

void lockvspace_attach(lockvspace_t *lockvspace, vspace_t parent_vspace, lock_interface_t lock) {
    lockvspace->parent_vspace = parent_vspace;
    lockvspace->lock = lock;
}

void lockvspace_set_allocated_object_cookie(lockvspace_t *lockvspace, void *new_allocated_object_cookie) {
    lockvspace->parent_vspace.allocated_object_cookie = new_allocated_object_cookie;
}
