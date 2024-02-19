/*
** game.c
**
** Author: Peter Sutton
**
*/

#include "game.h"
#include "score.h"
#include <stdio.h>
#include "ledmatrix.h"
#include "terminalio.h"
#include "line_drawing_characters.h"
#include "pixel_colour.h"
#include <avr/pgmspace.h>
#include <stdlib.h>
#include "../../PacManProjectFiles/score.h"
#include "timer0.h"

/* Stdlib needed for random() - random number generator */

///////////////////////////////////////////////////////////
// Initial game field
// The string below has 31 elements for each of the 31 rows. The index into 
// the string is row_number * 31 + column_number.
// Each location is one of the following values:
// (space) - nothing at this location
// - - horizontal wall at this location - uses LINE_HORIZONTAL
// | - vertical wall at this location - uses LINE_VERTICAL
// F - wall is down and to the right - uses LINE_DOWN_AND_RIGHT
// 7 - wall is down and to the left - uses LINE_DOWN_AND_LEFT
// L - wall is up and to the right - uses LINE_UP_AND_RIGHT
// J - wall is up and to the left - uses LINE_UP_AND_LEFT
// > - wall is vertical and to the right - uses LINE_VERTICAL_AND_RIGHT
// < - wall is vertical and to the left - uses LINE_VERTICAL_AND_LEFT
// ^ - wall is horizontal and up - uses LINE_HORIZONTAL_AND_UP
// v - wall is horizontal and down - uses LINE_HORIZONTAL_AND_DOWN
// + - wall is in all directions - uses LINE_VERTICAL_AND_HORIZONTAL
// . - pacdot initially at this location
// P - power pellet initial location (initially implemented just as a pac-dot)
//
// This array is stored in program memory to preserve RAM. (1 is added to 
// size to allow for null character at end of string.)
// (Note that string constants with whitespace between them are concatenated.)

static const char init_game_field[FIELD_HEIGHT*FIELD_WIDTH + 1] PROGMEM =
	"F-------------v-v-------------7"
	"|.............| |.............|"
	"|.F---7.F---7.| |.F---7.F---7.|"
	"|.|   |.L---J.L-J.L---J.|   |.|"
	"|.|   |.................|   |.|"
	"|.|   |.F---7.F-7.F---7.|   |.|"
	"|PL---J.L---J.L-J.L---J.L---JP|"
	"|.............................|"
	"|.F---7.F7.F-------7.F7.F---7.|"
	"|.L---J.||.L--7 F--J.||.L---J.|"
	"|.......||....| |....||.......|"
	"L-----7.|L--7 | | F--J|.F-----J"
	"      |.|F--J L-J L--7|.|      "
	"      |.||           ||.|      "
	"------J.LJ F--   --7 LJ.L------"
	"       .   |       |   .       "
	"------7.F7 L-------J F7.F------"
	"      |.||           ||.|      "
	"      |.|| F-------7 ||.|      "
	"F-----J.LJ L--7 F--J LJ.L-----7"
	"|.............| |.............|"
	"|.F---7.F---7.| |.F---7.F---7.|"
	"|.L-7 |.L---J.L-J.L---J.| F-J.|"
	"|P..| |........ ........| |..P|"
	">-7.| |.F7.F-------7.F7.| |.F-<"
	">-J.L-J.||.L--7 F--J.||.L-J.L-<"
	"|.......||....| |....||.......|"
	"|.F-----JL--7.| |.F--JL-----7.|"
	"|.L---------J.L-J.L---------J.|"
	"|.............................|"
	"L-----------------------------J";

// Array to store the game dots (pacdots) - each element in the array is a 32 bit integer, 
// representing the absence/presence of pacdots in each row. The first element in 
// the array is for row 0 (top), the last for row 30 (bottom).
// Within the 32-bit integer, the least significant 31 bits are used to store
// the data for each column in that row. The least significant bit (bit 0) is the value
// for column 0 (left hand column), the second most significant bit (bit 30) is for
// column 30 (right hand column). The most significant bit (bit 31) is unused. A value
// of 1 in a bit represents the presence of a pacdot, 0 is the absence.
//

