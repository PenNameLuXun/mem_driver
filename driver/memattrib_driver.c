#include <ntifs.h>
#include <ntstrsafe.h>

#define _KERNEL_MODE
#include "../shared/memattrib_shared.h"

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD MemAttribUnload;

NTKERNELAPI
NTSTATUS
MmCopyVirtualMemory(
    _In_ PEPROCESS FromProcess,
    _In_ PVOID FromAddress,
    _In_ PEPROCESS ToProcess,
    _Out_writes_bytes_(BufferSize) PVOID ToAddress,
    _In_ SIZE_T BufferSize,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PSIZE_T NumberOfBytesCopied
);

static NTSTATUS MemAttribCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp);
static NTSTATUS MemAttribDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp);
static NTSTATUS MemAttribHandleQueryRegion(PIRP Irp, PIO_STACK_LOCATION IrpSp);
static NTSTATUS MemAttribHandleSnapshot(PIRP Irp, PIO_STACK_LOCATION IrpSp);
static NTSTATUS MemAttribHandleReadMemory(PIRP Irp, PIO_STACK_LOCATION IrpSp);
static NTSTATUS MemAttribQueryRegion(
    _In_ PEPROCESS Process,
    _In_ PVOID Address,
    _Out_ PMEMATTRIB_REGION_INFO RegionInfo,
    _Out_opt_ PVOID* NextAddress
);
static VOID MemAttribFillMappedFile(
    _In_ PVOID Address,
    _Out_writes_(MEMATTRIB_MAX_PATH_CHARS) PWCHAR Buffer
);

NTSTATUS
DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    NTSTATUS status;
    PDEVICE_OBJECT deviceObject = NULL;
    UNICODE_STRING deviceName = RTL_CONSTANT_STRING(MEMATTRIB_NT_DEVICE_NAME);
    UNICODE_STRING dosName = RTL_CONSTANT_STRING(MEMATTRIB_DOS_SYMLINK_NAME);

    status = IoCreateDevice(
        DriverObject,
        0,
        &deviceName,
        MEMATTRIB_DEVICE_TYPE,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &deviceObject
    );
    if (!NT_SUCCESS(status)) {
        return status;
    }

    deviceObject->Flags |= DO_BUFFERED_IO;

    status = IoCreateSymbolicLink(&dosName, &deviceName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(deviceObject);
        return status;
    }

    DriverObject->MajorFunction[IRP_MJ_CREATE] = MemAttribCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = MemAttribCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = MemAttribDeviceControl;
    DriverObject->DriverUnload = MemAttribUnload;

    deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

VOID
MemAttribUnload(_In_ PDRIVER_OBJECT DriverObject)
{
    UNICODE_STRING dosName = RTL_CONSTANT_STRING(MEMATTRIB_DOS_SYMLINK_NAME);

    IoDeleteSymbolicLink(&dosName);
    IoDeleteDevice(DriverObject->DeviceObject);
}

