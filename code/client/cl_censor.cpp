/*
===========================================================================
Copyright (C) 2026 the OpenMoHAA team

This file is part of OpenMoHAA source code.

OpenMoHAA source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

OpenMoHAA source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with OpenMoHAA source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

// Added in OPM
//  cl_censor.cpp: the words of cl_censor starred out of the players' names
//  and the messages shown, however they're spelled: any case, letters
//  repeated, look-alike digits and signs (n1gg3r, f@g, niqqa), accented
//  letters, anything between the letters (n.i.g.g.e.r) and colour codes
//  in them, letters spelled out one by one (g a y). A word is looked for
//  in each word of the text, not across the spaces between them, and a
//  letter it has twice in a row has to be there twice at least (nigger
//  isn't in Nigeria). The text keeps its length.

#include "cl_censor.h"

#include <ctype.h>
#include <string.h>

#define CENSOR_WORDS 64
#define CENSOR_RUNS 32

typedef struct {
    char letter;
    int  count; // at least
} censorRun_t;

typedef struct {
    censorRun_t runs[CENSOR_RUNS];
    int         numRuns;
} censorWord_t;

static censorWord_t cl_censorWords[CENSOR_WORDS];
static int          cl_numCensorWords;
static int          cl_censorParsed = -1; // cl_censor's modificationCount then

/*
===============
CL_CensorFold

The letter a character stands for, any case: a look-alike digit or sign
as the letter, an accented letter (Latin-1) as its own; 0 for what isn't
one
===============
*/
static char CL_CensorFold(unsigned char c)
{
    static const char *const accents[] = {
        "a\xe0\xe1\xe2\xe3\xe4\xe5\xc0\xc1\xc2\xc3\xc4\xc5",
        "e\xe8\xe9\xea\xeb\xc8\xc9\xca\xcb",
        "i\xec\xed\xee\xef\xcc\xcd\xce\xcf",
        "o\xf2\xf3\xf4\xf5\xf6\xf8\xd2\xd3\xd4\xd5\xd6\xd8",
        "u\xf9\xfa\xfb\xfc\xd9\xda\xdb\xdc",
        "n\xf1\xd1",
        "c\xe7\xc7",
        "y\xfd\xff\xdd"
    };
    int i;

    if (c >= 'A' && c <= 'Z') {
        c += 'a' - 'A';
    }

    switch (c) {
    case '0':
        return 'o';
    case '1':
    case '!':
    case '|':
        return 'i';
    case '3':
        return 'e';
    case '4':
    case '@':
        return 'a';
    case '5':
    case '$':
        return 's';
    case '7':
    case '+':
        return 't';
    case 'q':
        return 'g';
    }

    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
        return c;
    }
    if (c >= 0xc0) {
        for (i = 0; i < (int)ARRAY_LEN(accents); i++) {
            if (strchr(accents[i] + 1, c)) {
                return accents[i][0];
            }
        }
    }
    return 0;
}

/*
===============
CL_CensorParse

The words of cl_censor, separated by spaces or commas, as runs of letters
===============
*/
static void CL_CensorParse(void)
{
    const char   *s;
    censorWord_t *word;
    char          letter;

    cl_censorParsed   = cl_censor->modificationCount;
    cl_numCensorWords = 0;

    for (s = cl_censor->string; *s && cl_numCensorWords < CENSOR_WORDS;) {
        while (*s == ' ' || *s == ',') {
            s++;
        }

        word          = &cl_censorWords[cl_numCensorWords];
        word->numRuns = 0;
        for (; *s && *s != ' ' && *s != ','; s++) {
            letter = CL_CensorFold(*s);
            if (!letter) {
                continue;
            }
            if (word->numRuns && word->runs[word->numRuns - 1].letter == letter) {
                word->runs[word->numRuns - 1].count++;
            } else if (word->numRuns < CENSOR_RUNS) {
                word->runs[word->numRuns].letter = letter;
                word->runs[word->numRuns].count  = 1;
                word->numRuns++;
            }
        }

        if (word->numRuns) {
            cl_numCensorWords++;
        }
    }
}

