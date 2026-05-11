#include <efi.h>
#include <efilib.h>

EFI_FILE_PROTOCOL *LoadFile(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable, CHAR16* FileName){

    EFI_STATUS status;
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;

    status = uefi_call_wrapper(SystemTable->BootServices->HandleProtocol, 3, 
                               ImageHandle, 
                               &LoadedImageProtocol, 
                               (void**)&LoadedImage);
 if (EFI_ERROR(status)) {
        Print(L"[-] Failed to LoadImage.\n");
    
    return NULL;
    }

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
    status = uefi_call_wrapper(SystemTable->BootServices->HandleProtocol, 3, 
                               LoadedImage->DeviceHandle, 
                               &FileSystemProtocol,
                               (void**)&FileSystem);
 if (EFI_ERROR(status)) {
        Print(L"[-] Failed to FileSystem.\n");return NULL;}
                        
 EFI_FILE_PROTOCOL *RootDirectory;
    status = uefi_call_wrapper(FileSystem->OpenVolume, 2, 
                               FileSystem, 
                               &RootDirectory);
 if (EFI_ERROR(status)) {
        Print(L"[-] Failed to Get Rootdir.\n");return NULL;}

//search for our kernel file to load In RAM;
EFI_FILE_PROTOCOL *KernelFile;
    status = uefi_call_wrapper(RootDirectory->Open, 5, 
                               RootDirectory, 
                               &KernelFile, 
                               FileName, 
                               EFI_FILE_MODE_READ, 
                               0);
if (EFI_ERROR(status)) {
    Print(L"[-] Failed to Load KernelFile. Status: %r (%x)\n", status, status);
    return NULL;
}

return KernelFile;
   

}

EFI_STATUS ReadFile(EFI_FILE_PROTOCOL *File, VOID *Buffer, UINTN *BufferSize,EFI_SYSTEM_TABLE *SystemTable) {


    //Read the file into the buffer
    EFI_STATUS status = uefi_call_wrapper(File->Read,3,File, BufferSize, Buffer);
    if (EFI_ERROR(status)) {
        Print(L"[-] Failed to read file into buffer.\n");
        SystemTable->BootServices->FreePool(Buffer);
        return status;
    } 
    
    return EFI_SUCCESS;

   
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
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

UINTN BufferSize = 1024 * 1024; // 1MB buffer
UINTN Pages = (BufferSize + 4095) / 4096; // Calculate how many 4KB pages you need for 1MB
EFI_PHYSICAL_ADDRESS KernelBuffer;

EFI_STATUS status = uefi_call_wrapper(SystemTable->BootServices->AllocatePages,4,
                           AllocateAnyPages,
                           EfiLoaderData,
                           Pages,
                           &KernelBuffer);

if (EFI_ERROR(status)) {
    Print(L"[-] Failed to allocate pages. Status: %r\n", status);
    return status;
}

VOID* Buffer = (VOID*)KernelBuffer;


 status = ReadFile(kernel,Buffer ,&BufferSize,SystemTable);


Print(L"Kernel loaded into memory at address: %p\n", Buffer);
Print(L"Kernel contains : %a\n", Buffer);


    while (1)
    {
        __asm__ __volatile__("hlt");
    }

    return EFI_SUCCESS;
}