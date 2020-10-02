#include <stdint.h>
#include <string.h>

#ifdef _WIN32
#define DllExport __declspec(dllexport)
#else
#define DllExport
#endif


extern "C"
{

DllExport int Ffi_MUL(int l, int r)
{
    return l * r;
}

typedef struct{
    enum Type { DBL, INT, PTR, STR };
    union {
        double d;
        int32_t i;
        void* p;
        char s[16];
    };
    uint8_t type;
} Variant;

DllExport Variant Ffi_get()
{
    Variant v;
    v.type = Variant::STR;
    strcpy(v.s,"gugus");
    return v;
}

}