// We also keep a count of the number of pac-dots remaining on the game field
static uint16_t num_pacdots;

// Initial pacman location and direction
#define INIT_PACMAN_X 15
#define INIT_PACMAN_Y 23
#define INIT_PACMAN_DIRN DIRN_RIGHT

// Location of the ghost's home - ghosts will be every 2 cells
// starting from the left most position (12,15) to (18,15)
#define GHOST_HOME_Y 15
#define GHOST_HOME_X_LEFT 12
#define GHOST_HOME_X_RIGHT 18
#define GHOST_HOME_ENTRY_Y 14
#define GHOST_HOME_ENTRY_X_LEFT 14
#define GHOST_HOME_ENTRY_X_RIGHT 16
#define INIT_GHOST_DIRN DIRN_RIGHT

// Values to represent the contents of a cell (x,y)
// Non-negative values are used for ghost numbers (0 to 3)
#define CELL_IS_GHOST_HOME (-1)
#define CELL_IS_WALL (-2)
#define CELL_CONTAINS_PACMAN (-3)
#define CELL_CONTAINS_PACDOT (-4)
#define CELL_EMPTY (-5)
#define CELL_CONTAINS_POWER_PELLET (-6)

// Maximum Pacman Lives
#define MAX_LIVES (3)
// Pacman Lives
uint8_t lives = MAX_LIVES;
// LED Display Value 
static int8_t lives_led = 7;

uint8_t power_active = 0;
uint8_t ghost_kills = 0;

uint8_t disabled_ghosts[NUM_GHOSTS] = {
	-1, -1, -1, -1
};

uint8_t ghost_original_colours[NUM_GHOSTS] = {
	BG_RED, BG_GREEN, BG_CYAN, BG_MAGENTA
};

uint8_t ghost_colours[NUM_GHOSTS] = {
	BG_RED, BG_GREEN, BG_CYAN, BG_MAGENTA
};

#define PACMAN_COLOUR (FG_YELLOW)

// Unicode characters used to represent the pacman in each direction
static const char* pacman_characters[NUM_DIRECTION_VALUES] = {
	"\u15E4", "\u15E2", "\u15E7", "\u15E3"
};

// Indicate whether the game is running or not - 1 indicates yes,
// 0 indicates game over
static uint8_t game_running;

///////////////////////////////////////////////////////////
// Private Functions
//
// is_wall_at() returns true (1) if there is a wall at the given 
// game location, 0 otherwise
static int8_t is_wall_at (uint8_t x, uint8_t y) {
	// Get information about any wall in that position
	char wall_character = pgm_read_byte(& init_game_field[y * FIELD_WIDTH + x]);
	return (wall_character != ' ' && wall_character != '.'
			&& wall_character != 'P');
}

void display_score(void) {
	move_cursor(50,20);
	printf_P(PSTR("Score /n %d /n High Score /n %d"), 1.0, "Score", get_score(), "High Score", get_highscore());
}

// is_pacman_at() returns true(1) if the pacman is at the given 
// game location (x,y), 0 otherwise
static int8_t is_pacman_at(uint8_t x, uint8_t y) {
	return (x == pacman_x && y == pacman_y);
}

// is_pacdot_at() returns true (1) if there is a pacdot at the given
// game location, 0 otherwise
static int8_t is_pacdot_at (uint8_t x, uint8_t y) {
	// Get the details for the row
	uint32_t dots_on_row = pacdots[y];
	// Extract the value for the column x (which is in bit x)
	if(dots_on_row & (1UL<< x)) {
		return 1;
	} else {
		return 0;
	}
}

