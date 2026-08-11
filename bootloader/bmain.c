#include <efi.h>
#include <efilib.h>
#include "../kernel/elf.hpp"
void Halt(void) {
  while (1) __asm__("hlt");
}

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

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{

    InitializeLib(ImageHandle, SystemTable);

    uefi_call_wrapper(SystemTable->ConOut->ClearScreen, 1, SystemTable->ConOut);


    Print(L"Booting rzOs...\n");

    Print(L"Loading Kernel Files\n");


EFI_FILE_PROTOCOL *kernel = LoadFile(ImageHandle, SystemTable, L"kernel.elf");

if (kernel == NULL) {
    Print(L"[!] Kernel is missing. System Halting.\n");
    Halt();
    return EFI_LOAD_ERROR; /* unreachable, but explicit for safety */
}
Print(L"[+] kernel.elf located on the hard drive!\n");

// Query the real kernel file size instead of hardcoding a 1MB buffer.
UINTN InfoSize = 0;
EFI_FILE_INFO *FileInfo = NULL;
EFI_STATUS status = uefi_call_wrapper(kernel->GetInfo, 4,
                                      kernel, &gEfiFileInfoGuid, &InfoSize, FileInfo);

if (status == EFI_BUFFER_TOO_SMALL) {
    status = uefi_call_wrapper(SystemTable->BootServices->AllocatePool, 3,
                               EfiLoaderData, InfoSize, (VOID **)&FileInfo);
    if (EFI_ERROR(status)) {
        Print(L"[-] Failed to allocate file info buffer. Status: %r\n", status);
        return status;
    }
    status = uefi_call_wrapper(kernel->GetInfo, 4,
                               kernel, &gEfiFileInfoGuid, &InfoSize, FileInfo);
}
if (EFI_ERROR(status)) {
    Print(L"[-] Failed to get kernel file info. Status: %r\n", status);
    if (FileInfo != NULL)
        uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, FileInfo);
    return status;
}

UINTN FileSize = (UINTN)FileInfo->FileSize;
uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, FileInfo);
Print(L"[+] Kernel file size: %llu bytes\n", (UINT64)FileSize);

if (FileSize < sizeof(Elf64_Ehdr)) {
    Print(L"[-] Kernel file too small to be a valid ELF.\n");
    return EFI_LOAD_ERROR;
}

// Allocate exactly enough pages for the file.
UINTN Pages = (FileSize + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;
EFI_PHYSICAL_ADDRESS KernelBuffer = 0;

status = uefi_call_wrapper(SystemTable->BootServices->AllocatePages, 4,
                           AllocateAnyPages, EfiLoaderData, Pages, &KernelBuffer);
if (EFI_ERROR(status)) {
    Print(L"[-] Failed to allocate pages. Status: %r\n", status);
    return status;
}

VOID *Buffer = (VOID *)(UINTN)KernelBuffer;

// Read the whole file into memory, handling short reads.
UINTN TotalRead = 0;
while (TotalRead < FileSize) {
    UINTN ChunkSize = FileSize - TotalRead;

    status = uefi_call_wrapper(kernel->Read, 3,
                               kernel, &ChunkSize, (UINT8 *)Buffer + TotalRead);
    if (EFI_ERROR(status)) {
        Print(L"[-] Failed to read kernel file. Status: %r\n", status);
        uefi_call_wrapper(SystemTable->BootServices->FreePages, 2, KernelBuffer, Pages);
        return status;
    }
    if (ChunkSize == 0) {
        Print(L"[-] Unexpected end of file (read %llu of %llu bytes).\n",
              (UINT64)TotalRead, (UINT64)FileSize);
        uefi_call_wrapper(SystemTable->BootServices->FreePages, 2, KernelBuffer, Pages);
        return EFI_LOAD_ERROR;
    }
    TotalRead += ChunkSize;
}
Print(L"[+] Loaded %llu bytes into memory.\n", (UINT64)TotalRead);

Elf64_Ehdr *elf_header = (Elf64_Ehdr *)Buffer;

if(elf_header->e_ident[0] == 0x7F &&
        elf_header->e_ident[1] == 'E' &&
        elf_header->e_ident[2] == 'L' &&
        elf_header->e_ident[3] == 'F'){
            Print(L"[+] Valid ELF Kernel is Loaded.\n");
            Print(L"Entry point : 0x%llx\n",elf_header->e_entry);
        }
        else  Print(L"[!] Not a valid Kernel ELF File.\n");


Halt();

    return EFI_SUCCESS;
}