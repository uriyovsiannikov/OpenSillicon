#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kernel/tpm2_acpi.h"

#define FIXTURE_CAPACITY 96U

static void
write_le16(uint8_t *bytes, uint16_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
}

static void
write_le32(uint8_t *bytes, uint32_t value)
{
	for (unsigned int i = 0; i < 4; i++)
		bytes[i] = (uint8_t)(value >> (i * 8));
}

static void
write_le64(uint8_t *bytes, uint64_t value)
{
	write_le32(bytes, (uint32_t)value);
	write_le32(bytes + 4, (uint32_t)(value >> 32));
}

static void
set_checksum(uint8_t *table, size_t length)
{
	uint8_t sum = 0;

	table[9] = 0;
	for (size_t i = 0; i < length; i++)
		sum = (uint8_t)(sum + table[i]);
	table[9] = (uint8_t)(0U - sum);
}

static size_t
make_table(uint8_t *table, uint32_t length, uint16_t platform_class,
	uint64_t control_area, uint32_t start_method)
{
	memset(table, 0, length);
	memcpy(table, "TPM2", 4);
	write_le32(table + 4, length);
	table[8] = TPM2_ACPI_SUPPORTED_REVISION;
	write_le16(table + 36, platform_class);
	write_le16(table + 38, 0);
	write_le64(table + 40, control_area);
	write_le32(table + 48, start_method);
	set_checksum(table, length);
	return length;
}

static void
assert_failure_unchanged(const void *table, size_t table_size,
	enum tpm2_acpi_status expected)
{
	struct tpm2_acpi_info output;
	struct tpm2_acpi_info before;
	enum tpm2_acpi_status actual;

	memset(&output, 0xa5, sizeof(output));
	memcpy(&before, &output, sizeof(before));
	actual = tpm2_acpi_parse(table, table_size, &output);
	if (actual != expected)
		fprintf(stderr, "parse status mismatch: got %d expected %d, size %zu\n",
		    actual, expected, table_size);
	assert(actual == expected);
	assert(memcmp(&output, &before, sizeof(output)) == 0);
}

static void
test_valid_minimum_and_unaligned(void)
{
	uint8_t raw[TPM2_ACPI_MIN_TABLE_SIZE + 1U];
	struct tpm2_acpi_info info;
	size_t length = make_table(raw + 1, TPM2_ACPI_MIN_TABLE_SIZE, 0,
	    0xFED40040ULL, TPM2_ACPI_START_METHOD_CRB);

	memset(&info, 0xcc, sizeof(info));
	assert(tpm2_acpi_parse(raw + 1, length, &info) == TPM2_ACPI_OK);
	assert(info.revision == TPM2_ACPI_SUPPORTED_REVISION);
	assert(info.platform_class == 0);
	assert(info.start_method == TPM2_ACPI_START_METHOD_CRB);
	assert(info.table_length == TPM2_ACPI_MIN_TABLE_SIZE);
	assert(info.control_area_address == 0xFED40040ULL);
	assert(!info.event_log_present);
	assert(info.event_log_minimum_length == 0 && info.event_log_address == 0);

	/* An input buffer may include bytes beyond the ACPI declared table. */
	raw[0] = 0xff;
	assert(tpm2_acpi_parse(raw + 1, sizeof(raw) - 1, &info) == TPM2_ACPI_OK);
}

static void
test_valid_extended_log_and_limits(void)
{
	uint8_t table[FIXTURE_CAPACITY];
	struct tpm2_acpi_info info;

	make_table(table, TPM2_ACPI_LOG_TABLE_SIZE, 1,
	    UINT64_MAX - 7U, TPM2_ACPI_START_METHOD_CRB);
	write_le32(table + 68, UINT32_MAX);
	write_le64(table + 72, 1);
	set_checksum(table, TPM2_ACPI_LOG_TABLE_SIZE);
	assert(tpm2_acpi_parse(table, TPM2_ACPI_LOG_TABLE_SIZE, &info) ==
	    TPM2_ACPI_OK);
	assert(info.platform_class == 1);
	assert(info.control_area_address == UINT64_MAX - 7U);
	assert(info.event_log_present);
	assert(info.event_log_minimum_length == UINT32_MAX);
	assert(info.event_log_address == 1);
}

