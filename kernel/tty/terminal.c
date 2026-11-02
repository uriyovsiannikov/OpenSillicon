#include "kernel/terminal.h"
#include "kernel/fd.h"

#define TERMINAL_EAGAIN 11
#define KEY_SHIFT_LEFT 0x2aU
#define KEY_SHIFT_RIGHT 0x36U
#define KEY_CONTROL 0x1dU
#define KEY_ALT 0x38U
#define KEY_CAPS_LOCK 0x3aU
#define KEY_F1 0x3bU
#define KEY_BACKSPACE 0x0eU
#define KEY_TAB 0x0fU
#define KEY_ENTER 0x1cU
#define KEY_SPACE 0x39U
#define KEY_ARROW_UP 0x48U
#define KEY_ARROW_LEFT 0x4bU
#define KEY_ARROW_RIGHT 0x4dU
#define KEY_ARROW_DOWN 0x50U
#define KEY_HOME 0x47U
#define KEY_END 0x4fU
#define EOF_TOKEN 256U

enum parser_state {
	PARSER_TEXT,
	PARSER_ESCAPE,
	PARSER_CSI
};

struct terminal_state {
	struct terminal_cell cells[TERMINAL_MAX_ROWS][TERMINAL_MAX_COLUMNS];
	unsigned int column;
	unsigned int row;
	bool wrap_pending;
	bool raw_mode;
	uint8_t foreground;
	uint8_t background;
	enum parser_state parser;
	unsigned int parameters[2];
	unsigned int parameter_count;
	bool parameter_present[2];
	char line[TERMINAL_LINE_CAPACITY];
	size_t line_length;
	uint16_t input[TERMINAL_INPUT_CAPACITY];
	size_t input_head;
	size_t input_count;
};

static struct terminal_state terminals[TERMINAL_COUNT];
static unsigned int terminal_columns;
static unsigned int terminal_rows;
static unsigned int active_terminal;
static terminal_draw_cell draw_callback;
static void *draw_context;
static bool shift_left;
static bool shift_right;
static bool control_down;
static bool alt_down;
static bool caps_lock;

static struct terminal_state *
active_state(void)
{
	return &terminals[active_terminal];
}

static void
draw_cell(unsigned int terminal, unsigned int column, unsigned int row)
{
	if (draw_callback != NULL)
		draw_callback(draw_context, terminal, column, row,
		    terminals[terminal].cells[row][column]);
}

static struct terminal_cell
blank_cell(uint8_t foreground, uint8_t background)
{
	return (struct terminal_cell){
		.character = ' ',
		.foreground = foreground,
		.background = background
	};
}

static void
clear_row(struct terminal_state *terminal, unsigned int row,
	unsigned int first_column)
{
	unsigned int column;

	for (column = first_column; column < terminal_columns; column++) {
		terminal->cells[row][column] = blank_cell(terminal->foreground,
		    terminal->background);
		draw_cell((unsigned int)(terminal - terminals), column, row);
	}
}

static void
scroll(struct terminal_state *terminal)
{
	unsigned int row;
	unsigned int column;

	for (row = 1; row < terminal_rows; row++) {
		for (column = 0; column < terminal_columns; column++) {
			terminal->cells[row - 1][column] =
			    terminal->cells[row][column];
			draw_cell((unsigned int)(terminal - terminals), column,
			    row - 1);
		}
	}
	clear_row(terminal, terminal_rows - 1, 0);
	terminal->row = terminal_rows - 1;
}

static void
line_feed(struct terminal_state *terminal)
{
	terminal->column = 0;
	if (++terminal->row >= terminal_rows)
		scroll(terminal);
}

static void
put_character(struct terminal_state *terminal, uint8_t character)
{
	if (character == '\n') {
		terminal->wrap_pending = false;
		line_feed(terminal);
		return;
	}
	if (character == '\r') {
		terminal->wrap_pending = false;
		terminal->column = 0;
		return;
	}
	if (character == '\b') {
		if (terminal->wrap_pending)
			terminal->wrap_pending = false;
		if (terminal->column != 0)
			terminal->column--;
		else if (terminal->row != 0) {
			terminal->row--;
			terminal->column = terminal_columns - 1;
		}
		terminal->cells[terminal->row][terminal->column] = blank_cell(
		    terminal->foreground, terminal->background);
		draw_cell((unsigned int)(terminal - terminals), terminal->column,
		    terminal->row);
		return;
	}
	if (character == '\t') {
		unsigned int spaces = 8U - terminal->column % 8U;

		while (spaces-- != 0)
			put_character(terminal, ' ');
		return;
	}
	if (character < 0x20 || character > 0x7e)
		return;
	if (terminal->wrap_pending) {
		terminal->wrap_pending = false;
		line_feed(terminal);
	}
	terminal->cells[terminal->row][terminal->column] = (struct terminal_cell){
		.character = character,
		.foreground = terminal->foreground,
		.background = terminal->background
	};
	draw_cell((unsigned int)(terminal - terminals), terminal->column,
	    terminal->row);
	if (terminal->column + 1 >= terminal_columns)
		terminal->wrap_pending = true;
	else
		terminal->column++;
}

