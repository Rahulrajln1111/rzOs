#include <efi.h>
#include <efilib.h>

// Notice: No EFIAPI here. gnu-efi's crt0 handles the translation for us.
EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    
    // Initialize the gnu-efi helper library
    InitializeLib(ImageHandle, SystemTable);

    // Safely call the motherboard's OutputString function using the wrapper!
    // Argument 1: The motherboard function you want to call
    // Argument 2: The number of arguments that function takes (2)
    // Argument 3 & 4: The actual arguments for OutputString
    uefi_call_wrapper(
        (void*)SystemTable->ConOut->OutputString, 
        2, 
        SystemTable->ConOut, 
        (CHAR16*)L"Hello RahulOS!\r\n"
    );

    // Halt the CPU so it doesn't reboot
    while (1) {
        __asm__ __volatile__("hlt");
    }

    return EFI_SUCCESS;
}