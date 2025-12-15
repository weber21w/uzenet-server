#ifndef UZENET_LICHESS_H
#define UZENET_LICHESS_H

#include <stdint.h>
#include <string.h>

typedef uint8_t	 u8;
typedef uint16_t u16;
typedef uint32_t u32;

/* Message directions:
 * CL_* = Uzebox client -> server
 * SV_* = server -> Uzebox client
 */

/* --------------------------------------------------------------------- */
/* Message types                                                         */
/* --------------------------------------------------------------------- */

enum{
	/* Client -> Server */
	LCH_CL_HELLO        = 0x01,
	LCH_CL_NEW_GAME     = 0x02,
	LCH_CL_MOVE         = 0x03,
	LCH_CL_RESIGN       = 0x04,
	LCH_CL_ABORT        = 0x05,
	LCH_CL_PING         = 0x06,
	LCH_CL_CHAT         = 0x07,
	LCH_CL_REQ_MOVES    = 0x08,
	LCH_CL_REQ_CHAT     = 0x09,

	/* Server -> Client */
	LCH_SV_HELLO        = 0x81,
	LCH_SV_GAME_START   = 0x82,
	LCH_SV_OPP_MOVE     = 0x83,
	LCH_SV_GAME_END     = 0x84,
	LCH_SV_ERROR        = 0x85,
	LCH_SV_PONG         = 0x86,
	LCH_SV_INFO         = 0x87,
	LCH_SV_CLOCK        = 0x88,
	LCH_SV_CHAT         = 0x89,
};

/* Flags for NEW_GAME */
#define LCH_FLAG_RATED		0x01

#define LCH_COLOR_MASK		0x06
#define LCH_COLOR_AUTO		0x00
#define LCH_COLOR_WHITE		0x02
#define LCH_COLOR_BLACK		0x04

/* Promotion piece types */
enum{
	LCH_PROMO_NONE = 0,
	LCH_PROMO_Q    = 1,
	LCH_PROMO_R    = 2,
	LCH_PROMO_B    = 3,
	LCH_PROMO_N    = 4
};

/* Side identifiers */
enum{
	LCH_SIDE_WHITE = 0,
	LCH_SIDE_BLACK = 1
};

/* Results from client point of view */
enum{
	LCH_RESULT_UNKNOWN  = 0,
	LCH_RESULT_WIN      = 1,
	LCH_RESULT_LOSS     = 2,
	LCH_RESULT_DRAW     = 3,
	LCH_RESULT_ABORTED  = 4
};

/* Reasons for game end */
enum{
	LCH_REASON_NONE          = 0,
	LCH_REASON_CHECKMATE     = 1,
	LCH_REASON_RESIGN        = 2,
	LCH_REASON_TIMEOUT       = 3,
	LCH_REASON_STALEMATE     = 4,
	LCH_REASON_AGREED_DRAW   = 5,
	LCH_REASON_REPETITION    = 6,
	LCH_REASON_50MOVE        = 7,
	LCH_REASON_MATERIAL      = 8,
	LCH_REASON_ABORTED       = 9,
	LCH_REASON_SERVER        = 10
};

/* Error codes */
enum{
	LCH_ERR_GENERIC       = 1,
	LCH_ERR_NO_TOKEN      = 2,
	LCH_ERR_LICHESS_HTTP  = 3,
	LCH_ERR_ALREADY_GAME  = 4,
	LCH_ERR_NO_GAME       = 5,
	LCH_ERR_BAD_MOVE      = 6,
	LCH_ERR_REMOTE_CLOSED = 7
};

/* Optional INFO codes (SV_INFO.info_code) – not heavily used yet */
enum{
	LCH_INFO_NONE         = 0,
	LCH_INFO_MOVES_TOTAL  = 1,
	LCH_INFO_CHAT_TOTAL   = 2
};

#define LCH_CHAT_MAX_LEN 60

#pragma pack(push,1)

/* --------------------------------------------------------------------- */
/* Client -> Server messages                                             */
/* --------------------------------------------------------------------- */

