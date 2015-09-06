/*
  Dokan : user-mode file system library for Windows

  Copyright (C) 2008 Hiroki Asakawa info@dokan-dev.net

  http://dokan-dev.net/en

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the Free
Software Foundation; either version 3 of the License, or (at your option) any
later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with this program. If not, see <http://www.gnu.org/licenses/>.
*/


#include "dokan.h"
#include <initguid.h>
#include <wdmsec.h>
#include <mountmgr.h>
#include <ntddstor.h>


static UNICODE_STRING sddl = RTL_CONSTANT_STRING(L"D:P(A;;GA;;;SY)(A;;GRGWGX;;;BA)(A;;GRGWGX;;;WD)(A;;GRGX;;;RC)");

NTSTATUS
DokanSendIoContlToMountManager(
	__in PVOID	InputBuffer,
	__in ULONG	Length
	)
{
	NTSTATUS		status;
	UNICODE_STRING	mountManagerName;
	PFILE_OBJECT    mountFileObject;
	PDEVICE_OBJECT  mountDeviceObject;
	PIRP			irp;
	KEVENT			driverEvent;
	IO_STATUS_BLOCK	iosb;

	DDbgPrint("=> DokanSendIoContlToMountManager\n");

	RtlInitUnicodeString(&mountManagerName, MOUNTMGR_DEVICE_NAME);


	status = IoGetDeviceObjectPointer(
				&mountManagerName,
				FILE_READ_ATTRIBUTES,
				&mountFileObject,
				&mountDeviceObject);

	if (!NT_SUCCESS(status)) {
		DDbgPrint("  IoGetDeviceObjectPointer failed: 0x%x\n", status);
		return status;
	}

	KeInitializeEvent(&driverEvent, NotificationEvent, FALSE);

	irp = IoBuildDeviceIoControlRequest(
			IOCTL_MOUNTMGR_VOLUME_ARRIVAL_NOTIFICATION,
			mountDeviceObject,
			InputBuffer,
			Length,
			NULL,
			0,
			FALSE,
			&driverEvent,
			&iosb);

	if (irp == NULL) {
		DDbgPrint("  IoBuildDeviceIoControlRequest failed\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	status = IoCallDriver(mountDeviceObject, irp);

	if (status == STATUS_PENDING) {
		KeWaitForSingleObject(
			&driverEvent, Executive, KernelMode, FALSE, NULL);
	}
	status = iosb.Status;

	ObDereferenceObject(mountFileObject);
	ObDereferenceObject(mountDeviceObject);

	if (NT_SUCCESS(status)) {
		DDbgPrint("  IoCallDriver success\n");
	} else {
		DDbgPrint("  IoCallDriver failed: 0x%x\n", status);
	}

	DDbgPrint("<= DokanSendIoContlToMountManager\n");

	return status;
}

NTSTATUS
DokanSendVolumeArrivalNotification(
	PUNICODE_STRING		DeviceName
	)
{
	NTSTATUS		status;
	PMOUNTMGR_TARGET_NAME targetName;
	ULONG			length;

	DDbgPrint("=> DokanSendVolumeArrivalNotification\n");

	length = sizeof(MOUNTMGR_TARGET_NAME) + DeviceName->Length - 1;
	targetName = ExAllocatePool(length);

	if (targetName == NULL) {
		DDbgPrint("  can't allocate MOUNTMGR_TARGET_NAME\n");
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	RtlZeroMemory(targetName, length);

	targetName->DeviceNameLength = DeviceName->Length;
	RtlCopyMemory(targetName->DeviceName, DeviceName->Buffer, DeviceName->Length);
	
	status = DokanSendIoContlToMountManager(targetName, length);

	if (NT_SUCCESS(status)) {
		DDbgPrint("  IoCallDriver success\n");
	} else {
		DDbgPrint("  IoCallDriver faield: 0x%x\n", status);
	}

	ExFreePool(targetName);

	DDbgPrint("<= DokanSendVolumeArrivalNotification\n");

	return status;
}


NTSTATUS
DokanRegisterMountedDeviceInterface(
	__in PDEVICE_OBJECT	DeviceObject,
	__in PDokanDCB		Dcb
	)
{
	NTSTATUS		status;
	UNICODE_STRING	interfaceName;
	DDbgPrint("=> DokanRegisterMountedDeviceInterface\n");

	status = IoRegisterDeviceInterface(
                DeviceObject,
                &MOUNTDEV_MOUNTED_DEVICE_GUID,
                NULL,
                &interfaceName
                );

    if(NT_SUCCESS(status)) {
		DDbgPrint("  InterfaceName:%wZ\n", &interfaceName);

        Dcb->MountedDeviceInterfaceName = interfaceName;
        status = IoSetDeviceInterfaceState(&interfaceName, TRUE);

        if(!NT_SUCCESS(status)) {
			DDbgPrint("  IoSetDeviceInterfaceState failed: 0x%x\n", status);
            RtlFreeUnicodeString(&interfaceName);
        }
	} else {
		DDbgPrint("  IoRegisterDeviceInterface failed: 0x%x\n", status);
	}

    if(!NT_SUCCESS(status)) {
        RtlInitUnicodeString(&(Dcb->MountedDeviceInterfaceName),
                             NULL);
    }
	DDbgPrint("<= DokanRegisterMountedDeviceInterface\n");
	return status;
}


NTSTATUS
DokanRegisterDeviceInterface(
	__in PDRIVER_OBJECT		DriverObject,
	__in PDEVICE_OBJECT		DeviceObject,
	__in PDokanDCB			Dcb
	)
{
	PDEVICE_OBJECT	pnpDeviceObject = NULL;
	NTSTATUS		status;

	status = IoReportDetectedDevice(
				DriverObject,
				InterfaceTypeUndefined,
				0,
				0,
				NULL,
				NULL,
				FALSE,
				&pnpDeviceObject);

	if (NT_SUCCESS(status)) {
		DDbgPrint("  IoReportDetectedDevice success\n");
	} else {
		DDbgPrint("  IoReportDetectedDevice failed: 0x%x\n", status);
		return status;
	}

	if (IoAttachDeviceToDeviceStack(pnpDeviceObject, DeviceObject) != NULL) {
		DDbgPrint("  IoAttachDeviceToDeviceStack success\n");
	} else {
		DDbgPrint("  IoAttachDeviceToDeviceStack failed\n");
	}

	status = IoRegisterDeviceInterface(
				pnpDeviceObject,
				&GUID_DEVINTERFACE_DISK,
				NULL,
				&Dcb->DiskDeviceInterfaceName);

	if (NT_SUCCESS(status)) {
		DDbgPrint("  IoRegisterDeviceInterface success: %wZ\n", &Dcb->DiskDeviceInterfaceName);
	} else {
		RtlInitUnicodeString(&Dcb->DiskDeviceInterfaceName, NULL);
		DDbgPrint("  IoRegisterDeviceInterface failed: 0x%x\n", status);
		return status;
	}

	status = IoSetDeviceInterfaceState(&Dcb->DiskDeviceInterfaceName, TRUE);

	if (NT_SUCCESS(status)) {
		DDbgPrint("  IoSetDeviceInterfaceState success\n");
	} else {
		DDbgPrint("  IoSetDeviceInterfaceState failed: 0x%x\n", status);
		return status;
	}

	status = IoRegisterDeviceInterface(
				pnpDeviceObject,
				&MOUNTDEV_MOUNTED_DEVICE_GUID,
				NULL,
				&Dcb->MountedDeviceInterfaceName);

	if (NT_SUCCESS(status)) {
		DDbgPrint("  IoRegisterDeviceInterface success: %wZ\n", &Dcb->MountedDeviceInterfaceName);
	} else {
		DDbgPrint("  IoRegisterDeviceInterface failed: 0x%x\n", status);
		return status;
	}

	status = IoSetDeviceInterfaceState(&Dcb->MountedDeviceInterfaceName, TRUE);

	if (NT_SUCCESS(status)) {
		DDbgPrint("  IoSetDeviceInterfaceState success\n");
	} else {
		RtlInitUnicodeString(&Dcb->MountedDeviceInterfaceName, NULL);
		DDbgPrint("  IoSetDeviceInterfaceState failed: 0x%x\n", status);
		return status;
	}

	return status;
}


VOID
DokanInitIrpList(
	 __in PIRP_LIST		IrpList
	 )
{
	InitializeListHead(&IrpList->ListHead);
	KeInitializeSpinLock(&IrpList->ListLock);
	KeInitializeEvent(&IrpList->NotEmpty, NotificationEvent, FALSE);
}


NTSTATUS
DokanCreateGlobalDiskDevice(
	__in PDRIVER_OBJECT DriverObject,
	__out PDOKAN_GLOBAL* DokanGlobal
	)
{
	WCHAR	deviceNameBuf[] = DOKAN_GLOBAL_DEVICE_NAME; 
	WCHAR	symbolicLinkBuf[] = DOKAN_GLOBAL_SYMBOLIC_LINK_NAME;
	WCHAR	fsDiskDeviceNameBuf[] = DOKAN_GLOBAL_FS_DISK_DEVICE_NAME;
	WCHAR	fsCdDeviceNameBuf[] = DOKAN_GLOBAL_FS_CD_DEVICE_NAME;
	WCHAR	fsNetworkDeviceNameBuf[] = DOKAN_GLOBAL_FS_NETWORK_DEVICE_NAME;
	NTSTATUS		status;
	UNICODE_STRING	deviceName;
	UNICODE_STRING	symbolicLinkName;
	UNICODE_STRING	fsDiskDeviceName;
	UNICODE_STRING	fsCdDeviceName;
	UNICODE_STRING	fsNetworkDeviceName;
	PDEVICE_OBJECT	deviceObject;
	PDEVICE_OBJECT	fsDiskDeviceObject;
	PDEVICE_OBJECT	fsCdDeviceObject;
	PDEVICE_OBJECT	fsNetworkDeviceObject;
	PDOKAN_GLOBAL	dokanGlobal;

	RtlInitUnicodeString(&deviceName, deviceNameBuf);
	RtlInitUnicodeString(&symbolicLinkName, symbolicLinkBuf);
	RtlInitUnicodeString(&fsDiskDeviceName, fsDiskDeviceNameBuf);
	RtlInitUnicodeString(&fsCdDeviceName, fsCdDeviceNameBuf);
	RtlInitUnicodeString(&fsNetworkDeviceName, fsNetworkDeviceNameBuf);

	status = IoCreateDeviceSecure(
				DriverObject,			// DriverObject
				sizeof(DOKAN_GLOBAL),	// DeviceExtensionSize
				&deviceName,			// DeviceName
				FILE_DEVICE_UNKNOWN,	// DeviceType
				0,						// DeviceCharacteristics
				FALSE,					// Not Exclusive
				&sddl,					// Default SDDL String
				NULL,					// Device Class GUID
				&deviceObject);			// DeviceObject

	if (!NT_SUCCESS(status)) {
		DDbgPrint("  IoCreateDevice returned 0x%x\n", status);
		return status;
	}
	DDbgPrint("DokanGlobalDevice: %wZ created\n", &deviceName);

	// Create supported file system device types and register them

	status = IoCreateDeviceSecure(
		DriverObject,					// DriverObject
		0,								// DeviceExtensionSize
		&fsDiskDeviceName,				// DeviceName
		FILE_DEVICE_DISK_FILE_SYSTEM,	// DeviceType
		0,								// DeviceCharacteristics
		FALSE,							// Not Exclusive
		&sddl,							// Default SDDL String
		NULL,							// Device Class GUID
		&fsDiskDeviceObject);			// DeviceObject

	if (!NT_SUCCESS(status)) {
		DDbgPrint("  IoCreateDevice Disk FileSystem failed: 0x%x\n", status);
		IoDeleteDevice(deviceObject);
		return status;
	}
	DDbgPrint("DokanDiskFileSystemDevice: %wZ created\n", fsDiskDeviceName);

	status = IoCreateDeviceSecure(
		DriverObject,					// DriverObject
		0,								// DeviceExtensionSize
		&fsCdDeviceName,				// DeviceName
		FILE_DEVICE_CD_ROM_FILE_SYSTEM,	// DeviceType
		0,								// DeviceCharacteristics
		FALSE,							// Not Exclusive
		&sddl,							// Default SDDL String
		NULL,							// Device Class GUID
		&fsCdDeviceObject);				// DeviceObject

	if (!NT_SUCCESS(status)) {
		DDbgPrint("  IoCreateDevice Cd FileSystem failed: 0x%x\n", status);
		IoDeleteDevice(fsDiskDeviceObject);
		IoDeleteDevice(deviceObject);
		return status;
	}
	DDbgPrint("DokanCdFileSystemDevice: %wZ created\n", fsCdDeviceName);

	status = IoCreateDeviceSecure(
		DriverObject,					// DriverObject
		0,								// DeviceExtensionSize
		&fsNetworkDeviceName,			// DeviceName
		FILE_DEVICE_NETWORK_FILE_SYSTEM,// DeviceType
		0,								// DeviceCharacteristics
		FALSE,							// Not Exclusive
		&sddl,							// Default SDDL String
		NULL,							// Device Class GUID
		&fsNetworkDeviceObject);		// DeviceObject

	if (!NT_SUCCESS(status)) {
		DDbgPrint("  IoCreateDevice Network FileSystem failed: 0x%x\n", status);
		IoDeleteDevice(fsCdDeviceObject);
		IoDeleteDevice(fsDiskDeviceObject);
		IoDeleteDevice(deviceObject);
		return status;
	}
	DDbgPrint("DokanNetworkFileSystemDevice: %wZ created\n", fsNetworkDeviceName);

	ObReferenceObject(deviceObject);

	status = IoCreateSymbolicLink(&symbolicLinkName, &deviceName);
	if (!NT_SUCCESS(status)) {
		DDbgPrint("  IoCreateSymbolicLink returned 0x%x\n", status);
		IoDeleteDevice(deviceObject);
		return status;
	}
	DDbgPrint("SymbolicLink: %wZ -> %wZ created\n", &deviceName, &symbolicLinkName);
	dokanGlobal = deviceObject->DeviceExtension;
	dokanGlobal->DeviceObject = deviceObject;
	dokanGlobal->FsDiskDeviceObject = fsDiskDeviceObject;
	dokanGlobal->FsCdDeviceObject = fsCdDeviceObject;
	dokanGlobal->FsNetworkDeviceObject = fsNetworkDeviceObject;

	RtlZeroMemory(dokanGlobal, sizeof(DOKAN_GLOBAL));
	DokanInitIrpList(&dokanGlobal->PendingService);
	DokanInitIrpList(&dokanGlobal->NotifyService);

	dokanGlobal->Identifier.Type = DGL;
	dokanGlobal->Identifier.Size = sizeof(DOKAN_GLOBAL);

	//
	// Establish user-buffer access method.
	//
	fsDiskDeviceObject->Flags |= DO_DIRECT_IO;
	fsDiskDeviceObject->Flags |= DO_LOW_PRIORITY_FILESYSTEM;
	fsCdDeviceObject->Flags |= DO_DIRECT_IO;
	fsCdDeviceObject->Flags |= DO_LOW_PRIORITY_FILESYSTEM;
	fsNetworkDeviceObject->Flags |= DO_DIRECT_IO;
	fsNetworkDeviceObject->Flags |= DO_LOW_PRIORITY_FILESYSTEM;

	fsDiskDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
	fsCdDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
	fsNetworkDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

	// Register file systems
	IoRegisterFileSystem(fsDiskDeviceObject);
	IoRegisterFileSystem(fsCdDeviceObject);
	IoRegisterFileSystem(fsNetworkDeviceObject);

	// Register network provider
	status = FsRtlRegisterUncProvider(&(dokanGlobal->MupHandle), &fsNetworkDeviceName, FALSE);
	if (NT_SUCCESS(status)) {
		DDbgPrint("  FsRtlRegisterUncProvider success\n");
	}
	else {
		DDbgPrint("  FsRtlRegisterUncProvider failed: 0x%x\n", status);
		dokanGlobal->MupHandle = 0;
	}

	ObReferenceObject(fsDiskDeviceObject);
	ObReferenceObject(fsCdDeviceObject);
	ObReferenceObject(fsNetworkDeviceObject);

	*DokanGlobal = dokanGlobal;
	return STATUS_SUCCESS;
}


PUNICODE_STRING
AllocateUnicodeString(
	__in PCWSTR String)
{
	PUNICODE_STRING	unicode;
	PWSTR 	buffer;
	ULONG	length;

	unicode = ExAllocatePool(sizeof(UNICODE_STRING));
	if (unicode == NULL) {
		return NULL;
	}

	length = (ULONG)(wcslen(String) + 1) * sizeof(WCHAR);
	buffer = ExAllocatePool(length);
	if (buffer == NULL) {
		ExFreePool(unicode);
		return NULL;
	}
	RtlCopyMemory(buffer, String, length);
	RtlInitUnicodeString(unicode, buffer);
	return unicode;
}

VOID
FreeUnicodeString(
	PUNICODE_STRING	UnicodeString)
{
	if (UnicodeString != NULL) {
		ExFreePool(UnicodeString->Buffer);
		ExFreePool(UnicodeString);
	}
}


//#define DOKAN_NET_PROVIDER

NTSTATUS
DokanCreateDiskDevice(
	__in PDRIVER_OBJECT DriverObject,
	__in ULONG			MountId,
	__in PWCHAR			BaseGuid,
	__in PDOKAN_GLOBAL	DokanGlobal,
	__in DEVICE_TYPE	DeviceType,
	__in ULONG			DeviceCharacteristics,
	__out PDokanDCB*	Dcb
	)
{
	WCHAR				diskDeviceNameBuf[MAXIMUM_FILENAME_LENGTH];
	WCHAR				symbolicLinkNameBuf[MAXIMUM_FILENAME_LENGTH];
	PDEVICE_OBJECT		diskDeviceObject;
	PDEVICE_OBJECT		volDeviceObject;
	PDokanDCB			dcb;
	PDokanVCB			vcb;
	UNICODE_STRING		diskDeviceName;
	NTSTATUS			status;
	BOOLEAN				isNetworkFileSystem = (DeviceType == FILE_DEVICE_NETWORK_FILE_SYSTEM);

	// make DeviceName and SymboliLink
	if (isNetworkFileSystem) {
#ifdef DOKAN_NET_PROVIDER
		RtlStringCchCopyW(diskDeviceNameBuf, MAXIMUM_FILENAME_LENGTH, DOKAN_NET_DEVICE_NAME);
		RtlStringCchCopyW(symbolicLinkNameBuf, MAXIMUM_FILENAME_LENGTH, DOKAN_NET_SYMBOLIC_LINK_NAME);
#else
		RtlStringCchCopyW(diskDeviceNameBuf, MAXIMUM_FILENAME_LENGTH, DOKAN_NET_DEVICE_NAME);
		RtlStringCchCatW(diskDeviceNameBuf, MAXIMUM_FILENAME_LENGTH, BaseGuid);
		RtlStringCchCopyW(symbolicLinkNameBuf, MAXIMUM_FILENAME_LENGTH, DOKAN_NET_SYMBOLIC_LINK_NAME);
		RtlStringCchCatW(symbolicLinkNameBuf, MAXIMUM_FILENAME_LENGTH, BaseGuid);
#endif

	} else {
		RtlStringCchCopyW(diskDeviceNameBuf, MAXIMUM_FILENAME_LENGTH, DOKAN_DISK_DEVICE_NAME);
		RtlStringCchCatW(diskDeviceNameBuf, MAXIMUM_FILENAME_LENGTH, BaseGuid);
		RtlStringCchCopyW(symbolicLinkNameBuf, MAXIMUM_FILENAME_LENGTH, DOKAN_SYMBOLIC_LINK_NAME);
		RtlStringCchCatW(symbolicLinkNameBuf, MAXIMUM_FILENAME_LENGTH, BaseGuid);
	}
	
	RtlInitUnicodeString(&diskDeviceName, diskDeviceNameBuf);

	//
	// make a DeviceObject for Disk Device
	//
	if (!isNetworkFileSystem) {
		status = IoCreateDeviceSecure(
					DriverObject,		// DriverObject
					sizeof(DokanDCB),	// DeviceExtensionSize
					&diskDeviceName,	// DeviceName
					FILE_DEVICE_DISK,	// DeviceType
					DeviceCharacteristics,	// DeviceCharacteristics
					FALSE,				// Not Exclusive
					&sddl, // Default SDDL String
					NULL,				// Device Class GUID
					&diskDeviceObject); // DeviceObject
	} else {
		status = IoCreateDevice(
					DriverObject,			// DriverObject
					sizeof(DokanDCB),		// DeviceExtensionSize
					NULL,					// DeviceName
					FILE_DEVICE_UNKNOWN,	// DeviceType
					DeviceCharacteristics,	// DeviceCharacteristics
					FALSE,					// Not Exclusive
					&diskDeviceObject);		// DeviceObject
	}


	if (!NT_SUCCESS(status)) {
		DDbgPrint("  IoCreateDevice (DISK_DEVICE) failed: 0x%x\n", status);
		return status;
	}
	DDbgPrint("DokanDiskDevice: %wZ created\n", &diskDeviceName);

	//
	// Initialize the device extension.
	//
	dcb = diskDeviceObject->DeviceExtension;
	*Dcb = dcb;
	dcb->DeviceObject = diskDeviceObject;
	dcb->Global = DokanGlobal;

	dcb->Identifier.Type = DCB;
	dcb->Identifier.Size = sizeof(DokanDCB);

	dcb->MountId = MountId;
	dcb->DeviceType = FILE_DEVICE_DISK;
	dcb->DeviceCharacteristics = DeviceCharacteristics;
	KeInitializeEvent(&dcb->KillEvent, NotificationEvent, FALSE);

	//
	// Establish user-buffer access method.
	//
	diskDeviceObject->Flags |= DO_DIRECT_IO;

	// initialize Event and Event queue
	DokanInitIrpList(&dcb->PendingIrp);
	DokanInitIrpList(&dcb->PendingEvent);
	DokanInitIrpList(&dcb->NotifyEvent);

	KeInitializeEvent(&dcb->ReleaseEvent, NotificationEvent, FALSE);

	// "0" means not mounted
	dcb->Mounted = 0;

	ExInitializeResourceLite(&dcb->Resource);

	dcb->CacheManagerNoOpCallbacks.AcquireForLazyWrite  = &DokanNoOpAcquire;
	dcb->CacheManagerNoOpCallbacks.ReleaseFromLazyWrite = &DokanNoOpRelease;
	dcb->CacheManagerNoOpCallbacks.AcquireForReadAhead  = &DokanNoOpAcquire;
	dcb->CacheManagerNoOpCallbacks.ReleaseFromReadAhead = &DokanNoOpRelease;

	dcb->SymbolicLinkName = AllocateUnicodeString(symbolicLinkNameBuf);
	dcb->DiskDeviceName =  AllocateUnicodeString(diskDeviceNameBuf);

	if (dcb->SymbolicLinkName == NULL) {
		DDbgPrint("  Can't allocate memory for SymbolicLinkName");
		ExDeleteResourceLite(&dcb->Resource);
		IoDeleteDevice(diskDeviceObject);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	if (dcb->DiskDeviceName == NULL) {
		DDbgPrint("  Can't allocate memory for DiskDeviceName");
		ExFreePool(dcb->SymbolicLinkName);
		ExDeleteResourceLite(&dcb->Resource);
		IoDeleteDevice(diskDeviceObject);
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	DDbgPrint("  IoCreateDevice DeviceType: %d\n", DeviceType);

	// Directly create volume device and init vcb here because it has strong dependency with fs/disk
	// Otherwise we would have done this work when mounting the volume

	status = IoCreateDevice(
		DriverObject,		// DriverObject
		sizeof(DokanVCB),	// DeviceExtensionSize
		NULL, // DeviceName
		DeviceType,			// DeviceType
		DeviceCharacteristics,	// DeviceCharacteristics
		FALSE,				// Not Exclusive
		&volDeviceObject);	// DeviceObject

	if (!NT_SUCCESS(status)) {
		DDbgPrint("  IoCreateDevice failed: 0x%x\n", status);
		ExDeleteResourceLite(&dcb->Resource);
		IoDeleteDevice(diskDeviceObject);
		return status;
	}

	vcb = volDeviceObject->DeviceExtension;
	vcb->Identifier.Type = VCB;
	vcb->Identifier.Size = sizeof(DokanVCB);

	vcb->DeviceObject = volDeviceObject;
	vcb->Dcb = dcb;
	dcb->Vcb = vcb;

	InitializeListHead(&vcb->NextFCB);

	InitializeListHead(&vcb->DirNotifyList);
	FsRtlNotifyInitializeSync(&vcb->NotifySync);

	ExInitializeFastMutex(&vcb->AdvancedFCBHeaderMutex);

#if _WIN32_WINNT >= 0x0501
	FsRtlSetupAdvancedHeader(&vcb->VolumeFileHeader, &vcb->AdvancedFCBHeaderMutex);
#else
	if (DokanFsRtlTeardownPerStreamContexts) {
		FsRtlSetupAdvancedHeader(&vcb->VolumeFileHeader, &vcb->AdvancedFCBHeaderMutex);
	}
#endif

	//
	// Establish user-buffer access method.
	//
	volDeviceObject->Flags |= DO_DIRECT_IO;

	DokanInitVpb(diskDeviceObject->Vpb, diskDeviceObject, volDeviceObject);

	//
	// Create a symbolic link for userapp to interact with the driver.
	//
	status = IoCreateSymbolicLink(dcb->SymbolicLinkName, dcb->DiskDeviceName);

	if (!NT_SUCCESS(status)) {
		IoDeleteDevice(diskDeviceObject);
		IoDeleteDevice(volDeviceObject);
		DDbgPrint("  IoCreateSymbolicLink returned 0x%x\n", status);
		return status;
	}
	DDbgPrint("SymbolicLink: %wZ -> %wZ created\n", dcb->SymbolicLinkName, dcb->DiskDeviceName);

	// Mark devices as initialized
	diskDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
	volDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

	ObReferenceObject(volDeviceObject);
	ObReferenceObject(diskDeviceObject);

	//DokanRegisterMountedDeviceInterface(diskDeviceObject, dcb);
	
	dcb->Mounted = 1;

	//DokanRegisterDeviceInterface(DriverObject, diskDeviceObject, dcb);
	DokanSendVolumeArrivalNotification(dcb->DiskDeviceName);

	return STATUS_SUCCESS;
}


VOID
DokanDeleteDeviceObject(
	__in PDokanDCB Dcb)
{
	PDokanVCB			vcb;

	ASSERT(GetIdentifierType(Dcb) == DCB);
	vcb = Dcb->Vcb;

    if (Dcb->SymbolicLinkName == NULL){
        DDbgPrint("  Symbolic Name already deleted, so go out here\n");
        return;
    }

	DDbgPrint("  Delete Symbolic Name: %wZ\n", Dcb->SymbolicLinkName);
	IoDeleteSymbolicLink(Dcb->SymbolicLinkName);

	if (Dcb->MountedDeviceInterfaceName.Buffer != NULL) {
		IoSetDeviceInterfaceState(&Dcb->MountedDeviceInterfaceName, FALSE);

		RtlFreeUnicodeString(&Dcb->MountedDeviceInterfaceName);
		RtlInitUnicodeString(&Dcb->MountedDeviceInterfaceName, NULL);
	}
	if (Dcb->DiskDeviceInterfaceName.Buffer != NULL) {
		IoSetDeviceInterfaceState(&Dcb->DiskDeviceInterfaceName, FALSE);

		RtlFreeUnicodeString(&Dcb->DiskDeviceInterfaceName);
		RtlInitUnicodeString(&Dcb->DiskDeviceInterfaceName, NULL);
	}

	FreeUnicodeString(Dcb->SymbolicLinkName);
	FreeUnicodeString(Dcb->DiskDeviceName);
	
	Dcb->SymbolicLinkName = NULL;
	Dcb->DiskDeviceName = NULL;

	if (Dcb->DeviceObject->Vpb) {
		Dcb->DeviceObject->Vpb->DeviceObject = NULL;
		Dcb->DeviceObject->Vpb->RealDevice = NULL;
		Dcb->DeviceObject->Vpb->Flags = 0;
	}

	if (vcb != NULL) {
		DDbgPrint("  FCB allocated: %d\n", vcb->FcbAllocated);
		DDbgPrint("  FCB     freed: %d\n", vcb->FcbFreed);
		DDbgPrint("  CCB allocated: %d\n", vcb->CcbAllocated);
		DDbgPrint("  CCB     freed: %d\n", vcb->CcbFreed);

		// delete volDeviceObject
		DDbgPrint("  Delete Volume DeviceObject\n");
		IoDeleteDevice(vcb->DeviceObject);
	}

	// delete diskDeviceObject
	DDbgPrint("  Delete Disk DeviceObject\n");
	IoDeleteDevice(Dcb->DeviceObject);
}

