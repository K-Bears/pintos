#ifndef VM_UNINIT_H
#define VM_UNINIT_H
#include "vm/vm.h"

struct page;
enum vm_type;

typedef bool vm_initializer(struct page *, void *aux);

/*
 * 초기화되지 않은 페이지.
 * "지연 로딩(Lazy loading)"을 구현하기 위한 타입입니다.
 *  이 페이지에 대한 흐름에 관해 세세하게 다 적어오기
 */
struct uninit_page {
    /* 페이지의 내용을 초기화하는 함수 포인터입니다.*/
    vm_initializer *init;
    enum vm_type type;
    /* 초기화 함수에 전달할 보조(auxiliary) 데이터입니다. */
    void *aux;
    /* struct page를 초기화하고,
     * 물리 주소(pa)를 가상 주소(va)에 매핑합니다. */
    bool (*page_initializer)(struct page *, enum vm_type, void *kva);
};
/*
 * 새로운 uninit 페이지를 설정합니다.
 * 이 함수는 lazy loading을 위해 페이지를 초기화하지 않고 등록할 때 사용됩니다.
 */
void uninit_new(struct page *page, void *va, vm_initializer *init, enum vm_type type, void *aux,
                bool (*initializer)(struct page *, enum vm_type, void *kva));
#endif
