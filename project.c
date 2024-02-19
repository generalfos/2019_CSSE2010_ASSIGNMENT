/*
 * project.c
 *
 * Main file
 *
 * Author: Peter Sutton. Modified by Joel Foster
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <stdio.h>
#include <avr/eeprom.h>

#include "ledmatrix.h"
#include "scrolling_char_display.h"
#include "buttons.h"
#include "serialio.h"
#include "terminalio.h"
#include "score.h"
#include "timer0.h"
#include "game.h"

#define F_CPU 8000000L
#include <util/delay.h>

// Function prototypes - these are defined below (after main()) in the order
// given here
void initialise_hardware(void);
void splash_screen(void);
void new_game(void);
void play_game(void);
void handle_level_complete(void);
void handle_game_over(void);

// ASCII code for Escape character
#define ESCAPE_CHAR 27

uint16_t value;
uint16_t resting_x;
uint16_t resting_y;
uint16_t x;
uint16_t y;
uint8_t x_or_y = 0;	/* 0 = x, 1 = y */
int8_t joystick_rest = 1;
uint32_t special_time_remaining = 0;
/* Seven segment display values */
uint8_t seven_seg[10] = { 63,6,91,79,102,109,125,7,127,111};

// EEPROM Storage
uint8_t  EEMEM IsWritten = 0;
uint16_t EEMEM written_pacman_x;
uint16_t EEMEM written_pacman_y;
uint8_t EEMEM written_pacman_direction;
uint32_t EEMEM written_score;
uint32_t EEMEM written_highscore;
uint8_t EEMEM written_lives;
uint8_t EEMEM written_power_active;
uint8_t EEMEM written_time_remaining;
uint8_t EEMEM written_ghost_x[NUM_GHOSTS];
uint8_t EEMEM written_ghost_y[NUM_GHOSTS];
uint8_t EEMEM written_ghost_direction[NUM_GHOSTS];
uint32_t EEMEM written_pacdots[FIELD_HEIGHT];
uint32_t EEMEM written_power_pellets[FIELD_HEIGHT];

/////////////////////////////// main //////////////////////////////////
int main(void) {
	// Setup hardware and call backs. This will turn on 
	// interrupts.
	initialise_hardware();
	
	// Show the splash screen message. Returns when display
	// is complete
	splash_screen();
	
	DDRC = 0xFF;
	DDRA = 0xF0;
	
	while(1) {
		new_game();
		play_game();
		handle_game_over();
	}
}

void save_data_available(void) {
	move_cursor(35, 5);
	if (eeprom_read_byte(&IsWritten)) {
		printf_P(PSTR("Save Data Available"));
	} else {
		printf_P(PSTR("Save Data Not Available"));
	}
}

void initialise_hardware(void) {
	ledmatrix_setup();
	init_button_interrupts();
	// Setup serial port for 19200 baud communication with no echo
	// of incoming characters
	init_serial_stdio(19200,0);
	
	init_timer0();
	
	// Turn on global interrupts
	sei();
}

void splash_screen(void) {
	// Clear terminal screen and output a message
	clear_terminal();
	move_cursor(10,10);
	printf_P(PSTR("Pac-Man"));
	move_cursor(10,12);
	printf_P(PSTR("CSSE2010/7201 project by Joel Foster - 45820384"));

	// Output the scrolling message to the LED matrix
	// and wait for a push button to be pushed.
	ledmatrix_clear();
	while(1) {
		set_scrolling_display_text("PACMAN 45820384", COLOUR_YELLOW);
		// Scroll the message until it has scrolled off the 
		// display or a button is pushed
		while(scroll_display()) {
			_delay_ms(150);
			if(button_pushed() != NO_BUTTON_PUSHED) {
				ledmatrix_clear();
				return;
			}
		}
	}
}

void get_resting_voltage(void) {
	// Set the ADC mux to choose ADC0 if x_or_y is 0, ADC1 if x_or_y is 1
	if(x_or_y == 0) {
		ADMUX &= ~1;
		} else {
		ADMUX |= 1;
	}
	// Start the ADC conversion
	ADCSRA |= (1<<ADSC);
	
	while(ADCSRA & (1<<ADSC)) {
		; /* Wait until conversion finished */
	}
	value = ADC; // read the value
	if(x_or_y == 0) {
		resting_x = value;
		} else {
		resting_y = value;
	}
	// Next time through the loop, do the other direction
	x_or_y ^= 1;
}

void new_game(void) {
	// Initialise the game and display
	initialise_game();
	
	// Initialise the score
	init_score();
	
	// Get Joystick Resting Voltages
	get_resting_voltage();
	get_resting_voltage();
	
	// Clear a button push or serial input if any are waiting
	// (The cast to void means the return value is ignored.)
	(void)button_pushed();
	clear_serial_input_buffer();
}

