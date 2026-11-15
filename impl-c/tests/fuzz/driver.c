#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int main(int argc, char **argv) {
    unsigned long count=argc>1?strtoul(argv[1],NULL,10):1000;
    uint32_t state=0x12345678u; uint8_t data[1024];
    for(unsigned long n=0;n<count;n++) {
        for(size_t i=0;i<sizeof(data);i++) { state^=state<<13; state^=state>>17; state^=state<<5; data[i]=(uint8_t)state; }
        LLVMFuzzerTestOneInput(data,n%(sizeof(data)+1));
    }
    return 0;
}
