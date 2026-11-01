#ifndef KERNEL_TPM2_ACPI_H
#define KERNEL_TPM2_ACPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TPM2_ACPI_SUPPORTED_REVISION 6U
#define TPM2_ACPI_START_METHOD_CRB 7U
#define TPM2_ACPI_MIN_TABLE_SIZE 52U
#define TPM2_ACPI_LOG_TABLE_SIZE 80U

enum tpm2_acpi_status {
	TPM2_ACPI_OK = 0,
	TPM2_ACPI_INVALID_ARGUMENT,
	TPM2_ACPI_TRUNCATED_INPUT,
	TPM2_ACPI_INVALID_SIGNATURE,
	TPM2_ACPI_INVALID_LENGTH,
	TPM2_ACPI_INVALID_CHECKSUM,
	TPM2_ACPI_UNSUPPORTED_REVISION,
	TPM2_ACPI_INVALID_RESERVED,
	TPM2_ACPI_INVALID_PLATFORM_CLASS,
	TPM2_ACPI_UNSUPPORTED_START_METHOD,
	TPM2_ACPI_INVALID_START_PARAMETERS,
	TPM2_ACPI_INVALID_ADDRESS,
	TPM2_ACPI_INVALID_EVENT_LOG
};

struct tpm2_acpi_info {
	uint8_t revision;
	uint16_t platform_class;
	uint32_t start_method;
	uint32_t table_length;
	/* Physical address of TPM_CRB_CTRL_REQ_0 in the PC Client CRB profile. */
	uint64_t control_area_address;
	/* Revision-6 optional ACPI event-log fields; not validated as a memory map. */
	bool event_log_present;
	uint32_t event_log_minimum_length;
	uint64_t event_log_address;
};

/* Pure parser. On every failure, info_out remains byte-for-byte unchanged.
 * Success validates the table description only; it does not validate or map
 * any physical range and does not establish that a device exists there.
 */
enum tpm2_acpi_status tpm2_acpi_parse(const void *table, size_t table_size,
	struct tpm2_acpi_info *info_out);

#endif