void valid_direction(void) {
	int8_t cell_contents;
	joystick_rest = 0;
	if(x > resting_x) {
		cell_contents = what_is_in_dirn(pacman_x, pacman_y, DIRN_RIGHT);
		if(cell_contents == CELL_IS_WALL) {
			;	// Invalid Direction Do Nothing
		} else {
			change_pacman_direction(DIRN_RIGHT);
		}
	} else if (x < resting_x) {
		cell_contents = what_is_in_dirn(pacman_x, pacman_y, DIRN_LEFT);
		if(cell_contents == CELL_IS_WALL) {
			;	// Invalid Direction Do Nothing
		} else {
			change_pacman_direction(DIRN_LEFT);
		}
	} else {
		if(y > resting_y) {
			cell_contents = what_is_in_dirn(pacman_x, pacman_y, DIRN_UP);
			if(cell_contents == CELL_IS_WALL) {
				;	// Invalid Direction Do Nothing
			} else {
				change_pacman_direction(DIRN_RIGHT);
			}
		} else if (y < resting_y) {
			cell_contents = what_is_in_dirn(pacman_x, pacman_y, DIRN_DOWN);
			if(cell_contents == CELL_IS_WALL) {
				;	// Invalid Direction Do Nothing
			} else {
				change_pacman_direction(DIRN_DOWN);
			}
		} else {
			joystick_rest = 1;
			; // Do Nothing - Joystick in Resting Position
		}
	}
}

void save(void) {
	eeprom_update_byte(&IsWritten, 1);
	eeprom_update_word(&written_pacman_x, pacman_x);
	eeprom_update_word(&written_pacman_y, pacman_y);
	eeprom_update_byte(&written_pacman_direction, pacman_direction);
	eeprom_update_byte(&written_lives, lives);
	eeprom_update_byte(&written_power_active, power_active);
	eeprom_update_byte(&written_time_remaining, special_time_remaining);
	eeprom_update_dword(&written_score, score);
	eeprom_update_dword(&written_highscore, highscore);
	eeprom_write_block(ghost_x, &written_ghost_x, NUM_GHOSTS);
	eeprom_write_block(ghost_y, &written_ghost_y, NUM_GHOSTS);
	eeprom_write_block(ghost_direction, &written_ghost_direction, NUM_GHOSTS);
	eeprom_write_block(pacdots, &written_pacdots, FIELD_HEIGHT);
	eeprom_write_block(power_pellets, &written_power_pellets, FIELD_HEIGHT);
}

void load(void) {
	if (eeprom_read_byte(&IsWritten) == 1) {
		pacman_x = eeprom_read_word(&written_pacman_x);
		pacman_y = eeprom_read_word(&written_pacman_y);
		pacman_direction = eeprom_read_byte(&written_pacman_direction);
		lives = eeprom_read_byte(&written_lives);
		power_active = eeprom_read_byte(&written_power_active);
		special_time_remaining = eeprom_read_byte(&written_time_remaining);
		score = eeprom_read_dword(&written_score);
		highscore = eeprom_read_dword(&written_highscore);
				
		eeprom_read_block(ghost_x, &written_ghost_x, NUM_GHOSTS);
		eeprom_read_block(ghost_y, &written_ghost_y, NUM_GHOSTS);
		eeprom_read_block(ghost_direction, &written_ghost_direction, NUM_GHOSTS);
		eeprom_read_block(pacdots, &written_pacdots, FIELD_HEIGHT);
		eeprom_read_block(power_pellets, &written_power_pellets, FIELD_HEIGHT);
	}
	// Otherwise - do nothing - memory has not been initialised
}

int8_t process_serial_input(void) {
	char serial_input, escape_sequence_char;
	serial_input = 'a';
	if(escape_sequence_char == 'a') {
		;
	}
	uint8_t characters_into_escape_sequence = 0;
	// Receive Serial Input
	if(serial_input_available()) {
		// Serial data was available - read the data from standard input
		serial_input = fgetc(stdin);
		// Check if the character is part of an escape sequence
		if(characters_into_escape_sequence == 0 && serial_input == ESCAPE_CHAR) {
			// We've hit the first character in an escape sequence (escape)
			characters_into_escape_sequence++;
			serial_input = -1; // Don't further process this character
			} else if(characters_into_escape_sequence == 1 && serial_input == '[') {
			// We've hit the second character in an escape sequence
			characters_into_escape_sequence++;
			serial_input = -1; // Don't further process this character
			} else if(characters_into_escape_sequence == 2) {
			// Third (and last) character in the escape sequence
			escape_sequence_char = serial_input;
			serial_input = -1;  // Don't further process this character - we
			// deal with it as part of the escape sequence
			characters_into_escape_sequence = 0;
			} else {
			// Character was not part of an escape sequence (or we received
			// an invalid second character in the sequence). We'll process
			// the data in the serial_input variable.
			characters_into_escape_sequence = 0;
		}
	}
	// Process the input.
	if(serial_input == 'p' || serial_input == 'P') {
		// Pause the game - pause/unpause the game until 'p' or 'P' is
		// pressed again
		return 0;
		} else if(serial_input == 'n' || serial_input == 'N') {
		// Start a new game
		new_game();
		} else if(serial_input == 's' || serial_input == 'S') {
		// Save the game
		save();
		} else if(serial_input == 'o' || serial_input == 'O') {
		load();
	}
	
	return 1;
}