static void
clear_screen(struct terminal_state *terminal)
{
	unsigned int row;

	for (row = 0; row < terminal_rows; row++)
		clear_row(terminal, row, 0);
	terminal->column = 0;
	terminal->row = 0;
	terminal->wrap_pending = false;
}

static unsigned int
parameter(const struct terminal_state *terminal, unsigned int index,
	unsigned int default_value)
{
	if (index >= terminal->parameter_count ||
	    !terminal->parameter_present[index] ||
	    terminal->parameters[index] == 0)
		return default_value;
	return terminal->parameters[index];
}

static void
apply_csi(struct terminal_state *terminal, uint8_t final)
{
	unsigned int i;

	switch (final) {
	case 'J':
		if (parameter(terminal, 0, 0) == 2)
			clear_screen(terminal);
		break;
	case 'H':
		terminal->row = parameter(terminal, 0, 1) - 1;
		terminal->column = parameter(terminal, 1, 1) - 1;
		terminal->wrap_pending = false;
		if (terminal->row >= terminal_rows)
			terminal->row = terminal_rows - 1;
		if (terminal->column >= terminal_columns)
			terminal->column = terminal_columns - 1;
		break;
	case 'K':
		if (parameter(terminal, 0, 0) == 0) {
			terminal->wrap_pending = false;
			clear_row(terminal, terminal->row, terminal->column);
		}
		break;
	case 'C':
		terminal->wrap_pending = false;
		terminal->column += parameter(terminal, 0, 1);
		if (terminal->column >= terminal_columns)
			terminal->column = terminal_columns - 1;
		break;
	case 'D':
		terminal->wrap_pending = false;
		if (terminal->column > parameter(terminal, 0, 1))
			terminal->column -= parameter(terminal, 0, 1);
		else
			terminal->column = 0;
		break;
	case 'm':
		for (i = 0; i < terminal->parameter_count; i++) {
			unsigned int value = terminal->parameter_present[i] ?
			    terminal->parameters[i] : 0;

			if (value == 0) {
				terminal->foreground = 7;
				terminal->background = 0;
			} else if (value >= 30 && value <= 37) {
				terminal->foreground = value - 30;
			} else if (value >= 40 && value <= 47) {
				terminal->background = value - 40;
			} else if (value >= 90 && value <= 97) {
				terminal->foreground = value - 90 + 8;
			} else if (value >= 100 && value <= 107) {
				terminal->background = value - 100 + 8;
			}
		}
		break;
	default:
		break;
	}
}

static void
write_byte(struct terminal_state *terminal, uint8_t byte)
{
	if (terminal->parser == PARSER_ESCAPE) {
		terminal->parser = byte == '[' ? PARSER_CSI : PARSER_TEXT;
		terminal->parameter_count = 1;
		terminal->parameters[0] = 0;
		terminal->parameters[1] = 0;
		terminal->parameter_present[0] = false;
		terminal->parameter_present[1] = false;
		return;
	}
	if (terminal->parser == PARSER_CSI) {
		if (byte >= '0' && byte <= '9') {
			unsigned int index = terminal->parameter_count - 1;

			if (index < 2 && terminal->parameters[index] < 1000) {
				terminal->parameters[index] =
				    terminal->parameters[index] * 10 + byte - '0';
				terminal->parameter_present[index] = true;
			}
		} else if (byte == ';' && terminal->parameter_count < 2) {
			terminal->parameter_count++;
		} else if (byte >= 0x40 && byte <= 0x7e) {
			apply_csi(terminal, byte);
			terminal->parser = PARSER_TEXT;
		} else {
			terminal->parser = PARSER_TEXT;
		}
		return;
	}
	if (byte == 0x1b) {
		terminal->parser = PARSER_ESCAPE;
		return;
	}
	put_character(terminal, byte);
}

bool
terminal_init(unsigned int columns, unsigned int rows,
	terminal_draw_cell callback, void *context)
{
	unsigned int terminal;
	unsigned int row;
	unsigned int column;

	if (columns == 0 || rows == 0 || columns > TERMINAL_MAX_COLUMNS ||
	    rows > TERMINAL_MAX_ROWS)
		return false;
	terminal_columns = columns;
	terminal_rows = rows;
	draw_callback = callback;
	draw_context = context;
	active_terminal = 0;
	shift_left = false;
	shift_right = false;
	control_down = false;
	alt_down = false;
	caps_lock = false;
	for (terminal = 0; terminal < TERMINAL_COUNT; terminal++) {
		struct terminal_state *state = &terminals[terminal];

		*state = (struct terminal_state){ .foreground = 7 };
		for (row = 0; row < rows; row++) {
			for (column = 0; column < columns; column++) {
				state->cells[row][column] = blank_cell(7, 0);
				if (terminal == 0)
					draw_cell(terminal, column, row);
			}
		}
	}
	return true;
}

