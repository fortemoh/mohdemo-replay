/*
 * crosshair_import / crosshair_export (cl_crosshair.c) against stub cvars, command line and clipboard:
 * the round trip of akiver/csgo-sharecode's test vectors and of the mock-up's codes, and the codes CS2 rejects
 */

#include "../../qcommon/q_shared.h"
#include "../../qcommon/qcommon.h"

#include <stdio.h>
#include <stdlib.h>

void CL_CrosshairImport_f(void);
void CL_CrosshairExport_f(void);

static cvar_t      vars[16];
static int         numVars, changes, failures;
static const char *arg;
static char        clipboard[128];

cvar_t *Cvar_FindVar(const char *name)
{
    int i;

    for (i = 0; i < numVars; i++) {
        if (!strcmp(vars[i].name, name)) {
            return &vars[i];
        }
    }
    return NULL;
}

void Cvar_Set(const char *name, const char *value)
{
    cvar_t *var = Cvar_FindVar(name);

    if (!var) {
        var       = &vars[numVars++];
        var->name = strdup(name);
    }
    free(var->string);
    var->string  = strdup(value);
    var->value   = atof(value);
    var->integer = atoi(value);
    changes++;
}

cvar_t *Cvar_Get(const char *name, const char *value, int flags)
{
    if (!Cvar_FindVar(name)) {
        Cvar_Set(name, value);
    }
    return Cvar_FindVar(name);
}

char *Cvar_VariableString(const char *name)
{
    cvar_t *var = Cvar_FindVar(name);

    return var ? var->string : (char *)"";
}

int Cmd_Argc(void)
{
    return arg ? 2 : 1;
}

char *Cmd_Argv(int i)
{
    return (char *)(i ? arg : "crosshair");
}

const char *Sys_GetWholeClipboard(void)
{
    return clipboard[0] ? clipboard : NULL;
}

void Sys_SetClipboard(const char *text)
{
    Q_strncpyz(clipboard, text, sizeof(clipboard));
}

static void Check(int ok, const char *what, const char *code)
{
    if (!ok) {
        printf("FAILED: %s: %s\n", what, code);
        failures++;
    }
}

static const char *Var(const char *name)
{
    return Cvar_VariableString(name);
}

static void Import(const char *code)
{
    arg = code;
    CL_CrosshairImport_f();
}

static void Export(void)
{
    arg = NULL;
    CL_CrosshairExport_f();
}

int main(void)
{
    static const char *const same[] = {
        "CSvbPubOq37zTGqtsPTP5QTrp5CB4xFXiKRLfzJsm49ZRe", // the README's: red, dot, at 768 lines
        "CSHt3ObzKS3ikGJLRBVrKjPUrhOCikyke53na8cJFtNZKa",
        "CSAB6Oby2JUDOrBRy4jRcsR6JOQdhbszKF9GuKQGhKVwAc", // negative gap, length 255
        "CS7uw3wMkJBJ4rMDLEaYbyfTJFFPvkLAhOmBFi8zOM6H2d", // height 100, gap -32768, thickness 63, outline 3
        "CSpb9t3x5NtrX5fWaENsr8vusEtwpiURjzqauLkOiYHMqF",
        "CSUyBmKsvnUffbDXZTmd9GxnFxzmKaO6VAvnEphKt6jQfJ", // Dot Only
        "CSe7hx33ke3OSwt6LZXyiGqAtsaBGkfmGbZ6v5oPo3SMjc",
        "CSB4wBB8iKGpayTW5nVFDaCewcaLiqfu9SUSxSVNBYKFZB",
    };
    static const char *const rejected[] = {
        "CSkSCHQKunGr2yPsJr2U3RTd8ycMA2pEs9jWnxK5KjMmWL", // checksum
        "CSoQhciXBHZUKAbfexZBeMWFKxkrXL5MLumDLj3JYW4snP", // height 0
        "CSuZhVyU8icUwjTjopBSVi6ctEbq5GiOXJBRdC6LNLKaFL", // version 2
        "CSGO-9JzcN-4dZtA-DdXis-8qz5T-rCnkP",
        "CSvbPubOq37zTGqtsPTP5QTrp5CB4xFXiKRLfzJsm49ZR0", // 0: not a character of the codes
        "CSvbPubOq37zTGqtsPTP5QTrp5CB4xFXiKRLfzJsm49ZR",
        "CS99999999999999999999999999999999999999999999", // more than 32 bytes
        "",
    };
    int i, before;

    // nothing imported, no setting registered yet, then the defaults registered: the default crosshair
    Export();
    Check(!strcmp(clipboard, "CSLBC5CuAwZPQ6UU8bmAK9AGuFMeHdQi7T89MkHETeOSsc"), "default, unregistered", clipboard);
    Cvar_Get("cg_crosshair_length", "8", 0);
    Cvar_Get("cg_crosshair_color", "FFFFFF", 0);
    Export();
    Check(!strcmp(clipboard, "CSLBC5CuAwZPQ6UU8bmAK9AGuFMeHdQi7T89MkHETeOSsc"), "default", clipboard);

    for (i = 0; i < (int)ARRAY_LEN(same); i++) {
        Import(same[i]);
        Export();
        Check(!strcmp(clipboard, same[i]), "round trip", same[i]);
    }

    Import(same[0]);
    Check(!strcmp(Var("cg_crosshair_length"), "7.03125") && !strcmp(Var("cg_crosshair_thickness"), "1.40625")
              && !strcmp(Var("cg_crosshair_gap"), "0") && !strcmp(Var("cg_crosshair_color"), "FF0000")
              && !strcmp(Var("cg_crosshair_dot"), "1") && !strcmp(Var("cg_crosshair_outline"), "0"),
          "cvars", same[0]);

    // from the clipboard, with a line break
    Q_strncpyz(clipboard, " CSHt3ObzKS3ikGJLRBVrKjPUrhOCikyke53na8cJFtNZKa\r\n", sizeof(clipboard));
    Import(NULL);
    Check(!strcmp(Var("cg_crosshair_code"), same[1]), "clipboard", same[1]);

    // one setting changed: only it written over the code
    Cvar_Set("cg_crosshair_color", "00ff00");
    Export();
    Import(clipboard);
    Check(!strcmp(Var("cg_crosshair_color"), "00FF00") && !strcmp(Var("cg_crosshair_length"), "7.03125"),
          "changed", clipboard);

    // Dot Only with arms: a Static Cross with a dot
    Import(same[5]);
    Cvar_Set("cg_crosshair_length", "8");
    Export();
    Import(clipboard);
    Check(!strcmp(Var("cg_crosshair_length"), "8") && !strcmp(Var("cg_crosshair_dot"), "1"), "dot only", clipboard);

    for (i = 0; i < (int)ARRAY_LEN(rejected); i++) {
        before = changes;
        Import(rejected[i]);
        Check(changes == before, "rejected", rejected[i]);
    }

    printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures != 0;
}
