//
//  File: %sys-scan.h
//  Summary: "Lexical Scanner Definitions"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//

extern const Byte Lex_Map[256];  // declared in %l-scan.c

//
//  Tokens returned by the scanner.  Keep in sync with Token_Names[].
//
// !!! There was a micro-optimization in R3-Alpha which made the order of
// tokens align with types, e.g. TOKEN_WORD + 1 => TOKEN_GET_WORD, and
// REB_WORD + 1 => SET_WORD.  As optimizations go, it causes annoyances when
// the type table is rearranged.  A better idea might be to use REB_XXX
// values as the tokens themselves--the main reason not to do this seems to
// be because of the Token_Names[] array.
//
enum Reb_Token {
    TOKEN_0 = 0,
    TOKEN_END,
    TOKEN_NEWLINE,
    TOKEN_BLANK,
    TOKEN_COMMA,
    TOKEN_COLON,
    TOKEN_CARET,
    TOKEN_AT,
    TOKEN_AMPERSAND,
    TOKEN_WORD,
    TOKEN_ESCAPED_WORD,
    TOKEN_LOGIC,
    TOKEN_INTEGER,
    TOKEN_DECIMAL,
    TOKEN_PERCENT,
    TOKEN_GROUP_END,
    TOKEN_GROUP_BEGIN,
    TOKEN_BLOCK_END,
    TOKEN_BLOCK_BEGIN,
    TOKEN_MONEY,
    TOKEN_TIME,
    TOKEN_DATE,
    TOKEN_CHAR,
    TOKEN_APOSTROPHE,
    TOKEN_TILDE,
    TOKEN_STRING,
    TOKEN_BINARY,
    TOKEN_PAIR,
    TOKEN_TUPLE,  // only triggered in leading dot cases (. .. .foo .foo.bar)
    TOKEN_FILE,
    TOKEN_EMAIL,
    TOKEN_URL,
    TOKEN_ISSUE,
    TOKEN_TAG,
    TOKEN_PATH,  // only triggered in leading slash cases (/ // /foo /foo.bar)
    TOKEN_CONSTRUCT,
    TOKEN_MAX
};


/*
**  Lexical Table Entry Encoding
*/
#define LEX_SHIFT       5               /* shift for encoding classes */
#define LEX_CLASS       (3<<LEX_SHIFT)  /* class bit field */
#define LEX_VALUE       (0x1F)          /* value bit field */

#define GET_LEX_CLASS(c)  (Lex_Map[(Byte)c] >> LEX_SHIFT)
#define GET_LEX_VALUE(c)  (Lex_Map[(Byte)c] & LEX_VALUE)


/*
**  Delimiting Chars (encoded in the LEX_VALUE field)
**  NOTE: Macros do make assumption that _RETURN is the last space delimiter
*/
enum LEX_DELIMIT_ENUM {
    LEX_DELIMIT_SPACE,              /* 20 space */
    LEX_DELIMIT_END,                /* 00 null terminator, end of input */
    LEX_DELIMIT_LINEFEED,           /* 0A line-feed */
    LEX_DELIMIT_RETURN,             /* 0D return */
    LEX_DELIMIT_COMMA,              /* 2C , - expression barrier */
    LEX_DELIMIT_LEFT_PAREN,         /* 28 ( */
    LEX_DELIMIT_RIGHT_PAREN,        /* 29 ) */
    LEX_DELIMIT_LEFT_BRACKET,       /* 5B [ */
    LEX_DELIMIT_RIGHT_BRACKET,      /* 5D ] */

    LEX_DELIMIT_HARD = LEX_DELIMIT_RIGHT_BRACKET,
    //
    // ^-- As a step toward "Plan -4", the above delimiters are considered to
    // always terminate, e.g. a URL `http://example.com/a)` will not pick up
    // the parenthesis as part of the URL.  But the below delimiters will be
    // picked up, so that `http://example.com/{a} is valid:
    //
    // https://github.com/metaeducation/ren-c/issues/1046

