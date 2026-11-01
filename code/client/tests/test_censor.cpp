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

// Checks what cl_censor stars out of names and messages:
//
//   test_censor [<cases>]
//
// The cases file has a line per case, the text and what it becomes,
// separated by a tab, censored with the words of its first line.

#include "../cl_censor.h"

#include <cstdio>
#include <cstring>
#include <string>

static cvar_t censor = {};
cvar_t       *cl_censor = &censor;

static const char *const WORDS = "nigger nigga negro faggot fag gay homo nazi hitler heil kkk jew";

static const char *const CASES[][2] = {
    // any case, look-alikes, repeats, accents, anything between the letters
    {"NiGGeR",                  "******"},
    {"n1gg3r",                  "******"},
    {"niiiggerrr",              "**********"},
    {"niqqa",                   "*****"},
    {"N!GGA",                   "*****"},
    {"f@gg0t",                  "******"},
    {"G4Y",                     "***"},
    {"J3w",                     "***"},
    {"N\xe9gro",                "*****"},
    {"n.i.g.g.e.r",             "*.*.*.*.*.*"},
    {"h1tl3r",                  "******"},
    {"N@Z!",                    "****"},
    // colour codes skipped and kept
    {"^1ni^2gger",              "^1**^2****"},
    // in a word, and in each word
    {"gaymer #inc",             "***mer #inc"},
    {"legend homo",             "legend ****"},
    {"Jew Hunter",              "*** Hunter"},
    // spelled out, one letter a word
    {"n i g g e r",             "* * * * * *"},
    {"a b g a y x",             "a b * * * x"},
    {"k k k",                   "* * *"},
    // not across words, nor with a letter less than it has twice
    {"Mega Yoshi",              "Mega Yoshi"},
    {"the lower torso",         "the lower torso"},
    {"Nigeria",                 "Nigeria"},
    {"kk",                      "kk"},
    {"hello world",             "hello world"},
    // a message as it comes
    {" \"\x04plut1 was sniped by f4g0t in the head\"", " \"\x04plut1 was sniped by ***0t in the head\""},
};

static bool Check(const std::string& text, const std::string& want)
{
    char got[4096];

    Q_strncpyz(got, text.c_str(), sizeof(got));
    CL_Censor(got);
    if (want != got) {
        printf("FAILED \"%s\": \"%s\", not \"%s\"\n", text.c_str(), got, want.c_str());
        return false;
    }
    return true;
}

static void Words(const char *words)
{
    censor.string = (char *)words;
    censor.modificationCount++;
}

int main(int argc, char **argv)
{
    char info[] = "\\name\\x gay x\\model\\gay_soldier\\";
    int  failed = 0, count = 0;
    int  i;

    Words(WORDS);
    for (i = 0; i < (int)ARRAY_LEN(CASES); i++, count++) {
        failed += !Check(CASES[i][0], CASES[i][1]);
    }

    // in a player's info string, the name only
    CL_CensorPlayerName(info);
    failed += strcmp(info, "\\name\\x *** x\\model\\gay_soldier\\") != 0;
    count++;

    // none: nothing
    Words("");
    failed += !Check("gay", "gay");
    count++;

    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        char  line[8192];

        if (!f) {
            printf("Can't open %s\n", argv[1]);
            return 1;
        }
        static std::string words;
        if (fgets(line, sizeof(line), f)) {
            words = std::string(line, strcspn(line, "\r\n"));
            Words(words.c_str());
        }
        while (fgets(line, sizeof(line), f)) {
            std::string all(line, strcspn(line, "\r\n"));
            size_t      tab = all.find('\t');
            if (tab != std::string::npos) {
                failed += !Check(all.substr(0, tab), all.substr(tab + 1));
                count++;
            }
        }
        fclose(f);
    }

    printf("%d of %d cases failed\n", failed, count);
    return failed ? 1 : 0;
}