/*
===============
CL_CensorWord

Stars the censored words out of a word of the text, its letters and where
they are in the text
===============
*/
static qboolean CL_CensorWord(char *text, const char *letters, const int *places, int numLetters)
{
    static char runLetter[MAX_STRING_CHARS];
    static int  runCount[MAX_STRING_CHARS];
    static int  runStart[MAX_STRING_CHARS + 1];
    const censorWord_t *word;
    qboolean            censored = qfalse;
    int                 numRuns  = 0;
    int                 i, j, k;

    for (i = 0; i < numLetters; i++) {
        if (numRuns && runLetter[numRuns - 1] == letters[i]) {
            runCount[numRuns - 1]++;
        } else {
            runLetter[numRuns] = letters[i];
            runCount[numRuns]  = 1;
            runStart[numRuns]  = i;
            numRuns++;
        }
    }
    runStart[numRuns] = numLetters;

    for (i = 0; i < cl_numCensorWords; i++) {
        word = &cl_censorWords[i];
        for (j = 0; j + word->numRuns <= numRuns; j++) {
            for (k = 0; k < word->numRuns; k++) {
                if (runLetter[j + k] != word->runs[k].letter || runCount[j + k] < word->runs[k].count) {
                    break;
                }
            }
            if (k < word->numRuns) {
                continue;
            }

            for (k = runStart[j]; k < runStart[j + word->numRuns]; k++) {
                text[places[k]] = '*';
            }
            censored = qtrue;
        }
    }
    return censored;
}

/*
===============
CL_CensorText

Stars the words of cl_censor out of len characters of text; whether it did
===============
*/
static qboolean CL_CensorText(char *text, int len)
{
    static char letters[MAX_STRING_CHARS];
    static int  places[MAX_STRING_CHARS];
    qboolean    censored   = qfalse;
    int         numLetters = 0;
    int         start      = 0; // the first letter not looked at yet
    int         inWord     = 0; // letters in the word being read
    int         i;
    char        letter;

    if (cl_censor->modificationCount != cl_censorParsed) {
        CL_CensorParse();
    }
    if (!cl_numCensorWords) {
        return qfalse;
    }

    for (i = 0; i <= len && numLetters < MAX_STRING_CHARS; i++) {
        if (i == len || text[i] == ' ' || text[i] == '\t' || text[i] == '\n') {
            // a word of several letters is looked at alone, and the
            // one-letter ones before it together, as spelled out
            if (inWord > 1) {
                if (numLetters - inWord > start) {
                    censored |= CL_CensorWord(text, letters + start, places + start, numLetters - inWord - start);
                }
                start = numLetters - inWord;
                censored |= CL_CensorWord(text, letters + start, places + start, inWord);
                start = numLetters;
            }
            inWord = 0;
            continue;
        }

        if (text[i] == Q_COLOR_ESCAPE && i + 1 < len && isalnum((unsigned char)text[i + 1])) {
            i++;
            continue;
        }

        letter = CL_CensorFold(text[i]);
        if (letter) {
            letters[numLetters] = letter;
            places[numLetters]  = i;
            numLetters++;
            inWord++;
        }
    }

    if (numLetters > start) {
        censored |= CL_CensorWord(text, letters + start, places + start, numLetters - start);
    }
    return censored;
}

/*
===============
CL_Censor

Stars the words of cl_censor out of a message; whether it did
===============
*/
qboolean CL_Censor(char *text)
{
    return CL_CensorText(text, strlen(text));
}

/*
===============
CL_CensorPlayerName

Stars the words of cl_censor out of the name in a player's info string
===============
*/
void CL_CensorPlayerName(char *info)
{
    char *key, *value;
    char *s = info;

    while (*s) {
        if (*s == '\\') {
            s++;
        }
        key = s;
        while (*s && *s != '\\') {
            s++;
        }
        if (!*s) {
            return;
        }
        value = ++s;
        while (*s && *s != '\\') {
            s++;
        }
        if (value - key == 5 && !Q_strncmp(key, "name", 4)) {
            CL_CensorText(value, s - value);
            return;
        }
    }
}