static void
test_header_failures(void)
{
	uint8_t table[FIXTURE_CAPACITY];
	size_t length = make_table(table, TPM2_ACPI_MIN_TABLE_SIZE, 0,
	    0x1000, TPM2_ACPI_START_METHOD_CRB);

	assert(tpm2_acpi_parse(NULL, length, NULL) == TPM2_ACPI_INVALID_ARGUMENT);
	assert_failure_unchanged(NULL, length, TPM2_ACPI_INVALID_ARGUMENT);
	for (size_t n = 0; n < TPM2_ACPI_MIN_TABLE_SIZE; n++)
		assert_failure_unchanged(table, n, TPM2_ACPI_TRUNCATED_INPUT);

	table[0] = 'X';
	assert_failure_unchanged(table, length, TPM2_ACPI_INVALID_SIGNATURE);
	table[0] = 'T';

	write_le32(table + 4, 51);
	set_checksum(table, 51);
	assert_failure_unchanged(table, length, TPM2_ACPI_INVALID_LENGTH);
	make_table(table, length, 0, 0x1000, TPM2_ACPI_START_METHOD_CRB);
	assert_failure_unchanged(table, length - 1,
	    TPM2_ACPI_TRUNCATED_INPUT);

	table[20] ^= 1;
	assert_failure_unchanged(table, length, TPM2_ACPI_INVALID_CHECKSUM);
	table[20] ^= 1;
	table[8] = 5;
	set_checksum(table, length);
	assert_failure_unchanged(table, length,
	    TPM2_ACPI_UNSUPPORTED_REVISION);

	make_table(table, 53, 0, 0x1000, TPM2_ACPI_START_METHOD_CRB);
	assert_failure_unchanged(table, 53, TPM2_ACPI_INVALID_LENGTH);
	make_table(table, 79, 0, 0x1000, TPM2_ACPI_START_METHOD_CRB);
	assert_failure_unchanged(table, 79, TPM2_ACPI_INVALID_LENGTH);
	make_table(table, 81, 0, 0x1000, TPM2_ACPI_START_METHOD_CRB);
	assert_failure_unchanged(table, 81, TPM2_ACPI_INVALID_LENGTH);
}