// is_power_pellet_at() returns true (1) if there is a power pellet at the given
// game location, 0 otherwise
static int8_t is_power_pellet_at (uint8_t x, uint8_t y) {
	// Get the details for the row
	uint32_t dots_on_row = power_pellets[y];
	// Extract the value for the column x (which is in bit x)
	if(dots_on_row & (1UL<< x)) {
		return 1;
		} else {
		return 0;
	}
}

// Returns true (1) if the given location is the home of the ghosts
// (this includes the entry to the home of the ghosts)
static int8_t is_ghost_home(uint8_t x, uint8_t y) {
	if(y == GHOST_HOME_Y && x >= GHOST_HOME_X_LEFT && x <= GHOST_HOME_X_RIGHT) {
		return 1;
	} else if(y == GHOST_HOME_ENTRY_Y && x >= GHOST_HOME_ENTRY_X_LEFT
			&& x <= GHOST_HOME_ENTRY_X_RIGHT) {
		return 1;
	} else {
		return 0;
	}
}

static void display_no_dots(void) {
	move_cursor(50,10);
	printf_P(PSTR("Number of Pacdots : %d"), num_pacdots);
}

// The pac-man has just arrived in a location occupied by a pac-dot. Update
// our array which keeps track of remaining pacdots. Update and output the
// count of remaining pac-dots.
// See initialise_pacdots() below for information on how the pacdots array
// is initialised.
static void eat_pacdot(void) {
	// Update Location to Contain No Dot
	pacdots[pacman_y] |= (0UL<<pacman_x);
	// Update Number of Pacdots
	num_pacdots--;
	// Update Current Score
	add_to_score(10);
	display_score();
	display_no_dots();
}

// The pac-man has just arrived in a location occupied by a power pellet. Update
// our array which keeps track of remaining power pellets.
// See initialise_power_pellets() below for information on how the power pellet array
// is initialised.
static void eat_power_pellet(void) {
	if (power_active) {
		ghost_kills = 0;
		disabled_ghosts[0] = -1;
		disabled_ghosts[1] = -1;
		disabled_ghosts[2] = -1;
		disabled_ghosts[3] = -1;
	}
	power_active = 1;
	// Update Location to Contain No Dot
	power_pellets[pacman_y] |= (0UL<<pacman_x);
	// Update Current Score
	add_to_score(50);
	display_score();
	for (int8_t i = 0; i < NUM_GHOSTS; i++) {
		ghost_colours[i] = FG_BLUE;
	}
	powered_period = get_current_time();
}

static void display_lives(void) {
	switch(lives) {
		case 0:
			lives_led = 0;
			break;
		case 1:
			lives_led = 1;
			break;
		case 2:
			lives_led = 3;
			break;
		case 3:
			lives_led = 7;
			break;
	}
	PORTC = lives_led;
}

// what_is_at(x,y) returns
//		CELL_EMPTY, CELL_CONTAINS_PACDOT, CELL_CONTAINS_PACMAN, CELL_IS_WALL,
//		CELL_IS_GHOST_HOME or the ghost number if the cell contains a ghost
static int8_t what_is_at(uint8_t x, uint8_t y) {
	if(is_pacman_at(x,y)) {
		return CELL_CONTAINS_PACMAN;
	} else { // Check for ghosts next - these take priority over dots
		// BUT note that there may be a pacdot at the same location
		for(int8_t i = 0; i < NUM_GHOSTS; i++) {
			if(x == ghost_x[i] && y == ghost_y[i]) {
				return i;	// ghost number
			}
		}
	}
	if (is_pacdot_at(x,y)) {
		return CELL_CONTAINS_PACDOT;
	} else if (is_wall_at(x,y)) {
		return CELL_IS_WALL;
	} else if(is_ghost_home(x,y)) {
		return CELL_IS_GHOST_HOME;
	}
	// If we get here, we haven't found anything else - cell is empty
	return CELL_EMPTY;
}

