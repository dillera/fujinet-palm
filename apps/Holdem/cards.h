/* Card pictures, composed from the converted art in cardart.h.
 * Portable C89, so the desktop test can check them too. */
#ifndef CARDS_H
#define CARDS_H

#include "fn_types.h"

/* A table card: 14 x 26, padded to 16 px of black felt, as on the Apple II. */
#define CARD_W 14
#define CARD_H 26
/* A mini card, for the other players' hands: 10 x 17, black on white. */
#define MINI_W 10
#define MINI_H 17
/* The back's edge peeking out from under a second mini card. */
#define MINI_HALF_W 5

/* rank 0..12 is 2..A and suit 0..3 is c, d, h, s, as hs_card gives them;
 * a rank or suit of 13 (HS_UNKNOWN) draws the back. */
void card_table(fn_u8 rank, fn_u8 suit, fn_u16 rows[CARD_H]);
void card_mini(fn_u8 rank, fn_u8 suit, fn_u16 rows[MINI_H]);
/* The left 5 px of a back, in the high bits. */
void card_mini_half(fn_u16 rows[MINI_H]);
/* The Apple II chip, 7 x 8, in the high bits. */
void card_chip(fn_u16 rows[8]);
/* A 5 x 6 suit, in the high bits. */
void card_suit(fn_u8 suit, fn_u16 rows[6]);

#endif /* CARDS_H */
