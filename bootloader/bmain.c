#include <efi.h>
#include <efilib.h>

EFI_FILE_PROTOCOL *LoadFile(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable, CHAR16* FileName){

    EFI_STATUS status;
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;

    status = uefi_call_wrapper(SystemTable->BootServices->HandleProtocol, 3, 
                               ImageHandle, 
                               &LoadedImageProtocol, 
                               (void**)&LoadedImage);


    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
    status = uefi_call_wrapper(SystemTable->BootServices->HandleProtocol, 3, 
                               LoadedImage->DeviceHandle, 
                               &FileSystemProtocol, // Defined in efilib.h
                               (void**)&FileSystem);
                        
 EFI_FILE_PROTOCOL *RootDirectory;
    status = uefi_call_wrapper(FileSystem->OpenVolume, 2, 
                               FileSystem, 
                               &RootDirectory);

//search for our kernel file to load In RAM;
EFI_FILE_PROTOCOL *KernelFile;
    status = uefi_call_wrapper(RootDirectory->Open, 5, 
                               RootDirectory, 
                               &KernelFile, 
                               FileName, 
                               EFI_FILE_MODE_READ, 
                               0);


return KernelFile;
   

}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{

    InitializeLib(ImageHandle, SystemTable);

    uefi_call_wrapper(SystemTable->ConOut->ClearScreen, 1, SystemTable->ConOut);


    Print(L"Booting rzOs...\n");

    Print(L"Loading Kernel Files\n");


EFI_FILE_PROTOCOL *kernel = LoadFile(ImageHandle,SystemTable,L"kernel.bin");

if (kernel != NULL) {
        
        Print(L"[+] kernel.bin located on the hard drive! \n");
    } else {
        Print(L"[!] Kernel is missing. System Halting.\n");
    }

    while (1)
    {
        __asm__ __volatile__("hlt");
    }

    return EFI_SUCCESS;
}