void
terminal_write(const char *buffer, size_t length)
{
	struct terminal_state *terminal = active_state();
	size_t i;

	if (buffer == NULL)
		return;
	for (i = 0; i < length; i++)
		write_byte(terminal, (uint8_t)buffer[i]);
}

static bool
queue_token(struct terminal_state *terminal, uint16_t token)
{
	size_t tail;

	if (terminal->input_count == TERMINAL_INPUT_CAPACITY)
		return false;
	tail = (terminal->input_head + terminal->input_count) %
	    TERMINAL_INPUT_CAPACITY;
	terminal->input[tail] = token;
	terminal->input_count++;
	fd_readiness_wake();
	return true;
}

int
terminal_read(char *buffer, size_t length, size_t *read_length)
{
	struct terminal_state *terminal = active_state();
	size_t count = 0;

	if (read_length == NULL || (buffer == NULL && length != 0))
		return -22;
	*read_length = 0;
	if (length == 0)
		return 0;
	if (terminal->input_count == 0)
		return -TERMINAL_EAGAIN;
	if (terminal->input[terminal->input_head] == EOF_TOKEN) {
		terminal->input_head = (terminal->input_head + 1) %
		    TERMINAL_INPUT_CAPACITY;
		terminal->input_count--;
		return 0;
	}
	while (count < length && terminal->input_count != 0 &&
	    terminal->input[terminal->input_head] != EOF_TOKEN) {
		buffer[count++] = (char)terminal->input[terminal->input_head];
		terminal->input_head = (terminal->input_head + 1) %
		    TERMINAL_INPUT_CAPACITY;
		terminal->input_count--;
	}
	*read_length = count;
	return 0;
}

bool
terminal_input_available(void)
{
	return active_state()->input_count != 0;
}

void
terminal_set_raw_mode(bool enabled)
{
	struct terminal_state *terminal = active_state();
	terminal->raw_mode = enabled;
	terminal->line_length = 0;
	terminal->input_head = 0;
	terminal->input_count = 0;
}

bool
terminal_raw_mode(void)
{
	return active_state()->raw_mode;
}

static void
queue_escape_sequence(struct terminal_state *terminal, char final)
{
	size_t tail;

	if (TERMINAL_INPUT_CAPACITY - terminal->input_count < 3)
		return;
	tail = (terminal->input_head + terminal->input_count) %
	    TERMINAL_INPUT_CAPACITY;
	terminal->input[tail] = 0x1bU;
	terminal->input[(tail + 1) % TERMINAL_INPUT_CAPACITY] = '[';
	terminal->input[(tail + 2) % TERMINAL_INPUT_CAPACITY] =
	    (uint8_t)final;
	terminal->input_count += 3;
}

static uint8_t
translate_key(uint8_t code, bool shifted, bool caps)
{
	static const char plain_digits[] = "1234567890-=";
	static const char shift_digits[] = "!@#$%^&*()_+";
	static const char plain_top[] = "qwertyuiop[]";
	static const char shift_top[] = "QWERTYUIOP{}";
	static const char plain_home[] = "asdfghjkl;'`";
	static const char shift_home[] = "ASDFGHJKL:\"~";
	static const char plain_bottom[] = "\\zxcvbnm,./";
	static const char shift_bottom[] = "|ZXCVBNM<>?";
	const char *plain;
	const char *shift;
	unsigned int index;

	if (code >= 0x02 && code <= 0x0d) {
		index = code - 0x02;
		return (uint8_t)(shifted ? shift_digits[index] : plain_digits[index]);
	}
	if (code >= 0x10 && code <= 0x1b) {
		index = code - 0x10;
		plain = plain_top;
		shift = shift_top;
	} else if (code >= 0x1e && code <= 0x29) {
		index = code - 0x1e;
		plain = plain_home;
		shift = shift_home;
	} else if (code >= 0x2b && code <= 0x35) {
		index = code - 0x2b;
		plain = plain_bottom;
		shift = shift_bottom;
	} else {
		if (code == KEY_SPACE)
			return ' ';
		if (code == KEY_TAB)
			return '\t';
		if (code == KEY_ENTER)
			return '\n';
		if (code == KEY_BACKSPACE)
			return '\b';
		return 0;
	}
	if (plain[index] >= 'a' && plain[index] <= 'z')
		return (uint8_t)((shifted != caps) ? shift[index] : plain[index]);
	return (uint8_t)(shifted ? shift[index] : plain[index]);
}