// what_is_in_dirn(x,y,direction) returns what is in the cell one from
// the cell at (x,y) in the given direction - provided that is not off
// the game field. (If it is, we just indicate that a wall is there.)
// We check for a wall first because this also checks that we're not at the edge
int8_t what_is_in_dirn(uint8_t x, uint8_t y, uint8_t direction) {
	// delta_x and delta_y keep track of the change to the current x,y
	// - we set these based on the direction we're checking in. One of these
	// will end up as -1 or +1, the other will stay at 0.
	int8_t delta_x = 0;
	int8_t delta_y = 0;
	switch(direction) {
		case DIRN_LEFT:
		if(x == 0) {
			// We can't move left since we're at the edge
			return CELL_IS_WALL;
		}
		delta_x = -1;
		break;
		case DIRN_RIGHT:
		if(x == FIELD_WIDTH-1) {
			// We can't move right since we're at the edge
			return CELL_IS_WALL;
		}
		delta_x = 1;
		break;
		case DIRN_UP:
		if(y == 0) {
			// We can't move up since we're at the edge
			return CELL_IS_WALL;
		}
		delta_y = -1;
		break;
		case DIRN_DOWN:
		if(y == FIELD_HEIGHT-1) {
			// We can't move down since we're at the edge
			return CELL_IS_WALL;
		}
		delta_y = 1;
		break;
		default:	// Shouldn't happen - we just return CELL_IS_WALL if
		// the direction given is invalid
		return CELL_IS_WALL;
	}
	return what_is_at(x + delta_x, y + delta_y);
}

// determine_dirns_ghost_can_move_in()
// Returns a number that indicates whether a ghost at the given x,y location
// can move in each direction. The lower 4 bits of the return value will each
// be 0 or 1 - 0 means can't move in the direction, 1 means can move. The bit
// positions are
// - 0 (DIRN_LEFT) (least significant) - left
// - 1 (DIRN_RIGHT) - right
// - 2 (DIRN_UP) - up
// - 3 (DIRN_DOWN) - down
// Movement in the given direction can only happen if the cell is one of
// - the pacman
// - a pacdot
// - empty
// It can not move there if the cell is a ghost or a wall.
// If we're in the ghost home we can move to another cell in the ghost home.
// If we're outside the ghost home we can't move into it.
static int8_t determine_dirns_ghost_can_move_in(uint8_t x, uint8_t y) {
	int8_t return_value = 0;
	int8_t posn_is_in_ghost_home = is_ghost_home(x,y);
	for(int8_t dirn = DIRN_LEFT; dirn <= DIRN_DOWN; dirn++) {
		int8_t adjacent_cell_contents = what_is_in_dirn(x,y,dirn);
		
		if(adjacent_cell_contents < CELL_IS_WALL) {
			// cell is empty or pacdot or pacman
			return_value |= (1 << dirn);
		} else if(posn_is_in_ghost_home && adjacent_cell_contents == CELL_IS_GHOST_HOME) {
			// we're in the ghost home and can move to an empty cell in the ghost home
			return_value |= (1 << dirn);
		} 
	}
	return return_value;
}