static void
test_tpm2_field_failures(void)
{
	uint8_t table[FIXTURE_CAPACITY];
	size_t length;

	length = make_table(table, TPM2_ACPI_MIN_TABLE_SIZE, 0,
	    0x1000, TPM2_ACPI_START_METHOD_CRB);
	write_le16(table + 38, 1);
	set_checksum(table, length);
	assert_failure_unchanged(table, length, TPM2_ACPI_INVALID_RESERVED);

	make_table(table, TPM2_ACPI_MIN_TABLE_SIZE, 2,
	    0x1000, TPM2_ACPI_START_METHOD_CRB);
	assert_failure_unchanged(table, TPM2_ACPI_MIN_TABLE_SIZE,
	    TPM2_ACPI_INVALID_PLATFORM_CLASS);

	for (uint32_t method = 0; method <= 16; method++) {
		if (method == TPM2_ACPI_START_METHOD_CRB)
			continue;
		make_table(table, TPM2_ACPI_MIN_TABLE_SIZE, 0, 0x1000, method);
		assert_failure_unchanged(table, TPM2_ACPI_MIN_TABLE_SIZE,
		    TPM2_ACPI_UNSUPPORTED_START_METHOD);
	}
	make_table(table, TPM2_ACPI_MIN_TABLE_SIZE, 0, 0x1000, UINT32_MAX);
	assert_failure_unchanged(table, TPM2_ACPI_MIN_TABLE_SIZE,
	    TPM2_ACPI_UNSUPPORTED_START_METHOD);
	/* Do not interpret parameter layouts for unsupported start methods. */
	make_table(table, 64, 0, 0x1000, 11);
	assert_failure_unchanged(table, 64,
	    TPM2_ACPI_UNSUPPORTED_START_METHOD);

	make_table(table, TPM2_ACPI_MIN_TABLE_SIZE, 0, 0,
	    TPM2_ACPI_START_METHOD_CRB);
	assert_failure_unchanged(table, TPM2_ACPI_MIN_TABLE_SIZE,
	    TPM2_ACPI_INVALID_ADDRESS);
	make_table(table, TPM2_ACPI_MIN_TABLE_SIZE, 0, 0x1001,
	    TPM2_ACPI_START_METHOD_CRB);
	assert_failure_unchanged(table, TPM2_ACPI_MIN_TABLE_SIZE,
	    TPM2_ACPI_INVALID_ADDRESS);
	make_table(table, TPM2_ACPI_MIN_TABLE_SIZE, 0, UINT64_MAX - 3U,
	    TPM2_ACPI_START_METHOD_CRB);
	assert_failure_unchanged(table, TPM2_ACPI_MIN_TABLE_SIZE,
	    TPM2_ACPI_INVALID_ADDRESS);

	length = make_table(table, TPM2_ACPI_LOG_TABLE_SIZE, 0, 0x1000,
	    TPM2_ACPI_START_METHOD_CRB);
	table[52] = 1;
	set_checksum(table, length);
	assert_failure_unchanged(table, length,
	    TPM2_ACPI_INVALID_START_PARAMETERS);

	make_table(table, TPM2_ACPI_LOG_TABLE_SIZE, 0, 0x1000,
	    TPM2_ACPI_START_METHOD_CRB);
	write_le32(table + 68, 16);
	write_le64(table + 72, 0);
	set_checksum(table, length);
	assert_failure_unchanged(table, length, TPM2_ACPI_INVALID_EVENT_LOG);

	make_table(table, TPM2_ACPI_LOG_TABLE_SIZE, 0, 0x1000,
	    TPM2_ACPI_START_METHOD_CRB);
	write_le32(table + 68, 3);
	write_le64(table + 72, UINT64_MAX - 1U);
	set_checksum(table, length);
	assert_failure_unchanged(table, length, TPM2_ACPI_INVALID_EVENT_LOG);
}

static void
test_repeated_and_deterministic_parse(void)
{
	uint8_t table[FIXTURE_CAPACITY];
	struct tpm2_acpi_info first;
	struct tpm2_acpi_info second;

	make_table(table, TPM2_ACPI_MIN_TABLE_SIZE, 1, 0xFED40040ULL,
	    TPM2_ACPI_START_METHOD_CRB);
	memset(&first, 0, sizeof(first));
	memset(&second, 0xff, sizeof(second));
	assert(tpm2_acpi_parse(table, TPM2_ACPI_MIN_TABLE_SIZE, &first) ==
	    TPM2_ACPI_OK);
	assert(tpm2_acpi_parse(table, TPM2_ACPI_MIN_TABLE_SIZE, &second) ==
	    TPM2_ACPI_OK);
	assert(first.revision == second.revision);
	assert(first.platform_class == second.platform_class);
	assert(first.start_method == second.start_method);
	assert(first.table_length == second.table_length);
	assert(first.control_area_address == second.control_area_address);
	assert(first.event_log_present == second.event_log_present);
	assert(first.event_log_minimum_length == second.event_log_minimum_length);
	assert(first.event_log_address == second.event_log_address);
}

int
main(void)
{
	test_valid_minimum_and_unaligned();
	test_valid_extended_log_and_limits();
	test_header_failures();
	test_tpm2_field_failures();
	test_repeated_and_deterministic_parse();
	puts("tpm2_acpi_test: all checks passed");
	return 0;
}