static void
echo_character(struct terminal_state *terminal, uint8_t character)
{
	write_byte(terminal, character);
}

static bool
submit_line(struct terminal_state *terminal, bool newline)
{
	size_t i;
	size_t required = terminal->line_length + (newline ? 1U : 0U);

	if (required > TERMINAL_INPUT_CAPACITY - terminal->input_count)
		return false;
	for (i = 0; i < terminal->line_length; i++)
		(void)queue_token(terminal, (uint8_t)terminal->line[i]);
	if (newline)
		(void)queue_token(terminal, '\n');
	terminal->line_length = 0;
	return true;
}

void
terminal_key_event(struct terminal_key_event event)
{
	struct terminal_state *terminal = active_state();
	uint8_t code = event.scancode;
	uint8_t character;
	bool shifted;

	if (!event.extended && code == KEY_SHIFT_LEFT) {
		shift_left = event.pressed;
		return;
	}
	if (!event.extended && code == KEY_SHIFT_RIGHT) {
		shift_right = event.pressed;
		return;
	}
	if (event.extended && code == KEY_CONTROL) {
		control_down = event.pressed;
		return;
	}
	if (!event.extended && code == KEY_CONTROL) {
		control_down = event.pressed;
		return;
	}
	if (event.extended && code == KEY_ALT) {
		alt_down = event.pressed;
		return;
	}
	if (!event.extended && code == KEY_ALT) {
		alt_down = event.pressed;
		return;
	}
	if (!event.extended && code == KEY_CAPS_LOCK && event.pressed) {
		caps_lock = !caps_lock;
		return;
	}
	if (!event.pressed)
		return;
	if (alt_down && !event.extended && code >= KEY_F1 &&
	    code < KEY_F1 + TERMINAL_COUNT) {
		(void)terminal_switch(code - KEY_F1);
		return;
	}
	if (event.extended) {
		if (terminal->raw_mode) {
			switch (code) {
			case KEY_ARROW_UP: queue_escape_sequence(terminal, 'A'); break;
			case KEY_ARROW_DOWN: queue_escape_sequence(terminal, 'B'); break;
			case KEY_ARROW_RIGHT: queue_escape_sequence(terminal, 'C'); break;
			case KEY_ARROW_LEFT: queue_escape_sequence(terminal, 'D'); break;
			case KEY_HOME: queue_escape_sequence(terminal, 'H'); break;
			case KEY_END: queue_escape_sequence(terminal, 'F'); break;
			default: break;
			}
		}
		return;
	}
	shifted = shift_left || shift_right;
	character = translate_key(code, shifted, caps_lock);
	if (character == 0)
		return;
	if (control_down && character >= 'a' && character <= 'z')
		character = (uint8_t)(character - 'a' + 1);
	else if (control_down && character >= 'A' && character <= 'Z')
		character = (uint8_t)(character - 'A' + 1);
	if (terminal->raw_mode) {
		(void)queue_token(terminal, character);
		return;
	}
	if (character == 4) {
		if (terminal->line_length == 0) {
			(void)queue_token(terminal, EOF_TOKEN);
		} else {
			(void)submit_line(terminal, false);
		}
		return;
	}
	if (character == '\n') {
		if (submit_line(terminal, true))
			echo_character(terminal, '\n');
		return;
	}
	if (character == '\b') {
		if (terminal->line_length != 0) {
			terminal->line_length--;
			echo_character(terminal, '\b');
		}
		return;
	}
	if (character == '\t' || (character >= 0x20 && character <= 0x7e)) {
		if (terminal->line_length + 1 >= TERMINAL_LINE_CAPACITY)
			return;
		terminal->line[terminal->line_length++] = (char)character;
		echo_character(terminal, character);
	}
}

bool
terminal_switch(unsigned int terminal)
{
	unsigned int row;
	unsigned int column;

	if (terminal >= TERMINAL_COUNT)
		return false;
	active_terminal = terminal;
	if (draw_callback != NULL) {
		for (row = 0; row < terminal_rows; row++) {
			for (column = 0; column < terminal_columns; column++)
				draw_cell(terminal, column, row);
		}
	}
	return true;
}

unsigned int
terminal_active(void)
{
	return active_terminal;
}

struct terminal_cell
terminal_cell_at(unsigned int terminal, unsigned int column,
	unsigned int row)
{
	if (terminal >= TERMINAL_COUNT || column >= terminal_columns ||
	    row >= terminal_rows)
		return (struct terminal_cell){ 0 };
	return terminals[terminal].cells[row][column];
}