// direction_to_pacman() is called for a ghost position and we return a direction
// to move in that will take us closer to the pacman (from DIRN_LEFT to DIRN_DOWN)
// or -1 if we can't move at all. (Note we can only move into cells that are empty
// OR contain a pacdot OR contain the pacman. We can't move into walls or cells 
// that contain ghosts.)
static int8_t direction_to_pacman(uint8_t x, uint8_t y) {
	int8_t delta_x = pacman_x - x;
	int8_t delta_y = pacman_y - y;
	// Work out which direction options are possible
	int8_t dirn_options = determine_dirns_ghost_can_move_in(x, y);
	if(dirn_options == 0) {
		// Can't move
		return -1;
	}
	
	if(abs(delta_x) < abs(delta_y)) {
		// Pacman is further away in y direction - try this direction (up/down) first
		if(delta_y < 0) {
			if(dirn_options & (1 << DIRN_UP)) {
				return DIRN_UP;
			}
			// Can't move up - move on to checking left/right
		} else if(delta_y > 0) {
			if(dirn_options & (1 << DIRN_DOWN)) {
				return DIRN_DOWN;
			}
			// Can't move down - move on to checking left/right
		} // else delta_y is 0 - so try left/right
	}
	// Try the x direction 
	if(delta_x < 0) {
		if(dirn_options & (1 << DIRN_LEFT)) {
			return DIRN_LEFT;
		}
		// Pacman is left but we can't move left - try up or down
		if(delta_y < 0) {
			if(dirn_options & (1 << DIRN_UP)) {
				return DIRN_UP;
			}
		} else if(dirn_options & (1 << DIRN_DOWN)) {
			return DIRN_DOWN;
		}
	} else {
		if(dirn_options & (1 << DIRN_RIGHT)) {
			return DIRN_RIGHT;
		}
		// Pacman is right (or directly above/below) but we can't move right - try up or down
		if(delta_y < 0) {
			if(dirn_options & (1 << DIRN_UP)) {
				return DIRN_UP;
			} 
		} else if(dirn_options & (1 << DIRN_DOWN)) {
			return DIRN_DOWN;
		}
	}
	// Just move whichever way we can - try until we find one that works
	for(int8_t dirn = DIRN_LEFT; dirn <= DIRN_DOWN; dirn++) {
		if(dirn_options & (1 << dirn)) {
			return dirn;
		}
	}
	// We can't move in any direction
	return -1;
}

// determine_ghost_direction_to_move()
// 
// Determine the direction the given ghost (0 to 3) should move in.
// (Each ghost uses a different approach to moving.)
// Return -1 if the ghost can't move (e.g. surrounded by walls and other
// ghosts).
static int8_t determine_ghost_direction_to_move(uint8_t ghostnum) {
	uint8_t x = ghost_x[ghostnum];
	uint8_t y = ghost_y[ghostnum];
	uint8_t curdirn = ghost_direction[ghostnum];

	int8_t dirn_options = determine_dirns_ghost_can_move_in(x,y);
	if(dirn_options == 0) {
		// ghost has no options - indicate that the ghost can't move
		return -1;
	}
	
	if(is_ghost_home(x,y)) {
		// Attempt to move ghost out of home - try UP
		if(dirn_options & (1 << DIRN_UP)) {
			return DIRN_UP;
		}
		// If this doesn't work, we'll try the usual algorithm
	}
	switch(ghostnum) {
		case 0:
			// Ghost 0 will always try to move towards the pacman
			return direction_to_pacman(x, y);
			break;
		case 1:
		case 3:
			// Ghosts 1 and 3 will always try to keep moving in their current
			// direction if possible
			if(dirn_options & (1<<curdirn)) {
				// Current direction is valid - just keep going
				return curdirn;
			} else {
				// Can't move in current direction - try right angles
				int8_t new_dirn = (curdirn + ghostnum)%4;
				if(dirn_options & (1 << new_dirn)) {
					return new_dirn;
				} else {
					// Try the other direction at right angles
					new_dirn = (new_dirn + 2)%4;
					if(dirn_options & (1 << new_dirn)) {
						return new_dirn;
					} else {
						// Neither of the right angles directions worked
						// - just go back in the opposite direction 
						return (curdirn + 2)%4;
					}
				}
			}
			break;	
		case 2:
			// Ghost 2 will try to move in the same direction as the pacman is moving
			if(dirn_options & (1 << pacman_direction)) {
				// That direction is one of the valid options
				return pacman_direction;
			} else {
				// Otherwise, start from a random direction and try each in turn
				int8_t first_direction_to_check = random()%4;
				for(int8_t i = 0; i < 4; i++) {
					int8_t direction_to_check = (first_direction_to_check + i)%4;
					if(dirn_options & (1 << direction_to_check)) {
						return direction_to_check;
					
					}
				}
				// Should never get here because one of the directions should be valid
				// - just indicate that we can't move
				return -1;
			}
	}
	// Should never get here either - just indicate that we can't move
	return -1;	
}


