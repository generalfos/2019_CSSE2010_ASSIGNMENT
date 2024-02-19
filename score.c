/*
 * score.c
 *
 * Written by Peter Sutton
 */

#include "score.h"

uint32_t score;
uint32_t highscore;

void init_score(void) {
	score = 0;
}

void add_to_score(uint16_t value) {
	score += value;
}

uint32_t get_score(void) {
	return score;
}

uint32_t get_highscore(void) {
	return highscore;
}

void update_highscore(void) {
	if (score > highscore) {
		highscore = score;
	}
}
