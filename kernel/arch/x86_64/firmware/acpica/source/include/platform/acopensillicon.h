/******************************************************************************
 * OpenSillicon ACPICA platform configuration.
 * Local glue for the freestanding x86_64 kernel; no host OS headers/runtime.
 ******************************************************************************/

#ifndef __ACOPENSILLICON_H__
#define __ACOPENSILLICON_H__

#include <stddef.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>

#define ACPI_MACHINE_WIDTH 64
#define COMPILER_DEPENDENT_INT64 int64_t
#define COMPILER_DEPENDENT_UINT64 uint64_t
#define ACPI_UINTPTR_T uintptr_t
#define ACPI_TO_INTEGER(pointer) ((uintptr_t)(pointer))
#define ACPI_OFFSET(type, field) offsetof(type, field)
#define ACPI_USE_DO_WHILE_0
#define ACPI_USE_LOCAL_CACHE
#define ACPI_USE_SYSTEM_CLIBRARY
#define ACPI_USE_NATIVE_DIVIDE
#define ACPI_USE_NATIVE_MATH64
#define ACPI_MUTEX_TYPE ACPI_OSL_MUTEX

#endif