// draw_initial_game_field()
static void draw_initial_game_field(void) {
	clear_terminal();
	normal_display_mode();
	hide_cursor();
	move_cursor(1,1);	// Start at top left
	uint16_t wall_array_index = 0;  // row_number * 31 + column_number, i.e. 31*x+y
	for(uint8_t y = 0; y < FIELD_HEIGHT; y++) {
		for(uint8_t x = 0; x < FIELD_WIDTH; x++) {
			char wall_character = pgm_read_byte(&init_game_field[wall_array_index]);
			switch(wall_character) {
				case '-':	printf("%s", LINE_HORIZONTAL); break;
				case '|':	printf("%s", LINE_VERTICAL); break;
				case 'F':	printf("%s", LINE_DOWN_AND_RIGHT); break;
				case '7':	printf("%s", LINE_DOWN_AND_LEFT); break;
				case 'L':	printf("%s", LINE_UP_AND_RIGHT); break;
				case 'J':	printf("%s", LINE_UP_AND_LEFT); break;
				case '>':	printf("%s", LINE_VERTICAL_AND_RIGHT); break;
				case '<':	printf("%s", LINE_VERTICAL_AND_LEFT); break;
				case '^':	printf("%s", LINE_HORIZONTAL_AND_UP); break;
				case 'v':	printf("%s", LINE_HORIZONTAL_AND_DOWN); break;
				case '+':	printf("%s", LINE_VERTICAL_AND_HORIZONTAL); break;
				case ' ':	printf(" "); break;
				case 'P':	printf("P"); break;	// power-pellet initially just implemented as a pac-dot
				case '.':	printf("."); break;	// pac-dot
				default:	printf("x"); break;	// shouldn't happen but we show an x in case it does
			}
			wall_array_index++;
		}
		printf("\n");
	}
}

static void initialise_pacdots(void) {
	num_pacdots = 0;
	uint16_t wall_array_index = 0;  // row_number * 31 + column_number, i.e. 31*x+y
	for(uint8_t y = 0; y < FIELD_HEIGHT; y++) {
		pacdots[y] = 0;
		for(uint8_t x = 0; x < FIELD_WIDTH; x++) {
			char wall_character = pgm_read_byte(&init_game_field[wall_array_index]);
			if(wall_character == '.') {
				pacdots[y] |= (1UL<<x);
				num_pacdots++;
			}
			wall_array_index++;
		}
	}	
}

static void initialise_power_pellets(void) {
	uint16_t wall_array_index = 0;  // row_number * 31 + column_number, i.e. 31*x+y
	for(uint8_t y = 0; y < FIELD_HEIGHT; y++) {
		power_pellets[y] = 0;
		for(uint8_t x = 0; x < FIELD_WIDTH; x++) {
			char wall_character = pgm_read_byte(&init_game_field[wall_array_index]);
			if(wall_character == 'P') {
				power_pellets[y] |= (1UL<<x);
				set_display_attribute(BG_CYAN);
			}
			wall_array_index++;
		}
	}
}

// Erase the pixel at the given location - presumably because the 
// ghost or the pac-man has moved out of this space. If there is 
// still a pac-dot at this space, we output a dot, otherwise we
// output a space. It is assumed that we are in normal video mode.
static void erase_pixel_at(uint8_t x, uint8_t y) {
	move_cursor(x+1, y+1);
	if(is_pacdot_at(x,y)) {
		printf(".");
 	} else if (is_power_pellet_at(x, y)) {
		printf("P");		 
	} else {
		printf(" ");
	 }
}