static NTSTATUS
MemAttribCreateClose(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS
MemAttribDeviceControl(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status;

    switch (irpSp->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_MEMATTRIB_QUERY_REGION:
        status = MemAttribHandleQueryRegion(Irp, irpSp);
        break;
    case IOCTL_MEMATTRIB_SNAPSHOT_REGIONS:
        status = MemAttribHandleSnapshot(Irp, irpSp);
        break;
    case IOCTL_MEMATTRIB_READ_MEMORY:
        status = MemAttribHandleReadMemory(Irp, irpSp);
        break;
    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        Irp->IoStatus.Information = 0;
        break;
    }

    Irp->IoStatus.Status = status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

static NTSTATUS
MemAttribHandleQueryRegion(_In_ PIRP Irp, _In_ PIO_STACK_LOCATION IrpSp)
{
    NTSTATUS status;
    PEPROCESS process = NULL;
    PMEMATTRIB_REGION_REQUEST request;
    PMEMATTRIB_REGION_INFO response;
    ULONG inputLength = IrpSp->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outputLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;

    if (inputLength < sizeof(MEMATTRIB_REGION_REQUEST) ||
        outputLength < sizeof(MEMATTRIB_REGION_INFO)) {
        Irp->IoStatus.Information = 0;
        return STATUS_BUFFER_TOO_SMALL;
    }

    request = (PMEMATTRIB_REGION_REQUEST)Irp->AssociatedIrp.SystemBuffer;
    response = (PMEMATTRIB_REGION_INFO)Irp->AssociatedIrp.SystemBuffer;

    status = PsLookupProcessByProcessId(ULongToHandle(request->ProcessId), &process);
    if (!NT_SUCCESS(status)) {
        Irp->IoStatus.Information = 0;
        return status;
    }

    status = MemAttribQueryRegion(process, (PVOID)(ULONG_PTR)request->Address, response, NULL);
    ObDereferenceObject(process);

    Irp->IoStatus.Information = NT_SUCCESS(status) ? sizeof(MEMATTRIB_REGION_INFO) : 0;
    return status;
}

static NTSTATUS
MemAttribHandleSnapshot(_In_ PIRP Irp, _In_ PIO_STACK_LOCATION IrpSp)
{
    NTSTATUS status;
    PEPROCESS process = NULL;
    PMEMATTRIB_SNAPSHOT_RESPONSE response;
    MEMATTRIB_SNAPSHOT_REQUEST request;
    ULONG inputLength = IrpSp->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outputLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
    ULONG capacity;
    ULONG count = 0;
    ULONG requestedCount;
    ULONG64 cursor;
    ULONG64 highestUserAddress = (ULONG64)(ULONG_PTR)MmHighestUserAddress;

    if (inputLength < sizeof(MEMATTRIB_SNAPSHOT_REQUEST) ||
        outputLength < FIELD_OFFSET(MEMATTRIB_SNAPSHOT_RESPONSE, Regions)) {
        Irp->IoStatus.Information = 0;
        return STATUS_BUFFER_TOO_SMALL;
    }

    capacity = (outputLength - FIELD_OFFSET(MEMATTRIB_SNAPSHOT_RESPONSE, Regions)) /
        sizeof(MEMATTRIB_REGION_INFO);
    if (capacity == 0) {
        Irp->IoStatus.Information = 0;
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlCopyMemory(&request, Irp->AssociatedIrp.SystemBuffer, sizeof(request));
    response = (PMEMATTRIB_SNAPSHOT_RESPONSE)Irp->AssociatedIrp.SystemBuffer;
    RtlZeroMemory(response, outputLength);

    requestedCount = request.MaxRegionCount == 0 ? 1 : request.MaxRegionCount;
    if (requestedCount > capacity) {
        requestedCount = capacity;
    }
    if (requestedCount > MEMATTRIB_MAX_BATCH_COUNT) {
        requestedCount = MEMATTRIB_MAX_BATCH_COUNT;
    }

    status = PsLookupProcessByProcessId(ULongToHandle(request.ProcessId), &process);
    if (!NT_SUCCESS(status)) {
        Irp->IoStatus.Information = 0;
        return status;
    }

    cursor = request.StartAddress;
    while (count < requestedCount && cursor <= highestUserAddress) {
        PVOID nextAddress = NULL;
        status = MemAttribQueryRegion(
            process,
            (PVOID)(ULONG_PTR)cursor,
            &response->Regions[count],
            &nextAddress
        );
        if (!NT_SUCCESS(status)) {
            break;
        }

        count += 1;
        if (nextAddress == NULL || (ULONG64)(ULONG_PTR)nextAddress <= cursor) {
            cursor = highestUserAddress + 1;
            break;
        }

        cursor = (ULONG64)(ULONG_PTR)nextAddress;
    }

    ObDereferenceObject(process);

    if (!NT_SUCCESS(status) && count == 0) {
        Irp->IoStatus.Information = 0;
        return status;
    }

    response->RegionCount = count;
    response->NextAddress = cursor;
    response->MoreData = cursor <= highestUserAddress ? 1U : 0U;

    Irp->IoStatus.Information = FIELD_OFFSET(MEMATTRIB_SNAPSHOT_RESPONSE, Regions) +
        count * sizeof(MEMATTRIB_REGION_INFO);
    return STATUS_SUCCESS;
}

static NTSTATUS
MemAttribHandleReadMemory(_In_ PIRP Irp, _In_ PIO_STACK_LOCATION IrpSp)
{
    NTSTATUS status;
    PEPROCESS process = NULL;
    PMEMATTRIB_READ_RESPONSE response;
    MEMATTRIB_READ_REQUEST request;
    ULONG inputLength = IrpSp->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outputLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
    SIZE_T maxPayload;
    SIZE_T bytesRead = 0;

    if (inputLength < sizeof(MEMATTRIB_READ_REQUEST) ||
        outputLength < FIELD_OFFSET(MEMATTRIB_READ_RESPONSE, Data)) {
        Irp->IoStatus.Information = 0;
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlCopyMemory(&request, Irp->AssociatedIrp.SystemBuffer, sizeof(request));
    response = (PMEMATTRIB_READ_RESPONSE)Irp->AssociatedIrp.SystemBuffer;
    RtlZeroMemory(response, outputLength);

    if (request.Size == 0 || request.Size > MEMATTRIB_MAX_READ_SIZE) {
        Irp->IoStatus.Information = 0;
        return STATUS_INVALID_PARAMETER;
    }

    maxPayload = outputLength - FIELD_OFFSET(MEMATTRIB_READ_RESPONSE, Data);
    if (maxPayload == 0 || request.Size > maxPayload) {
        Irp->IoStatus.Information = 0;
        return STATUS_BUFFER_TOO_SMALL;
    }

    status = PsLookupProcessByProcessId(ULongToHandle(request.ProcessId), &process);
    if (!NT_SUCCESS(status)) {
        Irp->IoStatus.Information = 0;
        return status;
    }

    status = MmCopyVirtualMemory(
        process,
        (PVOID)(ULONG_PTR)request.Address,
        PsGetCurrentProcess(),
        response->Data,
        request.Size,
        KernelMode,
        &bytesRead
    );

    ObDereferenceObject(process);

    if (!NT_SUCCESS(status) && bytesRead == 0) {
        Irp->IoStatus.Information = 0;
        return status;
    }

    response->BytesRead = (ULONG)bytesRead;
    Irp->IoStatus.Information = FIELD_OFFSET(MEMATTRIB_READ_RESPONSE, Data) + (ULONG)bytesRead;

    return STATUS_SUCCESS;
}

static NTSTATUS
MemAttribQueryRegion(
    _In_ PEPROCESS Process,
    _In_ PVOID Address,
    _Out_ PMEMATTRIB_REGION_INFO RegionInfo,
    _Out_opt_ PVOID* NextAddress
)
{
    NTSTATUS status;
    KAPC_STATE apcState;
    MEMORY_BASIC_INFORMATION mbi;
    SIZE_T bytesReturned = 0;
    ULONG_PTR nextValue;

    RtlZeroMemory(&mbi, sizeof(mbi));
    RtlZeroMemory(RegionInfo, sizeof(*RegionInfo));

    KeStackAttachProcess(Process, &apcState);
    status = ZwQueryVirtualMemory(
        ZwCurrentProcess(),
        Address,
        MemoryBasicInformation,
        &mbi,
        sizeof(mbi),
        &bytesReturned
    );

    if (NT_SUCCESS(status)) {
        RegionInfo->BaseAddress = (ULONG64)(ULONG_PTR)mbi.BaseAddress;
        RegionInfo->AllocationBase = (ULONG64)(ULONG_PTR)mbi.AllocationBase;
        RegionInfo->RegionSize = (ULONG64)mbi.RegionSize;
        RegionInfo->State = mbi.State;
        RegionInfo->Protect = mbi.Protect;
        RegionInfo->Type = mbi.Type;
        RegionInfo->AllocationProtect = mbi.AllocationProtect;

        if (mbi.Type == MEM_IMAGE || mbi.Type == MEM_MAPPED) {
            MemAttribFillMappedFile(mbi.BaseAddress, RegionInfo->MappedFile);
        }
    }

    KeUnstackDetachProcess(&apcState);

    if (!NT_SUCCESS(status)) {
        return status;
    }

    nextValue = (ULONG_PTR)mbi.BaseAddress + mbi.RegionSize;
    if (NextAddress != NULL) {
        *NextAddress = nextValue > (ULONG_PTR)mbi.BaseAddress ? (PVOID)nextValue : NULL;
    }

    return STATUS_SUCCESS;
}

static VOID
MemAttribFillMappedFile(_In_ PVOID Address, _Out_writes_(MEMATTRIB_MAX_PATH_CHARS) PWCHAR Buffer)
{
    UCHAR localBuffer[sizeof(UNICODE_STRING) + MEMATTRIB_MAX_PATH_CHARS * sizeof(WCHAR)];
    PUNICODE_STRING fileName = (PUNICODE_STRING)localBuffer;
    SIZE_T bytesReturned = 0;
    USHORT charCount;
    NTSTATUS status;

    RtlZeroMemory(localBuffer, sizeof(localBuffer));
    RtlZeroMemory(Buffer, MEMATTRIB_MAX_PATH_CHARS * sizeof(WCHAR));

    status = ZwQueryVirtualMemory(
        ZwCurrentProcess(),
        Address,
        MemoryMappedFilenameInformation,
        localBuffer,
        sizeof(localBuffer),
        &bytesReturned
    );
    if (!NT_SUCCESS(status) || fileName->Buffer == NULL || fileName->Length == 0) {
        return;
    }

    charCount = fileName->Length / sizeof(WCHAR);
    if (charCount >= MEMATTRIB_MAX_PATH_CHARS) {
        charCount = MEMATTRIB_MAX_PATH_CHARS - 1;
    }

    RtlCopyMemory(Buffer, fileName->Buffer, charCount * sizeof(WCHAR));
    Buffer[charCount] = L'\0';
}
