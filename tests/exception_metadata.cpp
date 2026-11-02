// CPU-only regression for the x86 metadata reader; does not run the D3D harness.
#define main D3d8AbiMain
#include "../src/d3d8abi.cpp"
#undef main

int main()
{
    struct Descriptor { void *vtable; void *spare; char name[32]; };
    Descriptor descriptor = { NULL, NULL, ".?AVsystem_error@std@@" };
    ULONG_PTR catchable[] = { 0, (ULONG_PTR)&descriptor };
    ULONG_PTR types[] = { 1, (ULONG_PTR)catchable };
    int destructorBytes = -12345;
    // ThrowInfo: attributes, unwind function, forward compatibility, type array.
    ULONG_PTR throwInfo[] = { 0, (ULONG_PTR)&destructorBytes, 0, (ULONG_PTR)types };
    static_assert(sizeof(throwInfo) == 16, "x86 ThrowInfo layout");

    logFile = fopen("exception_metadata.log", "w+");
    if (!logFile) return 2;
    DumpTypeName((const unsigned char *)throwInfo);
    throwInfo[3] = 0;
    DumpTypeName((const unsigned char *)throwInfo);
    rewind(logFile);
    char output[1024] = {};
    fread(output, 1, sizeof(output) - 1, logFile);
    fclose(logFile);
    if (!strstr(output, "catchable types=1") ||
        !strstr(output, "catchable type[0] name=.?AVsystem_error@std@@") ||
        !strstr(output, "throwInfo has no catchable type array")) {
        fprintf(stderr, "FAIL: unexpected type-reader output:\n%s", output);
        return 1;
    }
    puts("PASS: x86 ThrowInfo type array at +12, not destructor at +4");
    return 0;
}