// We draw the pac-man at the given location. The character used
// to draw the pac-man is based on the direction it is currently
// facing.
static void draw_pacman_at(uint8_t x, uint8_t y) {
	move_cursor(x+1,y+1);
	set_display_attribute(PACMAN_COLOUR);
	printf("%s", pacman_characters[pacman_direction]);
	normal_display_mode();
}

// ghostnum is assumed to be in the range 0..NUM_GHOSTS-1
// x and y values are assumed to be valid
static void draw_ghost_at(uint8_t ghostnum, uint8_t x, uint8_t y) {
	move_cursor(x+1,y+1);
	// change the background colour to the colour of the given ghost
	set_display_attribute(ghost_colours[ghostnum]);
	// If there is a pac-dot at this location we output a "." otherwise
	// we output a space (which will be shown as a block in reverse video)
	if(is_pacdot_at(x,y)) {
		printf(".");
	} else if (is_power_pellet_at(x, y)) {
		printf("P");
	} else {
		printf(" ");
	}
	// Return to normal display mode to ensure we don't use this
	// background colour for any other printing
	normal_display_mode();
}

/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
// Public Functions
void initialise_game_level(void) {
	draw_initial_game_field();
	initialise_pacdots();
	initialise_power_pellets();
	pacman_x = INIT_PACMAN_X;
	pacman_y = INIT_PACMAN_Y;
	pacman_direction = INIT_PACMAN_DIRN;
	draw_pacman_at(pacman_x, pacman_y);
	for(int8_t i = 0; i < NUM_GHOSTS; i++) {
		ghost_x[i] = GHOST_HOME_X_LEFT + 2*i;
		ghost_y[i] = GHOST_HOME_Y;
		ghost_direction[i] = INIT_GHOST_DIRN;
		draw_ghost_at(i, ghost_x[i], ghost_y[i]);
	}
}

void initialise_game(void) {
	initialise_game_level();
	// Initialise Life Display
	display_lives();
	game_running = 1;
}

void determine_ghost_score(int8_t number) {
	switch(number) {
		case 1:
			add_to_score(200);
			break;
		case 2:
			add_to_score(400);
			break;
		case 3:
			add_to_score(800);
			break;
		case 4:
			add_to_score(1600);
			break;		
	}
	display_score();
}

int8_t move_pacman(void) {
	if(!game_running) {
		// Game is over - do nothing
		return 0;
	}
	// If the pac-man is about to exit through the end of a passage-way
	// then wrap its location around to the other side of the game field
	// YOUR CODE HERE - you may need to alter the code below also
	
	// Work out what is in the direction we want to move
	int8_t cell_contents = what_is_in_dirn(pacman_x, pacman_y, pacman_direction);
	if(cell_contents == CELL_IS_WALL) {
		return 0;	// We can't move - wall is straight ahead
	}
	// We can move - erase the pac-man in the current location
	erase_pixel_at(pacman_x, pacman_y);
	// Update the pac-man location
	if(pacman_direction == DIRN_LEFT) {
		pacman_x--;
		if(pacman_x == 0) {
			pacman_x = FIELD_WIDTH - 1;
		}
	} else if(pacman_direction == DIRN_RIGHT) {
		pacman_x++;
		if(pacman_x == FIELD_WIDTH - 1) {
			pacman_x = 0;
		}
	} else if(pacman_direction == DIRN_UP) {
		pacman_y--;
	} else {
		pacman_y++;
	}
	if(cell_contents >= 0) {
		// We've encountered a ghost - draw both at the location
		// Set the background colour to that of the ghost
		// before we print out the pac-man
		// Note that the variable cell_contents contains the ghost number
		// Lose a life
		if (power_active) {
			ghost_x[cell_contents] = GHOST_HOME_X_LEFT;
			ghost_y[cell_contents] = GHOST_HOME_Y;
			ghost_kills++;
			determine_ghost_score(ghost_kills);
			disabled_ghosts[cell_contents] = cell_contents;
			draw_pacman_at(pacman_x, pacman_y);
		} else {
			lives--;
			// Update Life Display
			display_lives();
			// Check if pacman still has lives
			if(lives > 0) {
				reset_entities_pos();
				} else {
				set_display_attribute(ghost_colours[cell_contents]);
				draw_pacman_at(pacman_x, pacman_y);
				game_running = 0;
			}
		}
	} else {
		if(cell_contents == CELL_CONTAINS_PACDOT) {
			eat_pacdot();
		} else if (cell_contents == CELL_CONTAINS_POWER_PELLET) {
			eat_power_pellet();
		}
	}
	draw_pacman_at(pacman_x, pacman_y);
	return 1;
}

