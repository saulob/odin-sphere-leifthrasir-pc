/* Windows guest memory: sparse backing, on-demand commitment, aliases, protection and release.
 * Includes the implementation so commitment can be inspected and failed on demand. */
#include "runtime.h"
#include <stdio.h>
#include <string.h>
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *name) {
    for (size_t i=0;i<count;++i) if (!strcmp(table[i].name,name)) return (uintptr_t)table[i].function;
    return 0;
}
void runtime_log_flush(void) {}
int32_t *runtime_errno(void) { static int32_t value; return &value; }
#include <windows.h>
#include <psapi.h>
static int fail_section_creation, fail_commit;
static LPVOID WINAPI test_virtual_alloc(LPVOID address, SIZE_T size, DWORD type, DWORD protection) {
    if (fail_commit && (type & MEM_COMMIT)) { SetLastError(ERROR_COMMITMENT_LIMIT); return NULL; }
    return VirtualAlloc(address,size,type,protection);
}
static HANDLE WINAPI test_create_section(HANDLE file, SECURITY_ATTRIBUTES *security, ULONG access, ULONG protection,
                                         ULONG attributes, ULONG64 size, PCWSTR name,
                                         MEM_EXTENDED_PARAMETER *parameters, ULONG count) {
    if (fail_section_creation) { SetLastError(ERROR_COMMITMENT_LIMIT); return NULL; }
    return CreateFileMapping2(file,security,access,protection,attributes,size,name,parameters,count);
}
#define CreateFileMapping2 test_create_section
#define VirtualAlloc test_virtual_alloc
#include "../src/runtime_memory.c"
#undef CreateFileMapping2
#undef VirtualAlloc
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; printf("FAIL %d: %s (WinError %lu)\n",__LINE__,#x,GetLastError()); } } while(0)
static uint64_t system_commit(void) {
    PERFORMANCE_INFORMATION info;
    memset(&info,0,sizeof(info)); info.cb=sizeof(info);
    K32GetPerformanceInfo(&info,sizeof(info));
    return (uint64_t)info.CommitTotal*info.PageSize;
}
static DWORD state_at(const void *address) {
    MEMORY_BASIC_INFORMATION info;
    return VirtualQuery(address,&info,sizeof(info)) ? info.State : 0;
}
static DWORD protect_at(const void *address) {
    MEMORY_BASIC_INFORMATION info;
    return VirtualQuery(address,&info,sizeof(info)) ? info.Protect : 0;
}
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);
    fail_section_creation=1;
    CHECK(runtime_memory_prepare()!=0);
    CHECK(backing_base==NULL && section==NULL);
    fail_section_creation=0;
    const uint64_t commit_before_pool=system_commit();
    fail_commit=1;
    CHECK(runtime_memory_prepare()!=0);
    fail_commit=0;
    CHECK(backing_base!=NULL);
    if (!backing_base) return 1;
    CHECK(system_commit()-commit_before_pool<(POOL_SIZE+FLEX_SPAN)/4);
    CHECK(state_at(backing_base)==MEM_RESERVE);
    CHECK(runtime_memory_prepare()==0);
    CHECK(state_at(backing_base)==MEM_COMMIT);
    CHECK(state_at(backing_base+PAGE)==MEM_RESERVE);
    CHECK(state_at(backing_base+POOL_SIZE+FLEX_SPAN-PAGE)==MEM_RESERVE);

    void *at=NULL;
    CHECK(map_flexible(&at,4*PAGE,3,0)==0);
    if (!at) return 1;
    unsigned char *code=at;
    code[0]=0xb8; code[1]=42; code[2]=0; code[3]=0; code[4]=0; code[5]=0xc3;
    CHECK(kernel_mprotect(at,4*PAGE,7)==0);
    CHECK(((int (*)(void))at)()==42);
    CHECK(kernel_mprotect(at,4*PAGE,1)==0);
    CHECK(protect_at(at)==PAGE_READONLY);
    CHECK(kernel_mprotect(at,4*PAGE,5)==0);
    CHECK(protect_at(at)==PAGE_EXECUTE_READ);
    CHECK(kernel_mprotect(at,4*PAGE,7)==0);
    CHECK(protect_at(at)==PAGE_EXECUTE_READWRITE);
    CHECK(kernel_mprotect(at,4*PAGE,3)==0);
    CHECK(protect_at(at)==PAGE_READWRITE);
    runtime_memory_gpu_protect((uintptr_t)at,4*PAGE,0,0);
    CHECK(protect_at(at)==PAGE_NOACCESS);
    runtime_memory_gpu_protect((uintptr_t)at,4*PAGE,1,1);
    CHECK(protect_at(at)==PAGE_READWRITE);
    CHECK(kernel_mprotect(at,4*PAGE,1)==0);
    const unsigned char marker=17;
    CHECK(runtime_memory_write_backing((uintptr_t)at,&marker,1));
    CHECK(*(unsigned char *)at==marker);
    CHECK(direct_unmap(at,4*PAGE)==0);
    CHECK(state_at(at)==MEM_RESERVE);
    at=NULL; CHECK(map_flexible(&at,4*PAGE,3,0)==0); CHECK(*(unsigned char *)at==0); CHECK(direct_unmap(at,4*PAGE)==0);

    void *executable=NULL;
    CHECK(map_flexible(&executable,PAGE,7,0)==0);
    if (executable) {
        *(unsigned char *)executable=0xc3;
        CHECK(protect_at(executable)==PAGE_EXECUTE_READWRITE);
        CHECK(kernel_mprotect(executable,PAGE,5)==0);
        CHECK(protect_at(executable)==PAGE_EXECUTE_READ);
        CHECK(direct_unmap(executable,PAGE)==0);
    }
    int64_t phys=-1;
    void *direct_code=NULL;
    CHECK(direct_allocate(0,INT64_MAX,PAGE,0,3,&phys)==0);
    CHECK(direct_map(&direct_code,PAGE,7,0,phys,0)==0);
    if (direct_code) {
        *(unsigned char *)direct_code=0xc3;
        CHECK(protect_at(direct_code)==PAGE_EXECUTE_READWRITE);
        ((void (*)(void))direct_code)();
        CHECK(direct_unmap(direct_code,PAGE)==0);
    }
    CHECK(direct_release(phys,PAGE)==0);

    const uint64_t charged=128u<<20;
    CHECK(direct_allocate(0,INT64_MAX,charged,0,3,&phys)==0);
    void *first=NULL,*second=NULL;
    const uint64_t commit_before=system_commit();
    CHECK(direct_map(&first,charged,3,0,phys,0)==0);
    const uint64_t commit_first=system_commit();
    CHECK(direct_map(&second,charged,3,0,phys,0)==0);
    const uint64_t commit_second=system_commit();
    CHECK(commit_first-commit_before>=charged*3/4);
    CHECK(commit_second-commit_first<charged/2);
    CHECK(direct_unmap(first,charged)==0); CHECK(direct_unmap(second,charged)==0); CHECK(direct_release(phys,charged)==0);

    CHECK(direct_allocate(0,INT64_MAX,8*PAGE,0,3,&phys)==0);
    first=NULL; second=NULL;
    CHECK(direct_map(&first,8*PAGE,3,0,phys,0)==0); CHECK(direct_map(&second,8*PAGE,3,0,phys,0)==0);
    CHECK(first && second && first!=second);
    ((uint64_t *)first)[3]=UINT64_C(0x1122334455667788);
    CHECK(((uint64_t *)second)[3]==UINT64_C(0x1122334455667788));
    ((uint64_t *)second)[5]=UINT64_C(0x8877665544332211);
    CHECK(((uint64_t *)first)[5]==UINT64_C(0x8877665544332211));

    void *failed=NULL; fail_commit=1;
    CHECK(map_flexible(&failed,PAGE,3,0)==NO_MEMORY);
    fail_commit=0;
    CHECK(((uint64_t *)first)[3]==UINT64_C(0x1122334455667788));
    void *replacement=first; fail_commit=1;
    CHECK(map_flexible(&replacement,PAGE,3,MAP_FIXED_FLAG)==NO_MEMORY);
    replacement=first;
    CHECK(direct_map(&replacement,PAGE,3,MAP_FIXED_FLAG,phys,0)==NO_MEMORY);
    fail_commit=0;
    CHECK(((uint64_t *)first)[3]==UINT64_C(0x1122334455667788));
    CHECK(((uint64_t *)second)[3]==UINT64_C(0x1122334455667788));
    void *start=NULL,*end=NULL; uint32_t prot=0;
    CHECK(query_protection(first,&start,&end,&prot)==0 && start==first && end>=(void *)((char *)first+8*PAGE) && prot==3);
    CHECK(map_flexible(&failed,PAGE,3,0)==0); CHECK(direct_unmap(failed,PAGE)==0);

    CHECK(kernel_mprotect(first,2*PAGE,1)==0);
    CHECK(direct_unmap((char *)first+2*PAGE,2*PAGE)==0);
    CHECK(protect_at(first)==PAGE_READONLY);
    CHECK(((uint64_t *)first)[3]==UINT64_C(0x1122334455667788));
    CHECK(direct_unmap(first,2*PAGE)==0); CHECK(direct_unmap((char *)first+4*PAGE,4*PAGE)==0);
    CHECK(direct_unmap(second,8*PAGE)==0); CHECK(direct_release(phys,8*PAGE)==0);
    int64_t reused_phys=-1; void *reused=NULL; CHECK(direct_allocate(0,INT64_MAX,8*PAGE,0,3,&reused_phys)==0);
    CHECK(reused_phys==phys);
    CHECK(direct_map(&reused,8*PAGE,3,0,reused_phys,0)==0);
    CHECK(((uint64_t *)reused)[3]==0 && ((uint64_t *)reused)[5]==0);
    CHECK(direct_unmap(reused,8*PAGE)==0); CHECK(direct_release(reused_phys,8*PAGE)==0);

    const int64_t untouched=INT64_C(2)<<30;
    CHECK(direct_allocate(untouched,INT64_MAX,8*PAGE,0,3,&phys)==0);
    CHECK(phys==untouched);
    void *partial=NULL; CHECK(direct_map(&partial,2*PAGE,3,0,phys,0)==0);
    if (partial) *(unsigned char *)partial=0x77;
    CHECK(state_at(backing_base+phys+2*PAGE)==MEM_RESERVE);
    CHECK(direct_unmap(partial,2*PAGE)==0); CHECK(direct_release(phys,8*PAGE)==0);
    CHECK(state_at(backing_base+phys+2*PAGE)==MEM_RESERVE);
    partial=NULL; CHECK(direct_allocate(untouched,INT64_MAX,8*PAGE,0,3,&phys)==0);
    CHECK(phys==untouched);
    CHECK(direct_map(&partial,2*PAGE,3,0,phys,0)==0);
    CHECK(partial && *(unsigned char *)partial==0);
    CHECK(direct_unmap(partial,2*PAGE)==0); CHECK(direct_release(phys,8*PAGE)==0);

    int64_t high=(int64_t)(POOL_SIZE-65536); CHECK(direct_allocate(high,(int64_t)POOL_SIZE,65536,65536,3,&phys)==0);
    CHECK(direct_release(phys,65536)==0);
    CHECK(state_at(backing_base+high)==MEM_RESERVE);

    CHECK(pool_expand(0,INT64_MAX,65536,65536,&phys)==0);
    void *reserved=NULL; CHECK(pool_reserve(NULL,65536,65536,0,&reserved)==0);
    CHECK(state_at(reserved)==MEM_RESERVE);
    CHECK(pool_commit(reserved,65536,3,3,0)==0); memset(reserved,0x5a,65536);
    CHECK(state_at(reserved)==MEM_COMMIT);
    CHECK(pool_decommit(reserved,65536,0)==0);
    CHECK(state_at(reserved)==MEM_RESERVE);
    CHECK(pool_commit(reserved,65536,3,3,0)==0);
    CHECK(pool_decommit(reserved,65536,0)==0);
    printf("Windows memory: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
