#include "cards.h"
#include "cardart.h"

#define UNKNOWN 13

void card_table(fn_u8 rank, fn_u8 suit, fn_u16 rows[CARD_H])
{
    fn_u8 y;
    const fn_u16 *top, *mid, *bottom;

    if (rank >= UNKNOWN || suit >= 4) {
        top = artBack;
        mid = artBack;
        bottom = artBack;
    } else {
        top = artRank[rank];
        mid = artMiddle;
        bottom = artSuit[suit];
    }
    rows[0] = artEdge;
    for (y = 0; y < ART_PIECE_H; y++) {
        rows[1 + y] = top[y];
        rows[1 + ART_PIECE_H + y] = mid[y];
        rows[1 + 2 * ART_PIECE_H + y] = bottom[y];
    }
    rows[CARD_H - 1] = artEdge;
}

void card_mini(fn_u8 rank, fn_u8 suit, fn_u16 rows[MINI_H])
{
    fn_u8 y;

    if (rank >= UNKNOWN || suit >= 4) {
        for (y = 0; y < MINI_H; y++)
            rows[y] = artMiniBack[y];
        return;
    }
    for (y = 0; y < MINI_H; y++)
        rows[y] = artMiniFace[y];
    /* Rank in the top half, two px in from the left edge; suit below. */
    for (y = 0; y < ART_PIECE_H; y++)
        rows[1 + y] |= (fn_u16)artMiniRank[rank][y] << 6;
    for (y = 0; y < ART_MINI_SUIT_H; y++)
        rows[10 + y] |= (fn_u16)artMiniSuit[suit][y] << 5;
}

void card_mini_half(fn_u16 rows[MINI_H])
{
    fn_u8 y;

    for (y = 0; y < MINI_H; y++)
        rows[y] = (fn_u16)artMiniHalf[y] << 8;
}

void card_chip(fn_u16 rows[8])
{
    fn_u8 y;

    for (y = 0; y < 8; y++)
        rows[y] = (fn_u16)artChip[y] << 8;
}

void card_suit(fn_u8 suit, fn_u16 rows[6])
{
    fn_u8 y;

    for (y = 0; y < ART_MINI_SUIT_H; y++)
        rows[y] = suit < 4 ? (fn_u16)artMiniSuit[suit][y] << 8 : 0;
}
