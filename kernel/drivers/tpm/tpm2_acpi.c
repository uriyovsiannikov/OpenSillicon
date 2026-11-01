#include <string.h>

#include "kernel/tpm2_acpi.h"

#define ACPI_SDT_HEADER_SIZE 36U
#define TPM2_PLATFORM_CLASS_OFFSET 36U
#define TPM2_RESERVED_OFFSET 38U
#define TPM2_CONTROL_AREA_OFFSET 40U
#define TPM2_START_METHOD_OFFSET 48U
#define TPM2_START_PARAMETERS_OFFSET 52U
#define TPM2_START_PARAMETERS_MAX_SIZE 16U
#define TPM2_LAML_OFFSET 68U
#define TPM2_LASA_OFFSET 72U
#define TPM2_CONTROL_REQUEST_SIZE 4U

static uint16_t
read_le16(const uint8_t *bytes)
{
	return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint32_t
read_le32(const uint8_t *bytes)
{
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
	    ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t
read_le64(const uint8_t *bytes)
{
	return (uint64_t)read_le32(bytes) |
	    ((uint64_t)read_le32(bytes + 4) << 32);
}

static bool
range_fits(size_t total, size_t offset, size_t length)
{
	return offset <= total && length <= total - offset;
}

static bool
checksum_valid(const uint8_t *bytes, size_t length)
{
	uint8_t sum = 0;

	for (size_t i = 0; i < length; i++)
		sum = (uint8_t)(sum + bytes[i]);
	return sum == 0;
}

static bool
bytes_are_zero(const uint8_t *bytes, size_t length)
{
	for (size_t i = 0; i < length; i++)
		if (bytes[i] != 0)
			return false;
	return true;
}

enum tpm2_acpi_status
tpm2_acpi_parse(const void *table, size_t table_size,
	struct tpm2_acpi_info *info_out)
{
	const uint8_t *bytes = table;
	struct tpm2_acpi_info parsed;
	uint32_t declared_length;
	uint16_t reserved;
	uint64_t control_area;

	if (table == NULL || info_out == NULL)
		return TPM2_ACPI_INVALID_ARGUMENT;
	memset(&parsed, 0, sizeof(parsed));
	if (table_size < ACPI_SDT_HEADER_SIZE)
		return TPM2_ACPI_TRUNCATED_INPUT;
	if (memcmp(bytes, "TPM2", 4) != 0)
		return TPM2_ACPI_INVALID_SIGNATURE;
	declared_length = read_le32(bytes + 4);
	if (declared_length < TPM2_ACPI_MIN_TABLE_SIZE)
		return TPM2_ACPI_INVALID_LENGTH;
	if ((size_t)declared_length > table_size)
		return TPM2_ACPI_TRUNCATED_INPUT;
	if (!range_fits(declared_length, TPM2_PLATFORM_CLASS_OFFSET, 2) ||
	    !range_fits(declared_length, TPM2_RESERVED_OFFSET, 2) ||
	    !range_fits(declared_length, TPM2_CONTROL_AREA_OFFSET, 8) ||
	    !range_fits(declared_length, TPM2_START_METHOD_OFFSET, 4))
		return TPM2_ACPI_INVALID_LENGTH;
	if (bytes[8] != TPM2_ACPI_SUPPORTED_REVISION)
		return TPM2_ACPI_UNSUPPORTED_REVISION;
	if (!checksum_valid(bytes, declared_length))
		return TPM2_ACPI_INVALID_CHECKSUM;

	parsed.revision = bytes[8];
	parsed.table_length = declared_length;
	parsed.platform_class = read_le16(bytes + TPM2_PLATFORM_CLASS_OFFSET);
	if (parsed.platform_class > 1)
		return TPM2_ACPI_INVALID_PLATFORM_CLASS;
	reserved = read_le16(bytes + TPM2_RESERVED_OFFSET);
	if (reserved != 0)
		return TPM2_ACPI_INVALID_RESERVED;
	parsed.start_method = read_le32(bytes + TPM2_START_METHOD_OFFSET);
	if (parsed.start_method != TPM2_ACPI_START_METHOD_CRB)
		return TPM2_ACPI_UNSUPPORTED_START_METHOD;
	if (declared_length != TPM2_ACPI_MIN_TABLE_SIZE &&
	    declared_length != TPM2_ACPI_LOG_TABLE_SIZE)
		return TPM2_ACPI_INVALID_LENGTH;

	/* ACPI v1.50 defines no start parameters for method 7. The only accepted
	 * extended form is the revision-6 event-log trailer at offsets 68..79;
	 * require its intervening 16-byte parameter area to be zero rather than
	 * guessing how an unknown method extension should be interpreted.
	 */
	if (declared_length == TPM2_ACPI_LOG_TABLE_SIZE) {
		uint32_t log_length;
		uint64_t log_address;

		if (!range_fits(declared_length, TPM2_START_PARAMETERS_OFFSET,
		    TPM2_START_PARAMETERS_MAX_SIZE) ||
		    !range_fits(declared_length, TPM2_LAML_OFFSET, 4) ||
		    !range_fits(declared_length, TPM2_LASA_OFFSET, 8))
			return TPM2_ACPI_INVALID_LENGTH;
		if (!bytes_are_zero(bytes + TPM2_START_PARAMETERS_OFFSET,
		    TPM2_START_PARAMETERS_MAX_SIZE))
			return TPM2_ACPI_INVALID_START_PARAMETERS;
		log_length = read_le32(bytes + TPM2_LAML_OFFSET);
		log_address = read_le64(bytes + TPM2_LASA_OFFSET);
		if ((log_length == 0) != (log_address == 0) ||
		    (log_length != 0 &&
		    (uint64_t)(log_length - 1U) > UINT64_MAX - log_address))
			return TPM2_ACPI_INVALID_EVENT_LOG;
		parsed.event_log_present = log_length != 0;
		parsed.event_log_minimum_length = log_length;
		parsed.event_log_address = log_address;
	}

	control_area = read_le64(bytes + TPM2_CONTROL_AREA_OFFSET);
	/* For the PC Client CRB layout, the field identifies the 32-bit
	 * TPM_CRB_CTRL_REQ_0 register itself. Validate only that register's
	 * representable range; mapping and resource ownership belong to a driver.
	 */
	if (control_area == 0 ||
	    (control_area & (TPM2_CONTROL_REQUEST_SIZE - 1U)) != 0 ||
	    control_area > UINT64_MAX - TPM2_CONTROL_REQUEST_SIZE)
		return TPM2_ACPI_INVALID_ADDRESS;
	parsed.control_area_address = control_area;
	memcpy(info_out, &parsed, sizeof(parsed));
	return TPM2_ACPI_OK;
}