    LEX_DELIMIT_LEFT_BRACE,         /* 7B } */
    LEX_DELIMIT_RIGHT_BRACE,        /* 7D } */
    LEX_DELIMIT_DOUBLE_QUOTE,       /* 22 " */
    LEX_DELIMIT_SLASH,              /* 2F / - date, path, file */
    LEX_DELIMIT_PERIOD,             /* 2E . - decimal, tuple, file */
    LEX_DELIMIT_TILDE,              /* 7E ~ - used only by QUASI! */

    LEX_DELIMIT_UTF8_ERROR,

    LEX_DELIMIT_MAX
};

STATIC_ASSERT(LEX_DELIMIT_MAX <= 16);



/*
**  General Lexical Classes (encoded in the LEX_CLASS field)
**  NOTE: macros do make assumptions on the order, and that there are 4!
*/
enum LEX_CLASS_ENUM {
    LEX_CLASS_DELIMIT = 0,
    LEX_CLASS_SPECIAL,
    LEX_CLASS_WORD,
    LEX_CLASS_NUMBER
};

#define LEX_DELIMIT     (LEX_CLASS_DELIMIT<<LEX_SHIFT)
#define LEX_SPECIAL     (LEX_CLASS_SPECIAL<<LEX_SHIFT)
#define LEX_WORD        (LEX_CLASS_WORD<<LEX_SHIFT)
#define LEX_NUMBER      (LEX_CLASS_NUMBER<<LEX_SHIFT)

typedef uint16_t LEXFLAGS;  // 16 flags per lex class

#define LEX_FLAG(n)             (1 << (n))
#define SET_LEX_FLAG(f,l)       (f = f | LEX_FLAG(l))
#define HAS_LEX_FLAGS(f,l)      (f & (l))
#define HAS_LEX_FLAG(f,l)       (f & LEX_FLAG(l))
#define ONLY_LEX_FLAG(f,l)      (f == LEX_FLAG(l))

#define MASK_LEX_CLASS(c)               (Lex_Map[(Byte)c] & LEX_CLASS)
#define IS_LEX_SPACE(c)                 (!Lex_Map[(Byte)c])
#define IS_LEX_ANY_SPACE(c)             (Lex_Map[(Byte)c]<=LEX_DELIMIT_RETURN)
#define IS_LEX_DELIMIT(c)               (MASK_LEX_CLASS(c) == LEX_DELIMIT)
#define IS_LEX_SPECIAL(c)               (MASK_LEX_CLASS(c) == LEX_SPECIAL)
#define IS_LEX_WORD(c)                  (MASK_LEX_CLASS(c) == LEX_WORD)
// Optimization (necessary?)
#define IS_LEX_NUMBER(c)                (Lex_Map[(Byte)c] >= LEX_NUMBER)

#define IS_LEX_NOT_DELIMIT(c)           (Lex_Map[(Byte)c] >= LEX_SPECIAL)
#define IS_LEX_WORD_OR_NUMBER(c)        (Lex_Map[(Byte)c] >= LEX_WORD)

inline static bool IS_LEX_DELIMIT_HARD(Byte c) {
    assert(IS_LEX_DELIMIT(c));
    return GET_LEX_VALUE(c) <= LEX_DELIMIT_HARD;
}

//
//  Special Chars (encoded in the LEX_VALUE field)
//
enum LEX_SPECIAL_ENUM {             /* The order is important! */
    LEX_SPECIAL_AT,                 /* 40 @ - email */
    LEX_SPECIAL_PERCENT,            /* 25 % - file name */
    LEX_SPECIAL_BACKSLASH,          /* 5C \  */
    LEX_SPECIAL_COLON,              /* 3A : - time, get, set */
    LEX_SPECIAL_APOSTROPHE,         /* 27 ' - literal */
    LEX_SPECIAL_LESSER,             /* 3C < - compare or tag */
    LEX_SPECIAL_GREATER,            /* 3E > - compare or end tag */
    LEX_SPECIAL_PLUS,               /* 2B + - positive number */
    LEX_SPECIAL_MINUS,              /* 2D - - date, negative number */
    LEX_SPECIAL_BAR,                /* 7C | - expression barrier */
    LEX_SPECIAL_BLANK,              /* 5F _ - blank */