void reset_entities_pos(void) {
	// Reset Pacman
	pacman_x = INIT_PACMAN_X;
	pacman_y = INIT_PACMAN_Y;
	// Reset Ghosts
	for(int8_t i = 0; i < NUM_GHOSTS; i++) {
		ghost_x[i] = GHOST_HOME_X_LEFT;
		ghost_y[i] = GHOST_HOME_Y;
	}
}

int8_t change_pacman_direction(int8_t direction) {
	if(!game_running) {
		// Game is over - do nothing
		return 0;
	}
	// Work out what is in the direction we want to move
	int8_t cell_contents = what_is_in_dirn(pacman_x, pacman_y, direction);
	if(cell_contents == CELL_IS_WALL) {
		// Can't move
		return 0;
	} else {
		pacman_direction = direction;
		// Redraw the pacman so it is facing in the right direction
		draw_pacman_at(pacman_x, pacman_y);
		return 1;
	}
}

void move_ghost(int8_t ghostnum) {
	if(!game_running) {
		// Game is over - do nothing
		return;
	}
	if(disabled_ghosts[ghostnum] == ghostnum) {
		// Ghost has been eaten - do nothing
		return;
	}
	int8_t dirn_to_move = determine_ghost_direction_to_move(ghostnum);
	if(dirn_to_move < 0) {
		// Ghost can't move (e.g. boxed in) - do nothing
		return;
	}
	
	// Erase the ghost from the current location
	erase_pixel_at(ghost_x[ghostnum], ghost_y[ghostnum]);
	
	// Update the ghost's direction (it's possible this may be the same value)
	ghost_direction[ghostnum] = dirn_to_move;
	// Update the ghost's location
	switch(dirn_to_move) {
		case DIRN_LEFT:
			ghost_x[ghostnum]--;
			break;
		case DIRN_RIGHT:
			ghost_x[ghostnum]++;
			break;
		case DIRN_UP:
			ghost_y[ghostnum]--;
			break;
		case DIRN_DOWN:
			ghost_y[ghostnum]++;
			break;
	}
	
	// Check if the pac-man is at this ghost location. 
	if(is_pacman_at(ghost_x[ghostnum], ghost_y[ghostnum])) {
		if (power_active) {
			ghost_x[ghostnum] = GHOST_HOME_X_LEFT;
			ghost_y[ghostnum] = GHOST_HOME_Y;
			ghost_kills++;
			determine_ghost_score(ghost_kills);
			disabled_ghosts[ghostnum] = ghostnum;
		} else {
			// Update Lives
			lives--;
			// Update Life Display
			display_lives();
			// Check if pacman still has lives
			if(lives > 0) {
				reset_entities_pos();
			} else {
			game_running = 0;
			set_display_attribute(ghost_colours[ghostnum]);
			draw_pacman_at(ghost_x[ghostnum], ghost_y[ghostnum]);
			}
		}
	} else {
		draw_ghost_at(ghostnum, ghost_x[ghostnum], ghost_y[ghostnum]);
	}
	normal_display_mode();
}

int8_t is_game_over(void) {
	return !game_running;
}

int8_t is_level_complete(void) {
	return (num_pacdots == 0);
}