typedef struct{
	u8	type;		/* LCH_CL_HELLO */
	u8	proto_ver;	/* 1 */
	u8	reserved[2];
} LCH_MsgClHello;

typedef struct{
	u8	type;		/* LCH_CL_NEW_GAME */
	u8	flags;		/* rated + color pref */
	u8	minutes;	/* 1..255 (base minutes) */
	u8	increment;	/* 0..255 (seconds) */
} LCH_MsgClNewGame;

typedef struct{
	u8	type;		/* LCH_CL_MOVE */
	u8	from_sq;	/* 0..63 */
	u8	to_sq;		/* 0..63 */
	u8	promo;		/* LCH_PROMO_* */
} LCH_MsgClMove;

typedef struct{
	u8	type;		/* LCH_CL_RESIGN */
} LCH_MsgClResign;

typedef struct{
	u8	type;		/* LCH_CL_ABORT */
} LCH_MsgClAbort;

typedef struct{
	u8	type;		/* LCH_CL_PING */
	u8	token;		/* arbitrary */
	u8	reserved[2];
} LCH_MsgClPing;

typedef struct{
	u8	type;		/* LCH_CL_CHAT */
	u8	flags;		/* reserved, 0 for now */
	u8	len;		/* 0..LCH_CHAT_MAX_LEN */
	char text[LCH_CHAT_MAX_LEN];
} LCH_MsgClChat;

typedef struct{
	u8	type;		/* LCH_CL_REQ_MOVES */
	u8	start_hi;	/* high byte of start index */
	u8	start_lo;	/* low byte of start index */
	u8	count;		/* requested number of plies (0 = server default) */
} LCH_MsgClReqMoves;

typedef struct{
	u8	type;		/* LCH_CL_REQ_CHAT */
	u8	start_hi;	/* high byte of start index */
	u8	start_lo;	/* low byte of start index */
	u8	count;		/* requested number of lines (0 = server default) */
} LCH_MsgClReqChat;

/* --------------------------------------------------------------------- */
/* Server -> Client messages                                             */
/* --------------------------------------------------------------------- */

typedef struct{
	u8	type;		/* LCH_SV_HELLO */
	u8	proto_ver;	/* 1 */
	u8	capabilities;	/* reserved for future extensions */
	u8	reserved;
} LCH_MsgSvHello;

typedef struct{
	u8	type;		/* LCH_SV_GAME_START */
	u8	flags;		/* same semantics as NEW_GAME flags */
	u8	minutes;
	u8	increment;
	u8	my_side;	/* LCH_SIDE_* or 0xFF unknown */
	u8	game_id_len;
	char game_id[8];	/* short lichess id (optional) */
} LCH_MsgSvGameStart;

typedef struct{
	u8	type;		/* LCH_SV_OPP_MOVE */
	u8	from_sq;
	u8	to_sq;
	u8	promo;
} LCH_MsgSvOppMove;

typedef struct{
	u8	type;		/* LCH_SV_GAME_END */
	u8	result;		/* LCH_RESULT_* from client POV */
	u8	reason;		/* LCH_REASON_* */
	u8	reserved;
} LCH_MsgSvGameEnd;

typedef struct{
	u8	type;		/* LCH_SV_ERROR */
	u8	code;		/* LCH_ERR_* */
	u8	arg0;
	u8	arg1;
} LCH_MsgSvError;

typedef struct{
	u8	type;		/* LCH_SV_PONG */
	u8	token;
	u8	reserved[2];
} LCH_MsgSvPong;

typedef struct{
	u8	type;		/* LCH_SV_INFO */
	u8	info_code;	/* LCH_INFO_* */
	u8	value0;
	u8	value1;
} LCH_MsgSvInfo;

/* Clock snapshot */
typedef struct{
	u8	type;		/* LCH_SV_CLOCK */
	u8	flags;		/* bit0: side-to-move (0=white,1=black) */
	u16	white_secs;
	u16	black_secs;
} LCH_MsgSvClock;