                                    /** Any of these can follow - or ~ : */
    LEX_SPECIAL_POUND,              /* 23 # - hex number */
    LEX_SPECIAL_DOLLAR,             /* 24 $ - money */
    LEX_SPECIAL_SEMICOLON,          /* 3B ; - comment */

    // LEX_SPECIAL_WORD is not a LEX_VALUE() of anything in LEX_CLASS_SPECIAL,
    // it is used to set a flag by Prescan_Token().
    //
    // !!! Comment said "for nums"
    //
    LEX_SPECIAL_WORD,

    LEX_SPECIAL_MAX
};

STATIC_ASSERT(LEX_SPECIAL_MAX <= 16);


/*
**  Special Encodings
*/
#define LEX_DEFAULT (LEX_DELIMIT|LEX_DELIMIT_SPACE)     /* control chars = spaces */

// In UTF8 C0, C1, F5, and FF are invalid.  Ostensibly set to default because
// it's not necessary to use a bit for a special designation, since they
// should not occur.
//
// !!! If a bit is free, should it be used for errors in the debug build?
//
#define LEX_UTFE LEX_DEFAULT

/*
**  Characters not allowed in Words
*/
#define LEX_WORD_FLAGS (LEX_FLAG(LEX_SPECIAL_AT) |              \
                        LEX_FLAG(LEX_SPECIAL_PERCENT) |         \
                        LEX_FLAG(LEX_SPECIAL_BACKSLASH) |       \
                        LEX_FLAG(LEX_SPECIAL_POUND) |           \
                        LEX_FLAG(LEX_SPECIAL_DOLLAR) |          \
                        LEX_FLAG(LEX_SPECIAL_COLON) |           \
                        LEX_FLAG(LEX_SPECIAL_SEMICOLON))

enum rebol_esc_codes {
    // Must match Esc_Names[]!
    ESC_LINE,
    ESC_TAB,
    ESC_PAGE,
    ESC_ESCAPE,
    ESC_ESC,
    ESC_BACK,
    ESC_DEL,
    ESC_NULL,
    ESC_MAX
};


#define ANY_CR_LF_END(c) ((c) == '\0' or (c) == CR or (c) == LF)


//
// MAXIMUM LENGTHS
//
// These are the maximum input lengths in bytes needed for a buffer to give
// to Scan_XXX (not including terminator?)  The TO conversions from strings
// tended to hardcode the numbers, so that hardcoding is excised here to
// make it more clear what those numbers are and what their motivation might
// have been (not all were explained).
//
// (See also MAX_HEX_LEN, MAX_INT_LEN)
//

// 30-September-10000/12:34:56.123456789AM/12:34
#define MAX_SCAN_DATE 45

// The maximum length a tuple can be in characters legally for Scan_Tuple
// (should be in a better location, but just excised it for clarity.
#define MAX_SCAN_TUPLE (11 * 4 + 1)

#define MAX_SCAN_DECIMAL 24

#define MAX_SCAN_MONEY 36

#define MAX_SCAN_TIME 30

#define MAX_SCAN_WORD 255


#ifdef ITOA64 // Integer to ascii conversion
    #define INT_TO_STR(n,s) \
        _i64toa(n, s_cast(s), 10)
#else
    #define INT_TO_STR(n,s) \
        Form_Int_Len(s, n, MAX_INT_LEN)
#endif

#ifdef ATOI64 // Ascii to integer conversion
    #define CHR_TO_INT(s) \
        _atoi64(cs_cast(s))
#else
    #define CHR_TO_INT(s) \
        strtoll(cs_cast(s), 0, 10)
#endif


// Skip to the specified byte but not past the provided end pointer of bytes.
// nullptr if byte is not found.
//
inline static const Byte* Skip_To_Byte(
    const Byte* cp,
    const Byte* ep,
    Byte b
){
    while (cp != ep and *cp != b)
        ++cp;
    if (*cp == b)
        return cp;
    return nullptr;
}