void display_digit(uint8_t number, uint8_t digit)
{
	uint8_t ret = seven_seg[number];
	uint8_t high = ret & 0xF0;
	uint8_t low = ret & 0x0F;
	
	PORTD |= low;
	PORTC |= high;
	PORTD |= (digit << PORTD3);
}

void play_game(void) {
	uint8_t digit = 0;
	uint8_t f_digit;
	uint8_t l_digit;
	uint32_t current_time;
	uint32_t pacman_last_move_time;
	uint32_t ghost0_last_move_time;
	uint32_t ghost1_last_move_time;
	uint32_t ghost2_last_move_time;
	uint32_t ghost3_last_move_time;
	int8_t button;
	char serial_input, escape_sequence_char;
	uint8_t characters_into_escape_sequence = 0;
	int8_t paused;
	
	// Get the current time and remember this as the last time the projectiles
    // were moved.
	current_time = get_current_time();
	pacman_last_move_time = current_time;
	ghost0_last_move_time = current_time;
	ghost1_last_move_time = current_time;
	ghost2_last_move_time = current_time;
	ghost3_last_move_time = current_time;
	
	// We play the game until it's over
	while(!is_game_over()) {
		// Check for input - which could be a button push or serial input.
		// Serial input may be part of an escape sequence, e.g. ESC [ D
		// is a left cursor key press. At most one of the following three
		// variables will be set to a value other than -1 if input is available.
		// (We don't initalise button to -1 since button_pushed() will return -1
		// if no button pushes are waiting to be returned.)
		// Button pushes take priority over serial input. If there are both then
		// we'll retrieve the serial input the next time through this loop
		update_highscore();
			
			if (power_active) {
				if (powered_period + 15 < current_time) {
					power_active = 0;
					ghost_colours[0] = ghost_original_colours[0];
					ghost_colours[1] = ghost_original_colours[1];
					ghost_colours[2] = ghost_original_colours[2];
					ghost_colours[3] = ghost_original_colours[3];
					ghost_kills = 0;
					special_time_remaining = 0;
					PORTC &= 0x0F;
					PORTD &= 0xF0;
					} else {
					special_time_remaining = ceil(15 - (current_time - powered_period));
					f_digit = special_time_remaining % 10;
					l_digit = (special_time_remaining / 10) % 10;
					/* Write out seven segment display value to port A */
					if (l_digit == 0) {
						digit = 0;
						display_digit(f_digit, digit);
						} else {
						if(digit == 0) {
							display_digit(f_digit, digit);
							} else {
							display_digit(l_digit, digit);
						}
						digit = 1 - digit;
					}
				}

			}
		
		serial_input = -1;
		escape_sequence_char = -1;
		button = button_pushed();
		
		// Set the ADC mux to choose ADC0 if x_or_y is 0, ADC1 if x_or_y is 1
		if(x_or_y == 0) {
			ADMUX &= ~1;
			} else {
			ADMUX |= 1;
		}
		// Start the ADC conversion
		ADCSRA |= (1<<ADSC);
		
		while(ADCSRA & (1<<ADSC)) {
			; /* Wait until conversion finished */
		}
		value = ADC; // read the value
		if(x_or_y == 0) {
			x = value;
		} else {
			y = value;
		}
		// Next time through the loop, do the other direction
		x_or_y ^= 1;
		
		// Check if joystick is in valid direction
		valid_direction();
		
		if(button == NO_BUTTON_PUSHED) {
			// No push button was pushed, see if there is any serial input
			if(serial_input_available()) {
				// Serial data was available - read the data from standard input
				serial_input = fgetc(stdin);
				// Check if the character is part of an escape sequence
				if(characters_into_escape_sequence == 0 && serial_input == ESCAPE_CHAR) {
					// We've hit the first character in an escape sequence (escape)
					characters_into_escape_sequence++;
					serial_input = -1; // Don't further process this character
				} else if(characters_into_escape_sequence == 1 && serial_input == '[') {
					// We've hit the second character in an escape sequence
					characters_into_escape_sequence++;
					serial_input = -1; // Don't further process this character
				} else if(characters_into_escape_sequence == 2) {
					// Third (and last) character in the escape sequence
					escape_sequence_char = serial_input;
					serial_input = -1;  // Don't further process this character - we
										// deal with it as part of the escape sequence
					characters_into_escape_sequence = 0;
				} else {
					// Character was not part of an escape sequence (or we received
					// an invalid second character in the sequence). We'll process 
					// the data in the serial_input variable.
					characters_into_escape_sequence = 0;
				}
			}
		}
		
		// Process the input. 
		if((button==3 || escape_sequence_char=='D') && joystick_rest) {
			// Button 3 pressed OR left cursor key escape sequence completed 
			// Attempt to move left
			change_pacman_direction(DIRN_LEFT);
		} else if((button==2 || escape_sequence_char=='A') && joystick_rest) {
			// Button 2 pressed or up cursor key escape sequence completed
			// YOUR CODE HERE
			change_pacman_direction(DIRN_UP);
		} else if((button==1 || escape_sequence_char=='B') && joystick_rest) {
			// Button 1 pressed OR down cursor key escape sequence completed
			// YOUR CODE HERE
			change_pacman_direction(DIRN_DOWN);
		} else if((button==0 || escape_sequence_char=='C') && joystick_rest) {
			// Button 0 pressed OR right cursor key escape sequence completed 
			// Attempt to move right
			change_pacman_direction(DIRN_RIGHT);
		} else if(serial_input == 'n' || serial_input == 'N') {
			// Start a new game
			new_game();
		} else if(serial_input == 'p' || serial_input == 'P') {
			// Pause the game
			paused = 1;
			while (paused) {
				paused = process_serial_input();
				pacman_last_move_time = ghost0_last_move_time = ghost1_last_move_time = ghost2_last_move_time = ghost3_last_move_time = get_current_time();
			}
		} else if(serial_input == 's' || serial_input == 'S') {
		// Save the game
		save();
		} else if(serial_input == 'o' || serial_input == 'O') {
		// Load the game
		load();
		}
		
		// else - invalid input or we're part way through an escape sequence -
		// do nothing
		current_time = get_current_time();
		if(!is_game_over() && current_time >= pacman_last_move_time + 400) {
			// 400ms (0.4 second) has passed since the last time we moved 
			// the pac-man - move it.
			move_pacman();
			pacman_last_move_time = current_time;
			// Check if the move finished the level - and restart if so
			if(is_level_complete()) {
				handle_level_complete();	// This will pause until a button is pushed
				initialise_game_level();
				// Update our timers since we have a pause above
				pacman_last_move_time = ghost0_last_move_time = ghost1_last_move_time = ghost2_last_move_time = ghost3_last_move_time = get_current_time();
			}
		}
		// Ghost 0 Movement (0.4 seconds)
		if(!is_game_over() && current_time >= ghost0_last_move_time + 400) {
			// 400ms (0.4 second) has passed since the last time we moved the
			// ghosts - move them
			move_ghost(0);
			ghost0_last_move_time = current_time;
		}
		// Ghost 1 Movement (0.5 seconds)
		if(!is_game_over() && current_time >= ghost1_last_move_time + 500) {
			// 500ms (0.5 second) has passed since the last time we moved the
			// ghosts - move them
			move_ghost(1);
			ghost1_last_move_time = current_time;
		}
		// Ghost 2 Movement (0.55 seconds)
		if(!is_game_over() && current_time >= ghost2_last_move_time + 550) {
			// 550ms (0.55 second) has passed since the last time we moved the
			// ghosts - move them
			move_ghost(2);
			ghost2_last_move_time = current_time;
		}
		// Ghost 3 Movement (0.6 seconds)
		if(!is_game_over() && current_time >= ghost3_last_move_time + 600) {
			// 600ms (0.6 second) has passed since the last time we moved the
			// ghosts - move them
			move_ghost(3);
			ghost3_last_move_time = current_time;
		}
		// We get here if the game is over.
		}
	}
	
void handle_level_complete(void) {
	move_cursor(35,10);
	printf_P(PSTR("Level complete"));
	move_cursor(35,11);
	printf_P(PSTR("Push a button or key to continue"));
	// Clear any characters in the serial input buffer - to make
	// sure we only use key presses from now on.
	clear_serial_input_buffer();
	while(button_pushed() == NO_BUTTON_PUSHED && !serial_input_available()) {
		; // wait
	}
	// Throw away any characters in the serial input buffer
	clear_serial_input_buffer();

}

void handle_game_over(void) {
	move_cursor(35,14);
	printf_P(PSTR("GAME OVER"));
	move_cursor(35,16);
	printf_P(PSTR("Press a button to start again"));
	while(button_pushed() == NO_BUTTON_PUSHED) {
		; // wait
	}
	new_game();
}