typedef struct{
	u8	type;		/* LCH_SV_CHAT */
	u8	flags;		/* bit0 could mean "local player" later */
	u8	len;		/* 0..LCH_CHAT_MAX_LEN */
	char text[LCH_CHAT_MAX_LEN];
} LCH_MsgSvChat;

#pragma pack(pop)

/* --------------------------------------------------------------------- */
/* Helpers usable by client and server                                   */
/* --------------------------------------------------------------------- */

static inline u8 lch_sq_from_file_rank(u8 file, u8 rank){
	return (u8)(file + rank * 8);
}

static inline void lch_sq_to_file_rank(u8 sq, u8 *file, u8 *rank){
	*file = (u8)(sq & 7);
	*rank = (u8)(sq >> 3);
}

/* 0..63 + promo -> UCI ASCII */
static inline void lch_move_to_uci(u8 from_sq, u8 to_sq, u8 promo, char *buf){
	u8 ff, fr, tf, tr;
	lch_sq_to_file_rank(from_sq, &ff, &fr);
	lch_sq_to_file_rank(to_sq,   &tf, &tr);

	buf[0] = (char)('a' + ff);
	buf[1] = (char)('1' + fr);
	buf[2] = (char)('a' + tf);
	buf[3] = (char)('1' + tr);

	if(promo == LCH_PROMO_NONE){
		buf[4] = '\0';
	}else{
		char pc = 'q';
		if(promo == LCH_PROMO_R) pc = 'r';
		else if(promo == LCH_PROMO_B) pc = 'b';
        else if(promo == LCH_PROMO_N) pc = 'n';
		buf[4] = pc;
		buf[5] = '\0';
	}
}

/* UCI "e2e4" or "e7e8q" -> 0..63 + promo; returns 0 on success, <0 on error */
static inline int uci_to_lch_move(const char *uci, u8 *from_sq, u8 *to_sq, u8 *promo){
	u8 ff, fr, tf, tr;

	if(!uci) return -1;
	if(uci[0] < 'a' || uci[0] > 'h') return -1;
	if(uci[1] < '1' || uci[1] > '8') return -1;
	if(uci[2] < 'a' || uci[2] > 'h') return -1;
	if(uci[3] < '1' || uci[3] > '8') return -1;

	ff = (u8)(uci[0] - 'a');
	fr = (u8)(uci[1] - '1');
	tf = (u8)(uci[2] - 'a');
	tr = (u8)(uci[3] - '1');

	*from_sq = lch_sq_from_file_rank(ff, fr);
	*to_sq   = lch_sq_from_file_rank(tf, tr);

	if(uci[4] == '\0'){
		*promo = LCH_PROMO_NONE;
	}else{
		char pc = (char)(uci[4] | 32);
		if(pc == 'q') *promo = LCH_PROMO_Q;
		else if(pc == 'r') *promo = LCH_PROMO_R;
		else if(pc == 'b') *promo = LCH_PROMO_B;
		else if(pc == 'n') *promo = LCH_PROMO_N;
		else *promo = LCH_PROMO_NONE;
	}
	return 0;
}

/**
 * Start an open challenge (random opponent) on Lichess.
 * - token:          OAuth token with board:play / challenge:write
 * - rated:          0 = casual, non-zero = rated
 * - clock_limit:    base time in seconds (e.g. 600 for 10+0)
 * - clock_increment:increment in seconds (e.g. 5 for 10+5)
 * - out_game_id:    buffer to receive the game/challenge ID (0-terminated)
 * - out_game_id_sz: size of out_game_id
 *
 * Returns 0 on success, <0 on error.
 *
 * Implemented on the server side; on Uzebox you just send NEW_GAME.
 */
int lichess_start_random_game(const char *token,
	int rated,
	unsigned clock_limit,
	unsigned clock_increment,
	char *out_game_id,
	size_t out_game_id_sz);

#endif /* UZENET_LICHESS_H */
