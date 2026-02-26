/***************************************************************************
 *  Original Diku Mud copyright (C) 1990, 1991 by Sebastian Hammer,        *
 *  Michael Seifert, Hans Henrik St{rfeldt, Tom Madsen, and Katja Nyboe.   *
 *                                                                         *
 *  Merc Diku Mud improvments copyright (C) 1992, 1993 by Michael          *
 *  Chastain, Michael Quan, and Mitchell Tse.                              *
 *                                                                         *
 *  In order to use any part of this Merc Diku Mud, you must comply with   *
 *  both the original Diku license in 'license.doc' as well the Merc       *
 *  license in 'license.txt'.  In particular, you may not remove either of *
 *  these copyright notices.                                               *
 *                                                                         *
 *  Much time and thought has gone into this software and you are          *
 *  benefitting.  We hope that you share your changes too.  What goes      *
 *  around, comes around.                                                  *
 ***************************************************************************/
/***************************************************************************
 *	ROM 2.4 is copyright 1993-1998 Russ Taylor			   *
 *	ROM has been brought to you by the ROM consortium		   *
 *	    Russ Taylor (rtaylor@hypercube.org)				   *
 *	    Gabrielle Taylor (gtaylor@hypercube.org)			   *
 *	    Brian Moore (zump@rom.org)					   *
 *	By using this code, you have agreed to follow the terms of the	   *
 *	ROM license, in the file Rom24/doc/rom.license			   *
 ***************************************************************************/

#include <sys/types.h>
#include <sys/time.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <limits.h>
#include <unistd.h>



#include "merc.h"
#include "interp.h"
#include "recycle.h"
#include "magic.h"
#include "olc.h"
#include "config.h"


#define MAX_DESCRIPTORS 256

/*
 * Special grants
 */
static char *spec_grants = NULL;

struct quest_type immquest =
{
    {""},
    0, 0, FALSE, NULL,
    {
	{0, ""}
    },
    FALSE,
    1,
    1,
    1,
	NULL

};

int reboot_time = 0;
CHAR_DATA *who_claim_reboot;

bool check_parse_name (char* name, bool newbie);		     /* comm.c   */
char *initial(const char *str);		     /* comm.c   */
char *is_granted(CHAR_DATA *ch, char *command);      /* interp.c */
bool is_immcmd(char *command);			     /* interp.c */
void grant_all(CHAR_DATA *ch);			     /* interp.c */
void grant_level(CHAR_DATA *ch, int level);	     /* interp.c */

void kick_pet(CHAR_DATA *pet);


static bool is_safe_report_filename(const char *name)
{
    const unsigned char *p;

    if (IS_NULLSTR(name))
	return FALSE;

    if (strstr(name, "..") != NULL || strchr(name, '/') != NULL || strchr(name, '\\') != NULL)
	return FALSE;

    for (p = (const unsigned char *)name; *p != '\0'; p++)
    {
	if (!(isalnum(*p) || *p == '.' || *p == '_' || *p == '-'))
	    return FALSE;
    }

    return TRUE;
}

static FILE *open_report_file(CHAR_DATA *ch, const char *name)
{
    char path[MAX_INPUT_LENGTH];

    if (!is_safe_report_filename(name))
    {
	send_to_char("Invalid file name. Use only letters, digits, '.', '_' or '-'.\n\r", ch);
	return NULL;
    }

    snprintf(path, sizeof(path), "%s%s", LOG_DIR, name);

    return file_open(path, "w");
}

extern SLIST_HEAD(, descriptor_data) descriptor_free;

/*
 * Local functions.
 */


ROOM_INDEX_DATA *	find_location	args((CHAR_DATA *ch, char *arg));

/*
 * Construct spec_grants array from different sources
 */
#define SPEC_GRANTS "raedit_create raedit_pc post_delete auction_stat"
void init_grants()
{
    char   cmd[MAX_STRING_LENGTH];
    size_t grsiz = strlen(SPEC_GRANTS) + 2;
    int    i;

    spec_grants = calloc(sizeof(char *), grsiz);

    strlcpy(spec_grants, SPEC_GRANTS, grsiz);
    strlcat(spec_grants, " ", grsiz);

    for (i = 0; !IS_NULLSTR(wiznet_table[i].name); i++)
    {
	snprintf(cmd, sizeof(cmd), "wn_%s", wiznet_table[i].name);

	if (strlen(cmd) + strlen(spec_grants) + 2 > grsiz)
	{
	    spec_grants = realloc(spec_grants, grsiz * 2);
	    grsiz *= 2;
	}

	strlcat(spec_grants, cmd, grsiz);
	strlcat(spec_grants, " ", grsiz);
    }
}

void do_wiznet(CHAR_DATA *ch, char *argument)
{
    int flag, col = 0;
    char buf[MAX_STRING_LENGTH];

    if (argument[0] == '\0')
	/* Show wiznet options - just like channel command */
    {
	send_to_char("\t\t\t{GWELCOME TO WIZNET!!!{Z\n\r", ch);
	send_to_char(
		     "Option       | Status   Option       | Status   Option       | Status\n\r", ch);
	send_to_char(
		     "---------------------   ---------------------   ---------------------\n\r", ch);
	/* list of all wiznet options */
	buf[0] = '\0';

	for (flag = 0; wiznet_table[flag].name != NULL; flag++)
	{
	    snprintf(buf, sizeof(buf), "wn_%s", wiznet_table[flag].name);

	    if (wiznet_table[flag].level <= get_trust(ch)
		|| is_spec_granted(ch, buf))
	    {
		printf_to_char("%-13s| %s      ", ch, wiznet_table[flag].name,
			       IS_SET(ch->wiznet, wiznet_table[flag].flag)
			       ? "{GON{Z " : "OFF");

		col++;
		if (col==3)
		{
		    send_to_char("\n\r", ch);
		    col=0;
		}
	    }
	}
	/* To avoid color bleeding */
	send_to_char("{x\n\r", ch);
	return;
    }

    if (!str_prefix(argument, "on"))
    {
	send_to_char("{ZWelcome to Wiznet!{x\n\r", ch);
	SET_BIT(ch->wiznet, WIZ_ON);
	return;
    }

    if (!str_prefix(argument, "off"))
    {
	send_to_char("{ZSigning off of Wiznet.{x\n\r", ch);
	REMOVE_BIT(ch->wiznet, WIZ_ON);
	return;
    }

    flag = wiznet_lookup(argument);

    if (flag == -1)
    {
	send_to_char("{ZNo such option.{x\n\r", ch);
	return;
    }

    if (get_trust(ch) < wiznet_table[flag].level)
    {
	snprintf(buf, sizeof(buf), "wn_%s", wiznet_table[flag].name);

	if (!is_spec_granted(ch, buf))
	{
	    send_to_char("Permission denied.\n\r", ch);
	    return;
	}
    }

    if (IS_SET(ch->wiznet, wiznet_table[flag].flag))
    {
	sprintf(buf, "{ZYou will no longer see %s on wiznet.{x\n\r",
		wiznet_table[flag].name);
	send_to_char(buf, ch);
	REMOVE_BIT(ch->wiznet, wiznet_table[flag].flag);
	return;
    }
    else
    {
	sprintf(buf, "{ZYou will now see %s on wiznet.{x\n\r",
		wiznet_table[flag].name);
	send_to_char(buf, ch);
	SET_BIT(ch->wiznet, wiznet_table[flag].flag);
	return;
    }
}

void wiznet(char *string, CHAR_DATA *ch, OBJ_DATA *obj,
	    int64_t flag, int64_t flag_skip, int min_level)
{
    DESCRIPTOR_DATA *d;
    char *strtime;
    char buf[MAX_STRING_LENGTH];

    strtime = ctime(&current_time);
    strtime += 11;
    strtime[strlen(strtime)-6] = '\0';

    SLIST_FOREACH(d, &descriptor_list, link)
    {
	if (d->connected == CON_PLAYING
	    && IS_SET(d->character->wiznet, WIZ_ON)
	    && (!flag || IS_SET(d->character->wiznet, flag))
	    && (!flag_skip || !IS_SET(d->character->wiznet, flag_skip))
	    && get_trust(d->character) >= min_level
	    && d->character != ch)
	{
	    if (IS_SET(d->character->wiznet, WIZ_PREFIX))
		send_to_char("{Z--> ", d->character);
	    else
		send_to_char("{Z", d->character);

	    sprintf(buf, "(%s) %s", strtime, string);

	    act_new(buf, d->character, obj, ch, TO_CHAR, POS_DEAD);
	    send_to_char("{x", d->character);
	}
    }

    return;
}

/* equips a character */
void do_outfit(CHAR_DATA *ch, char *argument)
{
    OBJ_DATA *obj;
    int i, sn, vnum;

    if (ch->level > LEVEL_NEWBIE || IS_NPC(ch))
    {
	send_to_char("Íàéäè ýòî ñàìîñòîÿòåëüíî!\n\r", ch);
	return;
    }

    if (ch->carry_number >= can_carry_n(ch) || ch->carry_weight >= can_carry_w(ch))
    {
	send_to_char("Òû íåñåøü ñëèøêîì ìíîãî âåùåé!\n\r", ch);
	return;
    }

    send_to_char("Òû ïîëó÷àåøü îò Áîãîâ ïðîñòåíüêóþ ýêèïèðîâêó.\n\r", ch);

    if ((obj = get_eq_char(ch, WEAR_LIGHT)) == NULL)
    {
	obj = create_object(get_obj_index(OBJ_VNUM_SCHOOL_BANNER), 0);
	obj->cost = 0;
	obj_to_char(obj, ch);
	equip_char(ch, obj, WEAR_LIGHT);
    }

    if ((obj = get_eq_char(ch, WEAR_FEET)) == NULL)
    {
	OBJ_DATA *boots;

	obj = create_object(get_obj_index(OBJ_VNUM_SCHOOL_BOOTS), 0);
	obj->cost = 0;
	obj_to_char(obj, ch);

	if (!((boots = get_eq_char(ch, WEAR_FEET)) != NULL))
	    equip_char(ch, obj, WEAR_FEET);
    }

    if ( ch->level > 4)
	return;

    if ((obj = get_eq_char(ch, WEAR_BODY)) == NULL)
    {
	obj = create_object(get_obj_index(OBJ_VNUM_SCHOOL_VEST), 0);
	obj->cost = 0;
	obj_to_char(obj, ch);
	equip_char(ch, obj, WEAR_BODY);
    }

    /* do the weapon thing */
    if ((obj = get_eq_char(ch, WEAR_WIELD)) == NULL)
    {
	sn = 0;
	vnum = OBJ_VNUM_SCHOOL_SWORD; /* just in case! */

	for (i = 0; weapon_table[i].name != NULL; i++)
	{
	    if (ch->pcdata->learned[sn] <
		ch->pcdata->learned[*weapon_table[i].gsn])
	    {
		sn = *weapon_table[i].gsn;
		vnum = weapon_table[i].vnum;
	    }
	}

	obj = create_object(get_obj_index(vnum), 0);
	obj->cost = 0;
	obj_to_char(obj, ch);

	if (!(ch->size < SIZE_LARGE && IS_WEAPON_STAT(obj, WEAPON_TWO_HANDS)
	      && (get_eq_char(ch, WEAR_SHIELD) != NULL || get_eq_char(ch, WEAR_HOLD) != NULL)))
	    equip_char(ch, obj, WEAR_WIELD);
    }

    if ((obj = get_eq_char(ch, WEAR_SHIELD)) == NULL)
    {
	OBJ_DATA *weapon;

	obj = create_object(get_obj_index(OBJ_VNUM_SCHOOL_SHIELD), 0);
	obj->cost = 0;
	obj_to_char(obj, ch);

	if (!((weapon = get_eq_char(ch, WEAR_WIELD)) != NULL && ch->size < SIZE_LARGE &&
	      IS_WEAPON_STAT(weapon, WEAPON_TWO_HANDS)))
	    equip_char(ch, obj, WEAR_SHIELD);
    }

    return;
}


long get_next_seconds(char *argument, CHAR_DATA *ch)
{
    char buf[MAX_INPUT_LENGTH];
    int days = 0;

    one_argument(argument, buf);

    if (buf[0] != '\0')
    {
	if (!is_number(buf))
	{
	    send_to_char("Âòîðîé àðãóìåíò äîëæåí áûòü ÷èñëîâûì!\n\r", ch);
	    return 0;
	}
	days = atoi(buf);
    }

    if (days <= 0)
	return 0;

    return current_time + 60 * 60 * 24 * days;
}


/* RT nochannels command, for those spammers */
void do_nochannels(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH], buf[MAX_STRING_LENGTH];
    CHAR_DATA *victim;

    argument = one_argument(argument, arg);

    if (arg[0] == '\0')
    {
	send_to_char("Îòîáðàòü êàíàëû ó êîãî?\n\r", ch);
	return;
    }

    if ((victim = get_char_world(ch, arg)) == NULL || IS_NPC(victim))
    {
	send_to_char("Òàêèõ íåò â ýòîì ìèðå.\n\r", ch);
	return;
    }

    if (IS_IMMORTAL(victim) && (get_trust(victim) >= get_trust(ch)))
    {
	send_to_char("Íå ïîëó÷èëîñü.\n\r", ch);
	return;
    }

    if (IS_SET(victim->comm, COMM_NOCHANNELS))
    {
	REMOVE_BIT(victim->comm, COMM_NOCHANNELS);
	send_to_char("Áîãè âåðíóëè òåáå ïðèâèëåãèþ ïîëüçîâàíèÿ êàíàëàìè.\n\r", victim);
	send_to_char("NOCHANNELS removed.\n\r", ch);
	sprintf(buf, "$N âîçâðàùàåò %s ïðàâî ïîëüçîâàíèÿ êàíàëàìè", cases(victim->name, 2));
	wiznet(buf, ch, NULL, WIZ_PENALTIES, WIZ_SECURE, 0);
	victim->pcdata->nochan = 0;
    }
    else
    {
	victim->pcdata->nochan = get_next_seconds(argument, ch);
	SET_BIT(victim->comm, COMM_NOCHANNELS);
	send_to_char("Áîãè îòîáðàëè ó òåáÿ ïðèâèëåãèþ ïîëüçîâàíèÿ êàíàëàìè.\n\r", victim);
	send_to_char("NOCHANNELS set.\n\r", ch);
	sprintf(buf, "$N ëèøàåò %s ïðàâà ïîëüçîâàòüñÿ êàíàëàìè.", cases(victim->name, 1));
	wiznet(buf, ch, NULL, WIZ_PENALTIES, WIZ_SECURE, 0);
    }

    return;
}

void do_extranochannels(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH], buf[MAX_STRING_LENGTH];
    CHAR_DATA *victim;

    argument = one_argument(argument, arg);

    if (arg[0] == '\0')
    {
	send_to_char("Ñêðûòü êàíàëû ó êîãî?\n\r", ch);
	return;
    }

    if ((victim = get_char_world(ch, arg)) == NULL || IS_NPC(victim))
    {
	send_to_char("Òàêèõ íåò â ýòîì ìèðå.\n\r", ch);
	return;
    }

    if (IS_IMMORTAL(victim) && (get_trust(victim) >= get_trust(ch)))
    {
	send_to_char("Íå ïîëó÷èëîñü.\n\r", ch);
	return;
    }

    if (IS_SET(victim->comm, COMM_EXTRANOCHANNELS))
    {
	REMOVE_BIT(victim->comm, COMM_EXTRANOCHANNELS);
	send_to_char("EXTRANOCHANNELS removed.\n\r", ch);
	sprintf(buf, "$N âîçâðàùàåò %s ÂÈÄÈÌÎÑÒÜ êàíàëîâ", cases(victim->name, 2));
	wiznet(buf, ch, NULL, WIZ_PENALTIES, WIZ_SECURE, 0);
    }
    else
    {
	SET_BIT(victim->comm, COMM_EXTRANOCHANNELS);
	send_to_char("EXTRANOCHANNELS set.\n\r", ch);
	sprintf(buf, "$N ëèøàåò %s âèäèìîñòè êàíàëîâ.", cases(victim->name, 1));
	wiznet(buf, ch, NULL, WIZ_PENALTIES, WIZ_SECURE, 0);
    }

    return;
}



/*
 void do_smote(CHAR_DATA *ch, char *argument)
 {
 CHAR_DATA *vch;
 char *letter, *name;
 char last[MAX_INPUT_LENGTH], temp[MAX_STRING_LENGTH];
 int matches = 0;

 if (!IS_NPC(ch) && IS_SET(ch->comm, COMM_NOEMOTE))
 {
 send_to_char("Òû íå ìîæåøü ïîêàçûâàòü ñâîè ýìîöèè.\n\r", ch);
 return;
 }

 if (argument[0] == '\0')
 {
 send_to_char("Ïîêàçàòü ÷òî?\n\r", ch);
 return;
 }

 if (strstr(argument, ch->name) == NULL)
 {
 send_to_char("Òû äîëæåí âêëþ÷èòü ñâîå èìÿ â êîìàíäó smote.\n\r", ch);
 return;
 }

 send_to_char(argument, ch);
 send_to_char("\n\r", ch);

 for (vch = ch->in_room->people; vch != NULL; vch = vch->next_in_room)
 {
 if (vch->desc == NULL || vch == ch)
 continue;

 if ((letter = strstr(argument, vch->name)) == NULL)
 {
 send_to_char(argument, vch);
 send_to_char("\n\r", vch);
 continue;
 }

 strcpy(temp, argument);
 temp[strlen(argument) - strlen(letter)] = '\0';
 last[0] = '\0';
 name = vch->name;

 for (; *letter != '\0'; letter++)
 {
 if (*letter == '\'' && matches == strlen(vch->name))
 {
 strcat(temp, "r");
 continue;
 }

 if (*letter == 's' && matches == strlen(vch->name))
 {
 matches = 0;
 continue;
 }

 if (matches == strlen(vch->name))
 {
 matches = 0;
 }

 if (*letter == *name)
 {
 matches++;
 name++;
 if (matches == strlen(vch->name))
 {
 strcat(temp, "you");
last[0] = '\0';
name = vch->name;
continue;
}
strncat(last, letter, 1);
continue;
}

matches = 0;
strcat(temp, last);
strncat(temp, letter, 1);
last[0] = '\0';
name = vch->name;
}

send_to_char(temp, vch);
send_to_char("\n\r", vch);
}

return;
}
*/

void do_bamfin(CHAR_DATA *ch, char *argument)
{
    char buf[MAX_STRING_LENGTH];

    if (!IS_NPC(ch))
    {
	smash_tilde(argument);

	if (argument[0] == '\0')
	{
	    sprintf(buf, "Òâîå ïîÿâëåíèå âûãëÿäèò òàê:\n\r%s\n\r", ch->pcdata->bamfin);
	    send_to_char(buf, ch);
	    return;
	}

	if (strstr(argument, ch->name) == NULL)
	{
	    send_to_char("Òû äîëæåí âêëþ÷èòü ñâîå èìÿ â àðãóìåíò.\n\r", ch);
	    return;
	}

	free_string(ch->pcdata->bamfin);
	ch->pcdata->bamfin = str_dup(argument);

	sprintf(buf, "Òâîå ïîÿâëåíèå òåïåðü âûãëÿäèò òàê:\n\r%s\n\r", ch->pcdata->bamfin);
	send_to_char(buf, ch);
    }
    return;
}

void do_bamfout(CHAR_DATA *ch, char *argument)
{
    char buf[MAX_STRING_LENGTH];

    if (!IS_NPC(ch))
    {
	smash_tilde(argument);

	if (argument[0] == '\0')
	{
	    sprintf(buf, "Òû èñ÷åçàåøü òàê:\n\r%s\n\r", ch->pcdata->bamfout);
	    send_to_char(buf, ch);
	    return;
	}

	if (strstr(argument, ch->name) == NULL)
	{
	    send_to_char("Òû äîëæåí âêëþ÷èòü ñâîå èìÿ â àðãóìåíò.\n\r", ch);
	    return;
	}

	free_string(ch->pcdata->bamfout);
	ch->pcdata->bamfout = str_dup(argument);

	sprintf(buf, "Òåïåðü òû áóäåøü èñ÷åçàòü òàê:\n\r%s\n\r", ch->pcdata->bamfout);
	send_to_char(buf, ch);
    }
    return;
}

void do_cemail(CHAR_DATA *ch, char *argument)
{
    char arg1[MAX_INPUT_LENGTH];
    char arg2[MAX_INPUT_LENGTH];
    CHAR_DATA *victim;

    if (IS_NPC(ch))
		return;

    if (IS_NULLSTR(argument))
    {
		send_to_char("Ñèíòàêñèñ: cemail <char name> <email>.\n\r", ch);
		return;
    }

    argument = one_argument(argument, arg1);
	if (IS_NULLSTR(arg1))
	{
	    send_to_char("Ââåäè èìÿ ÷àðà, êîòîðîìó ìåíÿåøü àäðåñ email.\n\r", ch);
	    return;
	}

	if ((victim = get_char_world(ch, arg1)) == NULL || (IS_NPC(victim)))
	{
	    send_to_char("Òàêèõ â ìèðå íåò.\n\r", ch);
	    return;
	}

	if (IS_IMMORTAL(victim))
	{
	    send_to_char("Òîëüêî íå èììîðòàëó.\n\r", ch);
	    return;
	}

    argument = one_argument(argument, arg2);
	if (IS_NULLSTR(arg2))
	{
	    send_to_char("Ââåäè àäðåñ email.\n\r", ch);
	    return;
	}
	if (!CHECK_EMAIL(arg2))
	{
	    send_to_char("Òàêèõ àäðåñîâ íå áûâàåò... Ïîïðîáóé åùå.\n\r", ch);
	    return;
	}

	free_string(victim->pcdata->email);
	victim->pcdata->email = str_dup(arg2);
	
	send_to_char("Àäðåñ email óñïåøíî èçìåíåí.\n\r", ch);
	return;
}



void do_deny(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH], buf[MAX_STRING_LENGTH];
    CHAR_DATA *victim;

    one_argument(argument, arg);
    if (arg[0] == '\0')
    {
	send_to_char("Çàïðåòèòü/ðàçðåøèòü âõîä êîìó?\n\r", ch);
	return;
    }

    if ((victim = get_char_world(ch, arg)) == NULL)
    {
	send_to_char("Òàêèõ çäåñü íåò.\n\r", ch);
	return;
    }

    if (IS_NPC(victim))
    {
	send_to_char("Òîëüêî íå ìîáà.\n\r", ch);
	return;
    }

    if (get_trust(victim) >= get_trust(ch))
    {
	send_to_char("Íå ïîëó÷èëîñü.\n\r", ch);
	return;
    }


    if (!IS_SET(victim->act, PLR_DENY))
    {
	SET_BIT(victim->act, PLR_DENY);
	send_to_char("Áîãè îòêàçàëè òåáå â äîñòóïå â ýòîò ìèð!\n\r", victim);
	sprintf(buf, "$N çàêðûâàåò äîñòóï %s", cases(victim->name, 2));
	wiznet(buf, ch, NULL, WIZ_PENALTIES, WIZ_SECURE, 0);
	send_to_char("Deny set.\n\r", ch);
	save_char_obj(victim, FALSE);
	stop_fighting(victim, TRUE);
	do_function(victim, &do_quit, "");
    }
    else
    {
	REMOVE_BIT(victim->act, PLR_DENY);
	send_to_char("Áîãè ðàçðåøèëè òåáå äîñòóï â ýòîò ìèð!\n\r", victim);
	sprintf(buf, "$N îòêðûâàåò äîñòóï äëÿ %s", cases(victim->name, 1));
	wiznet(buf, ch, NULL, WIZ_PENALTIES, WIZ_SECURE, 0);
	send_to_char("Deny removed.\n\r", ch);
	save_char_obj(victim, FALSE);
    }

    return;
}

void do_nopk(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH], buf[MAX_STRING_LENGTH];
    CHAR_DATA *victim;

    one_argument(argument, arg);
    if (arg[0] == '\0')
    {
	send_to_char("Çàïðåòèòü/ðàçðåøèòü óáèâàòü êîãî?\n\r", ch);
	return;
    }

    if ((victim = get_char_world(ch, arg)) == NULL)
    {
	send_to_char("Òàêèõ çäåñü íåò.\n\r", ch);
	return;
    }

    if (IS_NPC(victim))
    {
	send_to_char("Òîëüêî íå ìîáà.\n\r", ch);
	return;
    }

    if (get_trust(victim) >= get_trust(ch))
    {
	send_to_char("Íå ïîëó÷èëîñü.\n\r", ch);
	return;
    }

    if (!IS_SET(victim->act, PLR_NOPK))
    {
	SET_BIT(victim->act, PLR_NOPK);
	send_to_char("{RÒåïåðü òåáÿ íå ñìîæåò óáèòü ÷àð.{x\n\r", victim);
	sprintf(buf, "$N çàïðåùàåò óáèâàòü èãðîêàì %s", cases(victim->name, 1));
	wiznet(buf, ch, NULL, WIZ_PENALTIES, WIZ_SECURE, 0);
	send_to_char("Ôëàã ÍÅÓÁÈÒÜ óñòàíîâëåí.\n\r", ch);
	save_char_obj(victim, FALSE);
    }
    else
    {
	REMOVE_BIT(victim->act, PLR_NOPK);
	send_to_char("{RÒåïåðü ÷àðû ñìîãóò òåáÿ óáèâàòü.{x\n\r", victim);
	sprintf(buf, "$N ðàçðåøàåò óáèâàòü èãðîêàì %s", cases(victim->name, 1));
	wiznet(buf, ch, NULL, WIZ_PENALTIES, WIZ_SECURE, 0);
	send_to_char("Ôëàã ÍÅÓÁÈÒÜ ñíÿò.\n\r", ch);
	save_char_obj(victim, FALSE);
    }
    return;
}

void do_success(CHAR_DATA *ch, char *argument) {
    char arg[MAX_INPUT_LENGTH], buf[MAX_STRING_LENGTH];
    CHAR_DATA *victim;

    one_argument(argument, arg);
    if (arg[0] == '\0') {
		send_to_char("Îäîáðèòü êîãî?\n\r", ch);
		return;
    }

    if ((victim = get_char_world(ch, arg)) == NULL){
		send_to_char("Òàêèõ çäåñü íåò.\n\r", ch);
		return;
    }

    if (IS_NPC(victim)){
		send_to_char("Òîëüêî íå ìîáà.\n\r", ch);
		return;
    }

    if (get_trust(victim) > get_trust(ch)){
		send_to_char("Íå ïîëó÷èëîñü.\n\r", ch);
		return;
    }


    if (victim->pcdata->successed == 0){
		victim->pcdata->successed = 1;
		send_to_char("{RÁîãè îäîáðèëè òâîå ïðåáûâàíèå â ýòîì ìèðå.{x\n\r", victim);
		sprintf(buf, "$N îäîáðèë %s", cases(victim->name, 1));
		wiznet(buf, ch, NULL, WIZ_PENALTIES, WIZ_SECURE, 0);
		send_to_char("Îäîáðåíèå ïðîøëî óñïåøíî.\n\r", ch);
		save_char_obj(victim, FALSE);
    } else {
		victim->pcdata->successed = 0;
		send_to_char("{RÁîãè ïåðåñòàëè îäîáðÿòü òâîå ïðåáûâàíèå â ýòîì ìèðå.{x\n\r", victim);
		sprintf(buf, "$N ñíÿë îäîáðåíèå %s", cases(victim->name, 1));
		wiznet(buf, ch, NULL, WIZ_PENALTIES, WIZ_SECURE, 0);
		send_to_char("Ñíÿòèå îäîáðåíèÿ ïðîøëî óñïåøíî.\n\r", ch);
		save_char_obj(victim, FALSE);
    }

    return;
}


void do_full_silence(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH]/*, buf[MAX_STRING_LENGTH]*/;
    CHAR_DATA *victim;

    one_argument(argument, arg);

    if (arg[0] == '\0')
    {
	send_to_char("Êîìó óñòàíîâèòü ïîëíîå ìîë÷àíèå?\n\r", ch);
	return;
    }

    if ((victim = get_char_world(ch, arg)) == NULL)
    {
	send_to_char("Òàêèõ çäåñü íåò.\n\r", ch);
	return;
    }

    if (IS_NPC(victim))
    {
	send_to_char("Òîëüêî íå ìîáó.\n\r", ch);
	return;
    }

    if (!IS_SET(victim->act, PLR_FULL_SILENCE))
    {
	SET_BIT(victim->act, PLR_FULL_SILENCE);
	send_to_char("{RÒåïåðü òû íàâñåãäà ìîë÷èøü.{x\n\r", victim);
	send_to_char("Ôëàã ÌÎË×ÀÍÈß óñòàíîâëåí.\n\r", ch);
	save_char_obj(victim, FALSE);
    }
    else
    {
	REMOVE_BIT(victim->act, PLR_FULL_SILENCE);
	send_to_char("{RÒåïåðü òû ñíîâà ãîâîðèøü.{x\n\r", victim);
	send_to_char("Ôëàã ÌÎË×ÀÍÈß ñíÿò.\n\r", ch);
	save_char_obj(victim, FALSE);
    }
    return;
}

void do_disconnect(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH];
    DESCRIPTOR_DATA *d;
    CHAR_DATA *victim;

    one_argument(argument, arg);
    if (arg[0] == '\0')
    {
	send_to_char("Îòêëþ÷èòü êîãî?\n\r", ch);
	return;
    }

    if (is_number(arg))
    {
	int desc;

	desc = atoi(arg);
	SLIST_FOREACH(d, &descriptor_list, link)
	{
	    if (d->descriptor == desc)
	    {
		close_socket(d);
		send_to_char("Ok.\n\r", ch);
		return;
	    }
	}
    }

    if ((victim = get_char_world(ch, arg)) == NULL)
    {
	send_to_char("Òàêèõ çäåñü íåò.\n\r", ch);
	return;
    }

    if (victim->desc == NULL)
    {
	act("$N doesn't have a descriptor.", ch, NULL, victim, TO_CHAR);
	return;
    }

    SLIST_FOREACH(d, &descriptor_list, link)
    {
	if (d == victim->desc)
	{
	    close_socket(d);
	    send_to_char("Ok.\n\r", ch);
	    return;
	}
    }

    bugf("Do_disconnect: desc not found.");
    send_to_char("Descriptor not found!\n\r", ch);
    return;
}



void do_pardon(CHAR_DATA *ch, char *argument)
{
    char arg1[MAX_INPUT_LENGTH];
    char arg2[MAX_INPUT_LENGTH];
    CHAR_DATA *victim;

    argument = one_argument(argument, arg1);
    argument = one_argument(argument, arg2);

    if (arg1[0] == '\0' || arg2[0] == '\0')
    {
	send_to_char("Syntax: ïðîñòèòü <character> <óáèéöà|âîð|åðåòèê|áðàòîóáèéöà>.\n\r", ch);
	return;
    }

    if ((victim = get_char_world(ch, arg1)) == NULL)
    {
	send_to_char("Òàêèõ çäåñü íåò.\n\r", ch);
	return;
    }

    if (IS_NPC(victim))
    {
	send_to_char("Òîëüêî íå ìîáà.\n\r", ch);
	return;
    }

    if (!str_cmp(arg2, "killer") || !str_cmp(arg2, "óáèéöà"))
    {
	if (IS_SET(victim->act, PLR_KILLER))
	{
	    REMOVE_BIT(victim->act, PLR_KILLER);
	    send_to_char("Ôëàã 'Óáèéöà' ñíÿò.\n\r", ch);
	    send_to_char("{RÁîãè ïðîñòèëè òåáå òâîè óáèéñòâà.{c\n\r", victim);
	}
	return;
    }


    if (!str_cmp(arg2, "thief") || !str_cmp(arg2, "âîð"))
    {
	if (IS_SET(victim->act, PLR_THIEF))
	{
	    REMOVE_BIT(victim->act, PLR_THIEF);
	    send_to_char("Ôëàã 'Âîð' ñíÿò.\n\r", ch);
	    send_to_char("{RÁîãè ïðîñòèëè òåáå òâîå âîðîâñòâî.{x\n\r", victim);
	}
	return;
    }

    if (!str_cmp(arg2, "åðåòèê"))
    {
	if (victim->count_holy_attacks > 0)
	{
	    victim->count_holy_attacks = 0;
	    send_to_char("Ôëàã 'Åðåòèê' ñíÿò.\n\r", ch);
	    send_to_char("{RÁîãè ïðîñòèëè òåáå òâîå åðåòè÷åñòâî.{c\n\r", victim);
	}
	return;
    }

    if (!str_cmp(arg2, "áðàòîóáèéöà"))
    {
	if (victim->count_guild_attacks > 0)
	{
	    victim->count_guild_attacks = 0;
	    send_to_char("Ôëàã 'Áðàòîóáèéöà' ñíÿò.\n\r", ch);
	    send_to_char("{RÃèëüäèÿ ïðîñòèëà òåáÿ.{c\n\r", victim);
	}
	return;
    }


    send_to_char("Syntax: ïðîñòèòü <character> <óáèéöà|âîð|åðåòèê|áðàòîóáèéöà>.\n\r", ch);
    return;
}



void do_echo(CHAR_DATA *ch, char *argument)
{
    DESCRIPTOR_DATA *d;

    if (argument[0] == '\0')
    {
	send_to_char("Ñêàçàòü âñåìó ìèðó ÷òî?\n\r", ch);
	return;
    }

    SLIST_FOREACH(d, &descriptor_list, link)
    {
	if (d->connected == CON_PLAYING)
	{
	    if (get_trust(d->character) >= get_trust(ch))
		send_to_char("{WGlobal>{x ", d->character);
	    
	    send_to_char(argument, d->character);
	    send_to_char("{x\n\r",   d->character);
	}
    }

    return;
}



void do_recho(CHAR_DATA *ch, char *argument)
{
    DESCRIPTOR_DATA *d;

    if (argument[0] == '\0')
    {
	send_to_char("Ñêàçàòü ëîêàëüíî ÷òî?\n\r", ch);

	return;
    }

    SLIST_FOREACH(d, &descriptor_list, link)
    {
	if (d->connected == CON_PLAYING
	    &&   d->character->in_room == ch->in_room)
	{
	    if (get_trust(d->character) >= get_trust(ch))
		send_to_char("{WLocal>{x ", d->character);
	    send_to_char(argument, d->character);
	    send_to_char("{x\n\r",   d->character);
	}
    }

    return;
}

void do_zecho(CHAR_DATA *ch, char *argument)
{
    DESCRIPTOR_DATA *d;

    if (argument[0] == '\0')
    {
	send_to_char("Ñêàçàòü íà âñþ çîíó ÷òî?\n\r", ch);
	return;
    }

    SLIST_FOREACH(d, &descriptor_list, link)
    {
	if (d->connected == CON_PLAYING
	    &&  d->character->in_room != NULL && ch->in_room != NULL
	    &&  d->character->in_room->area == ch->in_room->area)
	{
	    if (get_trust(d->character) >= get_trust(ch))
		send_to_char("{WZone>{x ", d->character);
	    send_to_char(argument, d->character);
	    send_to_char("{x\n\r", d->character);
	}
    }
}

void do_pecho(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH];
    CHAR_DATA *victim;

    argument = one_argument(argument, arg);

    if (argument[0] == '\0' || arg[0] == '\0')
    {
	send_to_char("×òî òû õîòåë ñêàçàòü ïåðñîíàëüíî?\n\r", ch);
	return;
    }

    if  ((victim = get_char_world(ch, arg)) == NULL)
    {
	send_to_char("Òàêèõ íå íàéäåíî.\n\r", ch);
	return;
    }

    if (get_trust(victim) >= get_trust(ch) && get_trust(ch) != MAX_LEVEL)
	send_to_char("{WPersonal>{x ", victim);

    send_to_char(argument, victim);
    send_to_char("\n\r", victim);
    /*    send_to_char("{WPersonal>{x ", ch); */
    send_to_char(argument, ch);
    send_to_char("{x\n\r", ch);
}


ROOM_INDEX_DATA *find_location(CHAR_DATA *ch, char *arg)
{
    CHAR_DATA *victim;
    OBJ_DATA *obj;

    if (is_number(arg))
	return get_room_index(atoi(arg));

    if ((victim = get_char_world(ch, arg)) != NULL)
	return victim->in_room;

    if ((obj = get_obj_world(ch, arg)) != NULL)
	return obj->in_room;

    return NULL;
}



void do_transfer(CHAR_DATA *ch, char *argument)
{
    char arg1[MAX_INPUT_LENGTH];
    char arg2[MAX_INPUT_LENGTH];
    ROOM_INDEX_DATA *location;
    DESCRIPTOR_DATA *d;
    CHAR_DATA *victim, *rch, *safe_rch;

    argument = one_argument(argument, arg1);
    argument = one_argument(argument, arg2);

    if (arg1[0] == '\0')
    {
	send_to_char("Ïåðåìåñòèòü êîãî è êóäà?\n\r", ch);
	return;
    }

    if (!str_cmp(arg1, "all") || !str_cmp(arg1, "âñåõ"))
    {
	SLIST_FOREACH(d, &descriptor_list, link)
	{
	    if (d->connected == CON_PLAYING
		&&   d->character != ch
		&&   d->character->in_room != NULL
		&&   can_see(ch, d->character))
	    {
		char buf[MAX_STRING_LENGTH];
		sprintf(buf, "%s %s", d->character->name, arg2);
		do_function(ch, &do_transfer, buf);
	    }
	}
	return;
    }

    /*
     * Thanks to Grodyn for the optional location parameter.
     */

    if ((victim = get_char_world(ch, arg1)) == NULL)
    {
	send_to_char("Òàêèõ çäåñü íåò.\n\r", ch);
	return;
    }

    if (arg2[0] == '\0')
    {
	location = ch->in_room;
    }
    else if (!str_prefix(arg2, "recall") || !str_prefix(arg2, "âîçâðàò"))
    {
	if (IS_NPC(victim))
	{
	    send_to_char("Ó ìîáîâ âîçâðàò íå ðàáîòàåò.\n\r", ch);
	    return;
	}

	location = get_recall(victim);
    }
    else
    {
	if ((location = find_location(ch, arg2)) == NULL)
	{
	    send_to_char("Êîìíàòû ñ òàêèì íîìåðîì íå îáíàðóæåíî!\n\r", ch);
	    return;
	}

	if (room_is_private(location, MOUNTED(victim))&&  get_trust(ch) < MAX_LEVEL)
	{
	    send_to_char("Ýòî ìåñòî ñåé÷àñ çàíÿòî.\n\r", ch);
	    return;
	}

    }

    if (victim->in_room == NULL)
    {
	send_to_char("Ïîõîæå, ýòîò èãðîê â Ëèìáî.\n\r", ch);
	return;
    }

    if (victim->fighting != NULL)
	stop_fighting(victim, TRUE);

    if (MOUNTED(victim))
    {
	char_from_room(MOUNTED(victim));
	char_to_room(MOUNTED(victim), location, TRUE);
	send_to_char("Òâîåãî âñàäíèêà ïåðåìåñòèëè, ïîýòîìó òû ñëåäóåøü çà íèì.\n\r", MOUNTED(victim));
	kick_pet(MOUNTED(victim));
    }

    if (victim->pet != NULL)
    {
	char_from_room (victim->pet);
	char_to_room (victim->pet, location, TRUE);
	kick_pet(victim->pet);
    }

    /* Åñëè â èììîâñêîì èíâèçå, òî ÷àðó è ïèêíóòü íå äîëæíû, ÷òî â åãî
     * êîìíàòå áûë èìì!
     */
    LIST_FOREACH_SAFE(rch, &victim->in_room->people, room_link, safe_rch)
    {
	if (get_trust(rch) >= victim->invis_level)
	    act("$n âíåçàïíî èñ÷åçàåò â ëåãêîé äûìêå.", victim, NULL, rch, TO_VICT);
    }

    if (ch != victim)
	act("$n ïåðåìåùàåò òåáÿ!", ch, NULL, victim, TO_VICT);

    LIST_FOREACH_SAFE(rch, &location->people, room_link, safe_rch)
    {
	if (get_trust(rch) > victim->invis_level)
	    act("$N âíåçàïíî ïîÿâëÿåòñÿ â êëóáàõ äûìà.", rch, NULL, victim, TO_CHAR);
    }

    char_from_room(victim);
    char_to_room(victim, location, TRUE);

    /*    do_function(victim, &do_look, "auto"); */
    send_to_char("Ok.\n\r", ch);
}



void do_at(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH];
    ROOM_INDEX_DATA *location;
    ROOM_INDEX_DATA *original;
    OBJ_DATA *on;
    CHAR_DATA *wch;

    argument = one_argument(argument, arg);

    if (arg[0] == '\0' || argument[0] == '\0')
    {
	send_to_char("Âûïîëíèòü êàêóþ êîìàíäó ãäå?\n\r", ch);
	return;
    }

    if ((location = find_location(ch, arg)) == NULL)
    {
	send_to_char("Íåò òàêîé êîìíàòû.\n\r", ch);
	return;
    }

    if (room_is_private(location, NULL) &&  get_trust(ch) < MAX_LEVEL)
    {
	send_to_char("Ýòà êîìíàòà ñåé÷àñ çàíÿòà.\n\r", ch);
	return;
    }

    original = ch->in_room;
    on = ch->on;
    char_from_room(ch);
    char_to_room(ch, location, FALSE);
    interpret(ch, argument);

    /*
     * See if 'ch' still exists before continuing!
     * Handles 'at XXXX quit' case.
     */
    LIST_FOREACH(wch, &char_list, link)
    {
	if (wch == ch)
	{
	    OBJ_DATA *obj;

	    char_from_room(ch);
	    char_to_room(ch, original, FALSE);

	    /* See if on still exists before setting ch->on back to it. */
	    for (obj = original->contents; obj; obj = obj->next_content)
	    {
		if (obj == on)
		{
		    ch->on = on;
		    break;
		}
	    }
	    break;
	}
    }

    return;
}



void do_goto(CHAR_DATA *ch, char *argument)
{
    ROOM_INDEX_DATA *location;
    CHAR_DATA *rch, *safe_rch;
    int count = 0;

    if (argument[0] == '\0')
    {
	send_to_char("Ïåðåìåñòèòüñÿ êóäà?\n\r", ch);
	return;
    }

    if ((location = find_location(ch, argument)) == NULL)
    {
	send_to_char("Íåò òàêîé êîìíàòû.\n\r", ch);
	return;
    }

    count = 0;
    LIST_FOREACH(rch, &location->people, room_link)
	count++;

    if (room_is_private(location, MOUNTED(ch))
	&& (count > 1 || get_trust(ch) < MAX_LEVEL))
    {
	send_to_char("Ýòà êîìíàòà ñåé÷àñ çàíÿòà.\n\r", ch);
	return;
    }

    if (ch->fighting != NULL)
	stop_fighting(ch, TRUE);

    LIST_FOREACH_SAFE(rch, &ch->in_room->people, room_link, safe_rch)
    {
	if (get_trust(rch) >= ch->invis_level)
	{
	    if (ch->pcdata != NULL && ch->pcdata->bamfout[0] != '\0')
		act("$t", ch, ch->pcdata->bamfout, rch, TO_VICT);
	    else
		act("$n èñ÷åçàåò â êëóáàõ äûìà.", ch, NULL, rch, TO_VICT);
	}
    }

    char_from_room(ch);

    if (ch->pet != NULL)
    {
	char_from_room (ch->pet);
	char_to_room (ch->pet, location, TRUE);
    }

    if (MOUNTED(ch))
    {
	char_from_room(MOUNTED(ch));
	char_to_room(MOUNTED(ch), location, TRUE);
	send_to_char("Òâîé âñàäíèê - áåññìåðòíûé, è ïðûãíóë êóäà-òî, ïîýòîìó òû ñëåäóåøü çà íèì.\n\r", MOUNTED(ch));
    }

    LIST_FOREACH_SAFE(rch, &location->people, room_link, safe_rch)
    {
	if (get_trust(rch) >= ch->invis_level)
	{
	    if (ch->pcdata != NULL && ch->pcdata->bamfin[0] != '\0')
		act("$t", rch, ch->pcdata->bamfin, ch, TO_CHAR);
	    else
		act("$n ïîÿâëÿåòñÿ â êëóáàõ äûìà.", ch, NULL, rch, TO_VICT);
	}
    }

    char_to_room(ch, location, TRUE);

    return;
}

void do_violate(CHAR_DATA *ch, char *argument)
{
    ROOM_INDEX_DATA *location;
    CHAR_DATA *rch, *safe_rch;

    if (argument[0] == '\0')
    {
	send_to_char("Ïåðåìåñòèòüñÿ êóäà?\n\r", ch);
	return;
    }

    if ((location = find_location(ch, argument)) == NULL)
    {
	send_to_char("Íåò òàêîé êîìíàòû.\n\r", ch);
	return;
    }

    if (!room_is_private(location, NULL))
    {
	send_to_char("Ýòà êîìíàòà íå ïðèâàòíàÿ.\n\r", ch);
	return;
    }

    if (ch->fighting != NULL)
	stop_fighting(ch, TRUE);

    LIST_FOREACH_SAFE(rch, &ch->in_room->people, room_link, safe_rch)
    {
	if (get_trust(rch) >= ch->invis_level)
	{
	    if (ch->pcdata != NULL && ch->pcdata->bamfout[0] != '\0')
		act("$t", ch, ch->pcdata->bamfout, rch, TO_VICT);
	    else
		act("$n èñ÷åçàåò â êëóáàõ äûìà.", ch, NULL, rch, TO_VICT);
	}
    }

    LIST_FOREACH_SAFE(rch, &location->people, room_link, safe_rch)
    {
	if (get_trust(rch) >= ch->invis_level)
	{
	    if (ch->pcdata != NULL && ch->pcdata->bamfin[0] != '\0')
		act("$t", ch, ch->pcdata->bamfin, rch, TO_VICT);
	    else
		act("$n ïîÿâëÿåòñÿ â êëóáàõ äûìà.", ch, NULL, rch, TO_VICT);
	}
    }

    char_from_room(ch);
    char_to_room(ch, location, TRUE);

    /*    do_function(ch, &do_look, "auto"); */
    return;
}

/* RT to replace the 3 stat commands */
void do_stat(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH], arg_all[MIL];
    OBJ_DATA *obj;
    ROOM_INDEX_DATA *location;
    CHAR_DATA *victim;

    strlcpy(arg_all, argument, MIL);
    argument = one_argument(argument, arg);
    if (arg[0] == '\0')
    {
	send_to_char("Syntax:\n\r", ch);
	send_to_char("  stat <name>\n\r", ch);
	send_to_char("  stat obj <name>\n\r", ch);
	send_to_char("  stat mob <name>\n\r", ch);
	send_to_char("  stat room <number>\n\r", ch);
	send_to_char("  ñòàòèñòèêà <name>\n\r", ch);
	send_to_char("  ñòàòèñòèêà îáúåêò <name>\n\r", ch);
	send_to_char("  ñòàòèñòèêà ìîá <name>\n\r", ch);
	send_to_char("  ñòàòèñòèêà êîìíàòà <number>\n\r", ch);
	return;
    }

    if (!str_cmp(arg, "room") || !str_prefix(arg, "êîìíàòà"))
    {
	location = IS_NULLSTR(argument) ? NULL : find_location(ch, argument);
	do_rstat(ch, location);
	return;
    }

    if (!str_cmp(arg, "obj") || !str_prefix(arg, "îáúåêò"))
    {
	if ((obj = get_obj_world(ch, argument)) != NULL)
	    do_ostat(ch, obj);
	else
	    printf_to_char("Îáúåêò íå íàéäåí.\n\r", ch);
	return;
    }

    if (!str_cmp(arg, "char")
	|| !str_cmp(arg, "mob")
	|| !str_cmp(arg, "÷àð")
	|| !str_cmp(arg, "ìîá"))
    {
	if ((victim = get_char_world(ch, argument)) != NULL)
	    do_mstat(ch, victim);
	else
	    printf_to_char("×àð/ìîá íå íàéäåí.\n\r", ch);
	return;
    }

    /* do it the old way */
    obj = get_obj_world(ch, arg_all);
    if (obj != NULL)
    {
	do_ostat(ch, obj);
	return;
    }

    victim = get_char_world(ch, arg_all);
    if (victim != NULL)
    {
	do_mstat(ch, victim);
	return;
    }

    location = find_location(ch, arg_all);
    if (location != NULL)
    {
	do_rstat(ch, location);
	return;
    }

    send_to_char("Íè÷åãî ñ òàêèì èìåíåì íå íàéäåíî.\n\r", ch);
}

void do_rstat(CHAR_DATA *ch, ROOM_INDEX_DATA *location)
{
    char buf[MAX_STRING_LENGTH];
    char buf2[MAX_STRING_LENGTH];
    OBJ_DATA *obj;
    CHAR_DATA *rch, *safe_rch;
    AFFECT_DATA *paf;
    int door;

    location = (location == NULL) ? ch->in_room : location;
    if (location == NULL)
    {
	send_to_char("Íåò òàêîé êîìíàòû.\n\r", ch);
	return;
    }

    if ( ch->in_room != location
	 && room_is_private(location, NULL)
	 && !is_granted(ch, "violate"))
    {
	send_to_char("Ýòî ìåñòî ñåé÷àñ çàíÿòî.\n\r", ch);
	return;
    }

    sprintf(buf, "Name: '%s'\n\rArea: '%s'\n\r",
	    location->name,
	    location->area->name);
    send_to_char(buf, ch);

    sprintf(buf,
	    "Vnum: %d  Sector: %s  Light: {Y%d{x  Healing: {G%d{x  Mana: {M%d{x\n\r",
	    location->vnum,
	    flag_string(sector_flags, location->sector_type, TRUE),
	    location->light,
	    location->heal_rate,
	    location->mana_rate);
    send_to_char(buf, ch);

    sprintf(buf,
	    "Room flags: {m%s{x.\n\rDescription:\n\r%s",
	    flag_string(room_flags, location->room_flags, FALSE),
	    location->description);
    send_to_char(buf, ch);

    sprintf(buf,
	    "Owners: {R%s{x.\n\r", location->owner != NULL ? location->owner : "(none)");
    send_to_char(buf, ch);

    if (location->extra_descr != NULL)
    {
	EXTRA_DESCR_DATA *ed;

	send_to_char("Extra description keywords: '{C", ch);
	for (ed = location->extra_descr; ed; ed = ed->next)
	{
	    send_to_char(ed->keyword, ch);
	    if (ed->next != NULL)
		send_to_char(" ", ch);
	}
	send_to_char("{x'.\n\r", ch);
    }

    send_to_char("Characters:", ch);
    LIST_FOREACH_SAFE(rch, &location->people, room_link, safe_rch)
    {
	if (can_see(ch, rch))
	{
	    one_argument(rch->name, buf);
	    sprintf(buf2, " %s[%d] ", buf, IS_NPC(rch) ? rch->pIndexData->vnum : 0);
	    send_to_char(buf2, ch);
	}
    }

    send_to_char(".\n\rObjects:   ", ch);
    for (obj = location->contents; obj; obj = obj->next_content)
    {
	one_argument(obj->name, buf);
	sprintf(buf2, " %s[%d]", buf, obj->pIndexData->vnum);
	send_to_char(buf2, ch);
    }
    send_to_char(".\n\r", ch);

    for (door = 0; door <= 5; door++)
    {
	EXIT_DATA *pexit;

	if ((pexit = location->exit[door]) != NULL)
	{
	    sprintf(buf,
		    "Door: {y%d{x.  To: {y%d{x.  Key: {g%d{x.  Exit flags: {c%s{x.\n\rKeyword: '{y%s{x'.  Description: {g%s{x",

		    door,
		    (pexit->u1.to_room == NULL ? -1 : pexit->u1.to_room->vnum),
		    pexit->key,
		    flag_string(exit_flags, pexit->exit_info, FALSE),
		    pexit->keyword,
		    pexit->description[0] != '\0'
		    ? pexit->description : "(none).\n\r");
	    send_to_char(buf, ch);
	}
    }

    for (paf = location->affected; paf != NULL; paf = paf->next)
    {
	sprintf(buf, "Affects {y%s{x by {y%d{x, level {y%d{x",
		flag_string(room_apply_flags, paf->location, FALSE), paf->modifier, paf->level);
	send_to_char(buf, ch);
	if (paf->duration > -1)
	{
	    sprintf(buf, ", {y%d{x hours", paf->duration);
	    send_to_char(buf, ch);
	}
	send_to_char(".\n\r", ch);
    }

    return;
}



void do_ostat(CHAR_DATA *ch, OBJ_DATA *obj)
{
    AFFECT_DATA *paf;
    int i;

    printf_to_char("Name(s): %s Count: {c%d{x\n\r",
		   ch, obj->name, obj->pIndexData->count);

    printf_to_char("Vnum: {y%d{x  Format: %s  Type: %s  Resets: %d\n\r",
		   ch, obj->pIndexData->vnum, obj->pIndexData->new_format ? "new" : "old",
		   item_name(obj->item_type, 1), obj->pIndexData->reset_num);

    // âñòàâèë âûäà÷ó ñîîáùåíèÿ, åñëè âåùü áûëà óëó÷øåíà
    if (obj->enchanted)
    {
	send_to_char("{RÝòà âåùü áûëà óëó÷øåíà àðòåôàêòîì/êàñòîì.{x\n\r", ch);
    }

    if (obj->pIndexData->edited)
    {
	printf_to_char("{RÝòà âåùü ðåäàêòèðóåòñÿ â OLC (%d).{x\n\r", ch, obj->pIndexData->edited);
    }

    printf_to_char("Short description: %s\n\rLong description: %s\n\r",
		   ch, obj->short_descr, obj->description);

    printf_to_char("Wear bits: {y%s{x\n\r",
		   ch, flag_string(wear_flags, obj->wear_flags, FALSE));

    printf_to_char("Extra bits: {g%s{x\n\r",
		   ch, flag_string(extra_flags, obj->extra_flags, FALSE));

    printf_to_char("Uncomfortable for: {y%s{x\n\r",
		   ch, flag_string(comf_flags, obj->uncomf, FALSE));

    printf_to_char("Unusable for: {Y%s{x\n\r",
		   ch, flag_string(comf_flags, obj->unusable, FALSE));

    printf_to_char("Number: {c%d{x/{C%d{x  Weight: {y%d{x/{y%d{x/{y%d{x  Material: %s (%d)\n\r",
		   ch, 1, get_obj_number(obj),
		   obj->weight/10, get_obj_weight(obj)/10, get_true_weight(obj)/10,
		   obj->material, material_lookup(obj->material));

    printf_to_char("Level: {y%d{x  Cost: {Y%d{x  Condition: {r%d{x  Timer: {R%d{x\n\r",
		   ch, obj->level, obj->cost, obj->condition, obj->timer);

    printf_to_char("In room: {y%d{x  In object: {g%s{x  "
		   "Wear_loc: {y%d{x\n\rCarried by: {c%s{x  Owner: {c%s{x Person: {c%s{x\n\r",
		   ch, obj->in_room == NULL ? 0 : obj->in_room->vnum,
		   obj->in_obj == NULL ? "(none)" : obj->in_obj->short_descr,
		   obj->wear_loc,
		   obj->carried_by == NULL ? "(none)" :
		   (can_see(ch, obj->carried_by) ? obj->carried_by->name : "someone"),
		   (!IS_NULLSTR(obj->owner) ? capitalize(obj->owner) : "(none)"),
		   (!IS_NULLSTR(obj->person) ? obj->person : "(none)"));

    printf_to_char("Values: {y%d %d %d %d %d{x\n\r",
		   ch, obj->value[0], obj->value[1], obj->value[2], obj->value[3],
		   obj->value[4]);

    send_to_char("Requirements: ", ch);

    for (i = 0; i < MAX_STATS; i++)
	if (obj->require[i] > 0)
	{
	    printf_to_char("%s:%d ", ch, flag_string(attr_flags, i, FALSE), obj->require[i]);
	}

    send_to_char("\n\r", ch);

    /* now give out vital statistics as per identify */

    switch (obj->item_type)
    {
    case ITEM_SCROLL:
    case ITEM_POTION:
    case ITEM_PILL:
	printf_to_char("Level {y%d{x spells of:", ch, obj->value[0]);

	if (obj->value[1] >= 0 && obj->value[1] < max_skills)
	{
	    send_to_char(" '{m", ch);
	    send_to_char(skill_table[obj->value[1]].name, ch);
	    send_to_char("{x'", ch);
	}

	if (obj->value[2] >= 0 && obj->value[2] < max_skills)
	{
	    send_to_char(" '{m", ch);
	    send_to_char(skill_table[obj->value[2]].name, ch);
	    send_to_char("{x'", ch);
	}

	if (obj->value[3] >= 0 && obj->value[3] < max_skills)
	{
	    send_to_char(" '{m", ch);
	    send_to_char(skill_table[obj->value[3]].name, ch);
	    send_to_char("{x'", ch);
	}

	if (obj->value[4] >= 0 && obj->value[4] < max_skills)
	{
	    send_to_char(" '{m", ch);
	    send_to_char(skill_table[obj->value[4]].name, ch);
	    send_to_char("{x'", ch);
	}

	send_to_char(".\n\r", ch);
	break;

    case ITEM_WAND:
    case ITEM_STAFF:
	printf_to_char("Has {r%d{x({g%d{x) charges of level {y%d{x",
		       ch, obj->value[1], obj->value[2], obj->value[0]);

	if (obj->value[3] >= 0 && obj->value[3] < max_skills)
	{
	    send_to_char(" '{m", ch);
	    send_to_char(skill_table[obj->value[3]].name, ch);
	    send_to_char("{x'", ch);
	}

	send_to_char(".\n\r", ch);
	break;
    case ITEM_ROD:
	printf_to_char("Has charge of level {y%d{x '{m%s{x'. Recharge time {r%d{x({g%d{x)\n\r",
		       ch, obj->value[0], obj->value[1] > 0 ? skill_table[obj->value[1]].name : "none",
		       obj->value[3], obj->value[2]);

	break;

    case ITEM_TRAP:
	printf_to_char("Damage {y%d{xd{y%d{x (average {g%d{x)\n\rActivation level: {C%d{x\n\r",
		       ch, obj->value[0], obj->value[1],
		       (1 + obj->value[0]) * obj->value[1] / 2,
		       obj->value[2]);
	printf_to_char("Damage type {y%s{x, delay %d battle rounds\n\r",
		       ch, flag_string(dam_flags, obj->value[3], TRUE),
		       obj->value[4] / PULSE_VIOLENCE);
	break;

    case ITEM_DRINK_CON:
	printf_to_char("It holds {y%s{x. Color %s.\n\r",
		       ch, liq_table[obj->value[2]].liq_rname,
		       liq_table[obj->value[2]].liq_color);
	break;


    case ITEM_WEAPON:
	printf_to_char("Weapon type is: {c%s{x   ", ch, flag_string(weapon_class, obj->value[0], FALSE));
	if (obj->pIndexData->new_format)
	    printf_to_char("Damage is {y%d{xd{y%d{x (average {g%d{x)\n\r",
			   ch, obj->value[1], obj->value[2],
			   (1 + obj->value[2]) * obj->value[1] / 2);
	else
	    printf_to_char("Damage is %d to %d (average %d)\n\r",
			   ch, obj->value[1], obj->value[2],
			   (obj->value[1] + obj->value[2]) / 2);

	printf_to_char("Damage noun is {y%s{x.\n\r",
		       ch, (obj->value[3] > 0 && obj->value[3] < MAX_DAMAGE_MESSAGE) ?
		       attack_table[obj->value[3]].noun : "undefined");

	if (obj->value[4])  /* weapon flags */
	{
	    printf_to_char("Weapons flags: {c%s{x\n\r",
			   ch, flag_string(weapon_type2, obj->value[4], TRUE));
	}
	break;

    case ITEM_ARMOR:
	printf_to_char(
		       "Armor class is {g%d{x pierce, {g%d{x bash, "
		       "{g%d{x slash, and {g%d{x other. \n\r",
		       ch, obj->value[0], obj->value[1], obj->value[2], obj->value[3]);
	break;

    case ITEM_CONTAINER:
	printf_to_char("Weight capacity: {y%d#{x  Max weight: {y%d#{x  flags: {y%s{x\n\r",
		       ch, obj->value[0], obj->value[3],
		       flag_string(container_flags, obj->value[1], TRUE));
	if (obj->value[4] != 100)
	{
	    printf_to_char("Weight multiplier: {y%d%%{x\n\r",
			   ch, obj->value[4]);
	}
	break;
    case ITEM_CHEST:
	printf_to_char("Weight capacity: {y%d#{x  Max weight: {y%d#{x  flags: {y%s{x\n\r",
		       ch, obj->value[0], obj->value[3],
		       flag_string(chest_flags, obj->value[1], TRUE));
	if (obj->value[4] != 100)
	{
	    printf_to_char("Weight multiplier: {y%d%%{x\n\r",
			   ch, obj->value[4]);
	}
	break;
    case ITEM_MORTAR:
	printf_to_char("Weight capacity: {y%d#{x  Max weight: {y%d#{x  Max. num: {y%d{x\n\r",
		       ch, obj->value[0], obj->value[3], obj->value[2]);
	printf_to_char("Flags: {y%s{x\n\r",
		       ch, flag_string(chest_flags, obj->value[1], TRUE));
	if (obj->value[4] != 100)
	{
	    printf_to_char("Weight multiplier: {y%d%%{x\n\r",
			   ch, obj->value[4]);
	}
	break;
    case ITEM_INGREDIENT:
	break;
    }


    if (obj->extra_descr != NULL || obj->pIndexData->extra_descr != NULL)
    {
	EXTRA_DESCR_DATA *ed;

	send_to_char("{gExtra description keywords:{x '", ch);

	for (ed = obj->extra_descr; ed != NULL; ed = ed->next)
	{
	    send_to_char(ed->keyword, ch);
	    if (ed->next != NULL)
		send_to_char(" ", ch);
	}

	for (ed = obj->pIndexData->extra_descr; ed != NULL; ed = ed->next)
	{
	    send_to_char(ed->keyword, ch);
	    if (ed->next != NULL)
		send_to_char(" ", ch);
	}

	send_to_char("'\n\r", ch);
    }

    for (paf = obj->affected; paf != NULL; paf = paf->next)
    {
	printf_to_char("Copy Affects {y%s{x by {y%d{x, level {y%d{x",
		       ch, flag_string(apply_flags, paf->location, FALSE), paf->modifier, paf->level);
	if (paf->duration > -1)
	    printf_to_char(", {y%d{x hours.\n\r", ch, paf->duration);
	else
	    printf_to_char(".\n\r", ch);

	if (paf->bitvector)
	{
	    switch(paf->where)
	    {
	    case TO_AFFECTS:
		printf_to_char("{gAdds %s affect.\n{x",
			       ch, flag_string(affect_flags, paf->bitvector, FALSE));
		break;
	    case TO_WEAPON:
		printf_to_char("{cAdds %s weapon flags.{x\n",
			       ch, flag_string(weapon_type2, paf->bitvector, FALSE));
		break;
	    case TO_OBJECT:
		printf_to_char("{yAdds %s object flag.{x\n",
			       ch, flag_string(extra_flags, paf->bitvector, FALSE));
		break;
	    case TO_RESIST:
		printf_to_char("{YModifies resistance to %s by %d%%.{x\n\r",
			       ch, flag_string(dam_flags, paf->bitvector, FALSE),
			       paf->modifier);
		break;
	    default:
		printf_to_char("Unknown bit %d: %ld\n\r",
			       ch, paf->where, paf->bitvector);
		break;
	    }
	}
    }

    if (!obj->enchanted && !obj->pIndexData->edited)
	for (paf = obj->pIndexData->affected; paf != NULL; paf = paf->next)
	{
	    printf_to_char("Proto Affects {y%s{x by {y%d{x, level {y%d{x.\n\r",
			   ch,
			   flag_string(apply_flags, paf->location, FALSE), paf->modifier, paf->level);
	    if (paf->bitvector)
	    {
		switch(paf->where)
		{
		case TO_AFFECTS:
		    printf_to_char("{gAdds %s affect.{x\n",
				   ch, flag_string(affect_flags, paf->bitvector, FALSE));
		    break;
		case TO_OBJECT:
		    printf_to_char("{yAdds %s object flag.{x\n",
				   ch, flag_string(extra_flags, paf->bitvector, FALSE));
		    break;
		case TO_RESIST:
		    printf_to_char("{YModifies resistance to %s by %d%%.{x\n\r",
				   ch, flag_string(dam_flags, paf->bitvector, FALSE),
				   paf->modifier);
		    break;
		default:
		    printf_to_char("Unknown bit %d: %ld\n\r",
				   ch, paf->where, paf->bitvector);
		    break;
		}
	    }
	}

    if (is_limit(obj) != -1)
    {
	send_to_char("\n\r{RÂ ýòîì ìèðå êîëè÷åñòâî ýòèõ âåùåé îãðàíè÷åíî.\n\r{x", ch);
	if (obj->lifetime > 0)
	{
	    printf_to_char("{rÂðåìÿ æèçíè:{x %s", ch, ctime(&obj->lifetime));
	}
    }

    return;
}



void do_mstat(CHAR_DATA *ch, CHAR_DATA *victim)
{
    char buf[MAX_STRING_LENGTH];
    char buf2[MAX_STRING_LENGTH];
    AFFECT_DATA *paf;
    CHAR_DATA *gch;
    char *descr;
    int i;

    sprintf(buf, "Name: %s Id: %ld\n\r",
	    str_color_fmt(victim->name, 40), victim->id);
    send_to_char(buf, ch);

    if (!IS_NPC(victim))
    {
	sprintf(buf, "Cases: %s/%s/%s/%s/%s\n\r",
		victim->pcdata->cases[0], victim->pcdata->cases[1],
		victim->pcdata->cases[2], victim->pcdata->cases[3],
		victim->pcdata->cases[4]);
	send_to_char(buf, ch);
	sprintf(buf, "Old Names: %s\n\r", victim->pcdata->old_names);
	send_to_char(buf, ch);
    }

    sprintf(buf,
	    "Vnum: %d  Format: %s  Race: %s  Group: %d  Sex: %s  Room: %d\n\r",
	    IS_NPC(victim) ? victim->pIndexData->vnum : 0,
	    IS_NPC(victim)
	    ? (victim->pIndexData->new_format ? "new" : "old") : "pc",
	    race_table[victim->race].name,
	    IS_NPC(victim) ? victim->group : 0,
	    sex_table[victim->sex].name,
	    victim->in_room == NULL ? 0 : victim->in_room->vnum);
    send_to_char(buf, ch);

    if (IS_NPC(victim))
	sprintf(buf, "Count: %d  Killed: %d\n\r",
		victim->pIndexData->count, victim->pIndexData->killed);
    else
	sprintf(buf, "Generated from: {y%s {xat {y%s{x\r",
		victim->pcdata->genip, ctime((time_t *)&victim->id));

    send_to_char(buf, ch);

    if (!IS_NPC(victim) && is_clan(victim))
    {
	sprintf(buf, "Clan: %s\n\r", clan_table[victim->clan].short_descr);
	send_to_char(buf, ch);
    }

    sprintf(buf, "Str: {c%d{w({C%d{w)  Int: {c%d{w({C%d{w)  "
	    "Wis: {c%d{w({C%d{w)  Dex: {c%d{w({C%d{w)  "
	    "Con: {c%d{w({C%d{w)\n\r",
	    victim->perm_stat[STAT_STR],
	    get_curr_stat(victim, STAT_STR),
	    victim->perm_stat[STAT_INT],
	    get_curr_stat(victim, STAT_INT),
	    victim->perm_stat[STAT_WIS],
	    get_curr_stat(victim, STAT_WIS),
	    victim->perm_stat[STAT_DEX],
	    get_curr_stat(victim, STAT_DEX),
	    victim->perm_stat[STAT_CON],
	    get_curr_stat(victim, STAT_CON));
    send_to_char(buf, ch);

    sprintf(buf, "Hp: {g%d/{G%d  {wMana: {m%d/{M%d  {wMove: {c%d/{C%d{x\n\r",
	    victim->hit,         victim->max_hit,
	    victim->mana,        victim->max_mana,
	    victim->move,        victim->max_move);
    send_to_char(buf, ch);

    if (!IS_NPC(ch))
    {
	sprintf(buf, "{wPractices: {y%2d{x Trains: {y%2d{x   Åðåòè÷åñòâî: {r%d{x  Áðàòîóáèéñòâî: {r%d{x Atheist: {r%s{x\n\r",
		victim->practice, victim->train,
		victim->count_holy_attacks,
		victim->count_guild_attacks,
	    	IS_NPC(victim) ? "mobile" : (victim->pcdata->atheist ? "yes" : "no"));
	send_to_char(buf, ch);
    }

    sprintf(buf,
	    "Lv: {y%d(%d){x  Class: {y%s{x  Align: {y%d  {wOriginal_Align: {y%d  {wGold: {Y%ld  {wSilver: {W%ld\n\r{wExp: {Y%d  {wPerLevel: {y%d Platinum: {Y%d{x Bronze: {r%d{x\n\r",
	    victim->level, IS_NPC(victim) ? victim->level : victim->pcdata->max_level,
	    IS_NPC(victim) ? "mobile" : class_table[victim->classid].name,
	    victim->alignment,
	    !IS_NPC(victim) ? victim->pcdata->orig_align : 0,
	    victim->gold, victim->silver, victim->exp,
	    (!IS_NPC(victim) && !IS_IMMORTAL(victim)) ? exp_per_level(victim, victim->pcdata->points) : 0,
	    !IS_NPC(victim) ? victim->pcdata->platinum : 0,		
	    !IS_NPC(victim) ? victim->pcdata->bronze : 0)	;
    send_to_char(buf, ch);

    sprintf(buf, "Armor: pierce: %d  bash: %d  slash: %d  other: %d\n\r",
	    GET_AC(victim, AC_PIERCE), GET_AC(victim, AC_BASH),
	    GET_AC(victim, AC_SLASH),  GET_AC(victim, AC_EXOTIC));
    send_to_char(buf, ch);

    sprintf(buf,
	    "Hit: {y%d  {wDam: {y%d  {wSaves: {g%d  {wSize: %s  Position: {y%s  {wWimpy: {r%d{w\n\r",
	    (int) GET_HITROLL(victim), (int) GET_DAMROLL(victim), (int) (IS_NPC(victim) ? victim->saving_throw : ((1.15+((victim->saving_throw/2)*0.005))*victim->saving_throw)),
	    size_table[victim->size].name, position_table[victim->position].name,
	    victim->wimpy);
    send_to_char(buf, ch);

    if (IS_NPC(victim) && victim->pIndexData->new_format)
    {
	sprintf(buf, "Damage: {y%dd%d  {wMessage:  {y%s{w\n\r",
		victim->damage[DICE_NUMBER], victim->damage[DICE_TYPE],
		attack_table[victim->dam_type].noun);
	send_to_char(buf, ch);
    }

    if (!IS_NPC(victim) && victim->pcdata)
	printf_to_char("{RAverage Damage:{x %d\n\r", ch, victim->pcdata->avg_dam);

    sprintf(buf, "Fighting: {r%s{w  Wait: {r%d{x  Delay: {r%d{x\n\r",
	    victim->fighting ? victim->fighting->name : "(none)", victim->wait, victim->mprog_delay);
    send_to_char(buf, ch);

    if (!IS_NPC(victim))
    {
	sprintf(buf,
		"Thirst: %d  Hunger: %d  Full: %d  Drunk: %d\n\r",
		victim->pcdata->condition[COND_THIRST],
		victim->pcdata->condition[COND_HUNGER],
		victim->pcdata->condition[COND_FULL],
		victim->pcdata->condition[COND_DRUNK]);
	send_to_char(buf, ch);
    }

    sprintf(buf, "Carry number: %d  Carry weight: %ld\n\r",
	    victim->carry_number, get_carry_weight(victim) / 10);
    send_to_char(buf, ch);

    if (!IS_NPC(victim))
    {
	sprintf(buf,
		"Age: {y%d{x  Played: {y%ld{x  Last Level: {y%d{x  Timer: {y%d{x\n\r",
		get_age(victim),
		(long)(victim->played + current_time - victim->logon) / 3600,
		victim->pcdata->last_level,
		victim->timer);
	send_to_char(buf, ch);
    }

    sprintf(buf, "Act: {y%s{x\n\r", flag_string(IS_NPC(victim) ? act_flags : plr_flags, victim->act, TRUE));
    send_to_char(buf, ch);

    if (!IS_NPC(victim))
    {
	if (check_freeze(victim))
	{
	    sprintf(buf, "Freezed by %s up to: {C%s{x", victim->pcdata->freeze_who, ctime(&victim->pcdata->freeze));
	    send_to_char(buf, ch);
	}

	sprintf(buf, "QP: {W%d{w(%d){x\n\r", victim->pcdata->quest_curr, victim->pcdata->quest_accum);
	send_to_char(buf, ch);
    }

    if (victim->comm && !IS_NPC(victim))
    {
	sprintf(buf, "Comm: {m%s{x\n\r", flag_string(comm_flags, victim->comm, TRUE));
	send_to_char(buf, ch);
    }

    if (!IS_NPC(victim) && check_channels(victim))
    {
	sprintf(buf, "Channels revoked up to: {C%s{x",
		ctime(&victim->pcdata->nochan));
	send_to_char(buf, ch);
    }

    if (!IS_NPC(victim)
	&& IS_SET(victim->comm, COMM_NONOTES)
	&& victim->pcdata->nonotes > 0)
    {
	sprintf(buf, "Can't write messages till: {C%s{x",
		ctime(&victim->pcdata->nonotes));
	send_to_char(buf, ch);
    }

    if (!IS_NPC(victim)
	&& IS_SET(victim->comm, COMM_NOTITLE)
	&& victim->pcdata->notitle > 0)
    {
	sprintf(buf, "Can't change title till: {C%s{x",
		ctime(&victim->pcdata->notitle));
	send_to_char(buf, ch);
    }

    if (IS_NPC(victim) && victim->off_flags)
    {
	sprintf(buf, "Offense: {y%s{x\n\r", flag_string(off_flags, victim->off_flags, TRUE));
	send_to_char(buf, ch);
    }

    for (i = 0; i < DAM_MAX; i++)
    {
	if (victim->resists[i] != 0)
	{
	    sprintf(buf, "Resist: {y%-20.20s %s%4d%%{x\n\r",
		    flag_string(dam_flags, i, TRUE),
		    get_resist(victim, i) == get_max_resist(victim, i)
		    ? "{G"
		    : (victim->resists[i] >= 0
		       ? "{Y"
		       : "{R"),
		    get_resist(victim, i));
	    send_to_char(buf, ch);
	}
    }

    sprintf(buf, "Form: %s\n\r", flag_string(form_flags, victim->form, FALSE));
    send_to_char(buf, ch);
    sprintf(buf, "Parts: %s\n\r",
	    flag_string(part_flags, victim->parts, FALSE));
    send_to_char(buf, ch);

    if (victim->affected_by)
    {
	sprintf(buf, "Affected by %s\n\r",
		flag_string(affect_flags, victim->affected_by, TRUE));
	send_to_char(buf, ch);
    }

    sprintf(buf, "Master: %s  Leader: %s  Pet: %s  Horse: %s\n\r",
	    victim->master      ? victim->master->name   : "(none)",
	    victim->leader      ? victim->leader->name   : "(none)",
	    victim->pet 	    ? victim->pet->name	     : "(none)",
	    victim->mount 	    ? victim->mount->name    : "(none)");
    send_to_char(buf, ch);

    //âñòàâèë - äëÿ ÷àðà ïîêàçûâàåòñÿ èìÿ ñóïðóãà, åñëè òàêîâûé åñòü. Íó è ïîêàçûâàåòñÿ îòâåò íà âîïðîñ.
    if (!IS_NPC(victim))
    {
	sprintf(buf, "Marry: %s \n\r",
		!IS_NULLSTR(victim->pcdata->spouse)  ? victim->pcdata->spouse   : "(none)");
	send_to_char(buf, ch);
    }

    if (!IS_NPC(victim))
    {
	sprintf(buf, "Control Answer: %s \n\r",	!IS_NULLSTR(victim->pcdata->reg_answer) ? victim->pcdata->reg_answer : "(none)");
	send_to_char(buf, ch);
    }


    //Äëÿ ÷àðîâ ïîêàçûâàåòñÿ íàëè÷èå äîìà. Åñëè òàêîé åñòü, åñëè æå âíóì 0 - òî íåòó.
    if (!IS_NPC(victim))
    {
	sprintf(buf, "House: %d \n\r",	victim->pcdata->vnum_house);
	send_to_char(buf, ch);
    }


    if (!IS_NPC(victim))
    {
	sprintf(buf, "Security: {g%d{x   E-mail: {y%s{x\n\r",
		victim->pcdata->security,
		!IS_NULLSTR(victim->pcdata->email) ? victim->pcdata->email :"");	/* OLC */
	send_to_char(buf, ch);					/* OLC */
    }
    else
    {
	send_to_char("Areal: {y", ch);

	for(i = 0; i < SECT_MAX; i++)
	    if (victim->pIndexData->areal[i])
	    {
		sprintf(buf, "%s ", flag_string(sector_flags, i, TRUE));
		send_to_char(buf, ch);
	    }

	send_to_char("{x\n\r", ch);
    }

    sprintf(buf, "Short description: %s\n\rLong  description:\n\r",
	    victim->short_descr);
    send_to_char(buf, ch);

    descr = IS_NPC(victim) ? victim->long_descr : victim->description;

    send_to_char(!IS_NULLSTR(descr) ? descr : "(none)\n\r", ch);

    if (IS_NPC(victim) && victim->spec_fun != 0)
    {
	sprintf(buf, "Mobile has special procedure %s.\n\r",
		spec_name(victim->spec_fun));
	send_to_char(buf, ch);
    }

    if (IS_NPC(victim))
    {
	MEM_DATA *mem;

	for (mem = victim->memory; mem; mem = mem->next)
	{
	    sprintf(buf, "Memory: {r%s{x\n\r",
		    (gch = pc_id_lookup(mem->id))
		    ? gch->name : "{R(æåðòâû íåò â Ìèðå)");
	    send_to_char(buf, ch);
	}
    }

    for (paf = victim->affected; paf != NULL; paf = paf->next)
    {
	CHAR_DATA *caster;

	caster = pc_id_lookup(paf->caster_id);
	if (paf->where != TO_RESIST)
	{
	    strlcpy(buf2, flag_string(affect_flags, paf->bitvector, TRUE), sizeof(buf2));
	    sprintf(buf, "Spell: '%s' modifies %s by %d for %d hours with "
		    "bits %s, level %d, caster %s.\n\r",
		    paf->type > 0 ? skill_table[(int) paf->type].name : "",
		    flag_string(apply_flags, paf->location, TRUE),
		    paf->modifier,
		    paf->duration,
		    buf2,
		    paf->level, caster ? caster->name : "unknown");
	}
	else
	{
	    sprintf(buf, "Spell: '%s' modifies resistance to %s by %d%% for "
		    "%d hours, level %d, caster %s.\n\r",
		    paf->type > 0 ? skill_table[(int) paf->type].name : "",
		    flag_string(dam_flags, paf->bitvector, FALSE),
		    paf->modifier,
		    paf->duration,
		    paf->level, caster ? caster->name : "unknown");
	}
	send_to_char(buf, ch);
    }

    return;
}

/* ofind and mfind replaced with vnum, vnum skill also added */

void do_vnum(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH];
    char *string;

    string = one_argument(argument, arg);

    if (arg[0] == '\0')
    {
	send_to_char("Syntax:\n\r", ch);
	send_to_char("  vnum obj <name>\n\r", ch);
	send_to_char("  vnum mob <name>\n\r", ch);
	send_to_char("  vnum skill <skill or spell>\n\r", ch);
	send_to_char("  âíóì îáúåêò <name>\n\r", ch);
	send_to_char("  âíóì ìîá <name>\n\r", ch);
	send_to_char("  âíóì óìåíèå <skill or spell>\n\r", ch);
	return;
    }

    if (!str_cmp(arg, "obj"))
    {
	do_function(ch, &do_ofind, string);
	return;
    }

    if (!str_cmp(arg, "mob") || !str_cmp(arg, "char") || !str_cmp(arg, "ìîá") || !str_cmp(arg, "÷àð"))
    {
	do_function(ch, &do_mfind, string);
	return;
    }

    if (!str_cmp(arg, "skill") || !str_cmp(arg, "spell") || !str_prefix(arg, "óìåíèÿ") || !str_prefix(arg, "çàêëèíàíèå"))
    {
	do_function (ch, &do_slookup, string);
	return;
    }
    /* do both */
    send_to_char("Ìîáû:\n\r", ch);
    do_function(ch, &do_mfind, argument);
    send_to_char("\n\rÎáúåêòû:\n\r", ch);
    do_function(ch, &do_ofind, argument);
}


void do_mfind(CHAR_DATA *ch, char *argument)
{
    extern int top_mob_index;
    char buf[MAX_STRING_LENGTH];
    char arg[MAX_INPUT_LENGTH];
    MOB_INDEX_DATA *pMobIndex;
    int vnum;
    int nMatch, max_found = 100, count = 0;
    bool found;

    one_argument(argument, arg);
    if (arg[0] == '\0')
    {
	send_to_char("Íàéòè êîãî?\n\r", ch);
	return;
    }

    found	= FALSE;
    nMatch	= 0;

    /*
     * Yeah, so iterating over all vnum's takes 10, 000 loops.
     * Get_mob_index is fast, and I don't feel like threading another link.
     * Do you?
     * -- Furey
     */
    for (vnum = 0; nMatch < top_mob_index; vnum++)
    {
	if ((pMobIndex = get_mob_index(vnum)) != NULL)
	{
	    nMatch++;
	    if (is_name(argument, pMobIndex->player_name))
	    {
		found = TRUE;
		sprintf(buf, "[%5d] %s\n\r",
			pMobIndex->vnum, pMobIndex->short_descr);
		send_to_char(buf, ch);

		if (count >= max_found)
		    break;
	    }
	}
    }

    if (!found)
	send_to_char("Íå íàéäåíî íèêîãî ñ òàêèì èìåíåì.\n\r", ch);

    return;
}



void do_ofind(CHAR_DATA *ch, char *argument)
{
    extern int top_obj_index;
    char buf[MAX_STRING_LENGTH];
    char arg[MAX_INPUT_LENGTH];
    OBJ_INDEX_DATA *pObjIndex;
    int vnum;
    int nMatch, max_count = 100, count = 0;
    bool found;

    one_argument(argument, arg);
    if (arg[0] == '\0')
    {
	send_to_char("Íàéòè ÷òî?\n\r", ch);
	return;
    }

    found	= FALSE;
    nMatch	= 0;

    /*
     * Yeah, so iterating over all vnum's takes 10, 000 loops.
     * Get_obj_index is fast, and I don't feel like threading another link.
     * Do you?
     * -- Furey
     */
    for (vnum = 0; nMatch < top_obj_index; vnum++)
    {
	if ((pObjIndex = get_obj_index(vnum)) != NULL)
	{
	    nMatch++;
	    if (is_name(argument, pObjIndex->name))
	    {
		found = TRUE;
		sprintf(buf, "[%5d] %s\n\r",
			pObjIndex->vnum, pObjIndex->short_descr);
		send_to_char(buf, ch);

		if (count >= max_count)
		    break;
	    }
	}
    }

    if (!found)
	send_to_char("Íåò îáúåêòîâ ñ òàêèì èìåíåì.\n\r", ch);

    return;
}


void do_owhere(CHAR_DATA *ch, char *argument)
{
    char buf[MAX_INPUT_LENGTH];
    char buf1[10];
    BUFFER *buffer;
    OBJ_DATA *obj;
    OBJ_DATA *in_obj;
    bool found;
    int number = 0, max_found;
    int vnum = 0;

    found = FALSE;
    number = 0;
    max_found = 100;

    buffer = new_buf();

    if (argument[0] == '\0')
    {
	send_to_char("Íàéòè ÷òî?\n\r", ch);
	free_buf(buffer);
	return;
    }

    if (is_number(argument))
	vnum = atoi(argument);

    for (obj = object_list; obj != NULL; obj = obj->next)
    {
	if (!can_see_obj(ch, obj) || ch->level < obj->level)
	    continue;
	
	if (vnum == 0)
	{
	    if (!is_name(argument, obj->name))
		continue;
	}
	else
	{
	    if (obj->pIndexData->vnum != vnum)
		continue;
	}

	found = TRUE;
	number++;

	for (in_obj = obj; in_obj->in_obj != NULL; in_obj = in_obj->in_obj)
	    ;

	if (is_limit(obj) != -1) strcpy(buf1, " (limit) ");
	else buf1[0] = '\0';

	if (in_obj->carried_by != NULL && can_see(ch, in_obj->carried_by)
	    && in_obj->carried_by->in_room != NULL)
	{
	    sprintf(buf, "%3d) %s%s is carried by %s [Room %d]\n\r",
		    number, obj->short_descr, buf1, PERS(in_obj->carried_by, ch, 0),
		    in_obj->carried_by->in_room->vnum);
	}
	else if (in_obj->in_room != NULL && can_see_room(ch, in_obj->in_room))
	{
	    sprintf(buf, "%3d) %s%s is in %s [Room %d]\n\r",
		    number, obj->short_descr, buf1, in_obj->in_room->name,
		    in_obj->in_room->vnum);
	}
	else
	    sprintf(buf, "%3d) %s%s is somewhere\n\r", number, obj->short_descr, buf1);

	buf[0] = UPPER(buf[0]);
	add_buf(buffer, buf);

	if (number >= max_found)
	    break;
    }

    if (!found)
	send_to_char("Íè÷åãî ïîäîáíîãî íåò íè íà íåáåñàõ, íè íà çåìëå.\n\r", ch);
    else
	page_to_char(buf_string(buffer), ch);

    free_buf(buffer);
}


void do_mwhere(CHAR_DATA *ch, char *argument)
{
    char buf[MAX_STRING_LENGTH];
    BUFFER *buffer;
    CHAR_DATA *victim;
    bool found;
    int count = 0, max_found = 100;

    if (argument[0] == '\0')
    {
	DESCRIPTOR_DATA *d;

	/* show characters logged */

	buffer = new_buf();
	SLIST_FOREACH(d, &descriptor_list, link)
	{
	    if (d->character != NULL && d->connected == CON_PLAYING
		&&  d->character->in_room != NULL && can_see(ch, d->character)
		&&  can_see_room(ch, d->character->in_room))
	    {
		victim = d->character;
		count++;
		if (d->original != NULL)
		    sprintf(buf, "%3d) %s (in the body of %s) is in %s [%d]\n\r",
			    count, d->original->name, victim->short_descr,
			    victim->in_room->name, victim->in_room->vnum);
		else
		    sprintf(buf, "%3d) %s is in %s [%d]\n\r",
			    count, victim->name, victim->in_room->name,
			    victim->in_room->vnum);
		add_buf(buffer, buf);
		if (count >= max_found)
		    break;
	    }
	}

	page_to_char(buf_string(buffer), ch);
	free_buf(buffer);
	return;
    }

    found = FALSE;
    buffer = new_buf();
    LIST_FOREACH(victim, &char_list, link)
    {
	if (victim->in_room != NULL
	    && is_name(argument, victim->name)
	    && can_see(ch, victim)
	    && can_see_room(ch, victim->in_room))
	{
	    found = TRUE;
	    count++;
	    sprintf(buf, "%3d) [%5d] %-28s [%5d] %s\n\r", count,
		    IS_NPC(victim) ? victim->pIndexData->vnum : 0,
		    IS_NPC(victim) ? victim->short_descr : victim->name,
		    victim->in_room->vnum,
		    victim->in_room->name);
	    add_buf(buffer, buf);

	    if (count >= max_found)
		break;
	}
    }

    if (!found)
	act("Òû íå íàõîäèøü íè îäíîãî '$T'.", ch, NULL, argument, TO_CHAR);
    else
	page_to_char(buf_string(buffer), ch);

    free_buf(buffer);

    return;
}



void do_reboo(CHAR_DATA *ch, char *argument)
{
    send_to_char("{RÅñëè òû õî÷åøü ïåðåçàãðóçèòüñÿ, íàïèøè êîìàíäó ïîëíîñòüþ.{x\n\r", ch);
    return;
}

void p_open(CHAR_DATA *ch, char *argument);
void save_corpses(void);

void for_reboot(CHAR_DATA *ch, char *buf)
{
    extern bool merc_down;
    DESCRIPTOR_DATA *d, *d_next;
    CHAR_DATA *vch;

    do_function(ch, &do_echo, buf);
    merc_down = TRUE;

    save_corpses();
    save_popularity();
    save_chest(0);

    SLIST_FOREACH_SAFE(d, &descriptor_list, link, d_next)
    {
	vch = CH(d);

	if (vch != NULL) {
	    check_auctions(vch, NULL, "ïåðåçàãðóçêè ñåðâåðà");
	    save_char_obj(vch, FALSE);	    
	}

	close_socket(d);
    }

    return;
}

void do_reboot(CHAR_DATA *ch, char *argument)
{
    char buf[MAX_STRING_LENGTH];

    if (argument[0] != '\0')
    {
	argument = one_argument(argument, buf);

	if (!is_number(buf))
	{
	    send_to_char("Òû äîëæåí ââåñòè ÷èñëîâîé àðãóìåíò.\n\r", ch);
	    return;
	}

	reboot_time = atoi(buf);

	if (reboot_time > 0 )
	    who_claim_reboot = ch;
	else
	    who_claim_reboot = NULL;

	one_argument(argument, buf);

	if (!str_cmp(buf, "compile"))
	{
	    p_open(ch, "cd /usr/local/mud/test/src/ && cvs update 2>&1");
	    p_open(ch, "cd /usr/local/mud/test/src/ && cvs commit -m \"server commit\" 2>&1");
	    p_open(ch, "cd /usr/local/mud/area && cvs commit -m \"Areas server commit\" 2>&1");
	    /* Recommended values of '-j' for multi-CPU machines is between 6
	     * and 10 */
	    p_open(ch, "cd /usr/local/mud/test/src/ && make -j6 2>&1"); 
	    /*	    strcpy(buf, "copy"); XXX */
	}

	if (!str_cmp(buf, "copy"))
	{
	    FILE *fp;

	    if ((fp = file_open("copy.txt", "w")) != NULL)
	    {
		fprintf(fp, "\n");
		file_close(fp);
		send_to_char("Ôàéë copy.txt ñîçäàí.\n\r", ch);
	    }
	    else
	    {
		send_to_char("Íåâîçìîæíî ñîçäàòü ôàéë copy.txt.\n\r", ch);
		return;
	    }
	}
	send_to_char("Ok.\n\r", ch);
	return;
    }

    buf[0]='\0';
    if (ch->invis_level < LEVEL_HERO)
    {
	sprintf(buf, "Reboot by %s.\n\r", ch->name);
    }

    strcat(buf,
	   "Âíèìàíèå, ÏÅÐÅÇÀÃÐÓÇÊÀ!\n\rÍå âîëíóéòåñü, âñå áóäåò â ïîðÿäêå.\n\r");


    for_reboot(ch, buf);

    return;
}

void do_shutdow(CHAR_DATA *ch, char *argument)
{
    send_to_char("Åñëè õî÷åøü âûêëþ÷èòü ñåðâåð, íàïèøè êîìàíäó ïîëíîñòüþ!\n\r",
		 ch);
    return;
}

void do_shutdown(CHAR_DATA *ch, char *argument)
{
    char buf[MAX_STRING_LENGTH];

    buf[0]='\0';
    if (ch->invis_level < LEVEL_HERO)
	sprintf(buf, "Shutdown by %s.\n\r", ch->name);

    append_file(ch, SHUTDOWN_FILE, buf);
    strcat(buf, "Âíèìàíèå, ñåðâåð áóäåò ÂÛÊËÞ×ÅÍ!!!\n\r");

    for_reboot(ch, buf);
}

void do_protect(CHAR_DATA *ch, char *argument)
{
    CHAR_DATA *victim;

    if (argument[0] == '\0')
    {
	send_to_char("Çàùèòèòü êîãî îò ïîäãëÿäûâàíèÿ?\n\r", ch);
	return;
    }

    if ((victim = get_char_world(ch, argument)) == NULL)
    {
	send_to_char("Òàêèõ çäåñü íåò.\n\r", ch);
	return;
    }

    if (IS_SET(victim->comm, COMM_SNOOP_PROOF))
    {
	act_new("$N áîëüøå íå çàùèùåí îò ëþáîïûòíûõ.", ch, NULL, victim, TO_CHAR, POS_DEAD);
	send_to_char("Òâîÿ çàùèòà îò ëþáîïûòíûõ áûëà ñíÿòà.\n\r", victim);
	REMOVE_BIT(victim->comm, COMM_SNOOP_PROOF);
    }
    else
    {
	act_new("$N òåïåðü çàùèùåí îò ëþáîïûòíûõ.", ch, NULL, victim, TO_CHAR, POS_DEAD);
	send_to_char("Òû òåïåðü çàùèùåí îò ëþáîïûòíûõ.\n\r", victim);
	SET_BIT(victim->comm, COMM_SNOOP_PROOF);
    }
}



void do_snoop(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH];
    DESCRIPTOR_DATA *d;
    CHAR_DATA *victim;
    char buf[MAX_STRING_LENGTH];

    one_argument(argument, arg);

    if (arg[0] == '\0')
    {
	send_to_char("Ïîäñìîòðåòü çà êåì?\n\r", ch);
	return;
    }

    if ((victim = get_char_world(ch, arg)) == NULL)
    {
	send_to_char("Òàêèõ íåò.\n\r", ch);
	return;
    }

    if (victim->desc == NULL)
    {
	send_to_char("No descriptor to snoop.\n\r", ch);
	return;
    }

    if (victim == ch)
    {
	send_to_char("Òû áîëüøå íå ïîäãëÿäûâàåøü.\n\r", ch);
	wiznet("$N ïåðåñòàåò ïîäãëÿäûâàòü.",
	       ch, NULL, WIZ_SNOOPS, WIZ_SECURE, get_trust(ch));
	SLIST_FOREACH(d, &descriptor_list, link)
	{
	    if (d->snoop_by == ch->desc)
		d->snoop_by = NULL;
	}
	return;
    }

    if (victim->desc->snoop_by == ch->desc)
    {
	act("Òû áîëüøå íå ïîäãëÿäûâàåøü çà $N4.", ch, NULL, victim, TO_CHAR);
	sprintf(buf, "$N ïåðåñòàåò ïîäãëÿäûâàòü çà %s.",
		(IS_NPC(victim)
		 ? cases(victim->short_descr, 4)
		 : cases(victim->name, 4)));
	wiznet(buf, ch, NULL, WIZ_SNOOPS, WIZ_SECURE, get_trust(ch));
	victim->desc->snoop_by = NULL;

	return;
    }

    if (victim->desc->snoop_by != NULL)
    {
	send_to_char("Çà íèì óæå ïîäãëÿäûâàþò.\n\r", ch);
	return;
    }

    if ( ch->in_room != victim->in_room
	 && room_is_private(victim->in_room, NULL)
	 && !IS_TRUSTED(ch, IMPLEMENTOR))
    {
	send_to_char("Ýòîò ïåðñîíàæ â ïðèâàòíîé êîìíàòå!\n\r", ch);
	return;
    }

    if (get_trust(victim) > get_trust(ch)
	|| IS_SET(victim->comm, COMM_SNOOP_PROOF))
    {
	send_to_char("Íå ïîëó÷àåòñÿ.\n\r", ch);
	return;
    }

    if (ch->desc != NULL)
    {
	for (d = ch->desc->snoop_by; d != NULL; d = d->snoop_by)
	{
	    if (d->character == victim || d->original == victim)
	    {
		send_to_char("No snoop loops.\n\r", ch);
		return;
	    }
	}
    }

    victim->desc->snoop_by = ch->desc;
    sprintf(buf, "$N íà÷èíàåò ïîäãëÿäûâàòü çà %s",
	    (IS_NPC(victim)
	     ? cases(victim->short_descr, 4)
	     : cases(victim->name, 4)));
    wiznet(buf, ch, NULL, WIZ_SNOOPS, WIZ_SECURE, get_trust(ch));
    send_to_char("Ok.\n\r", ch);
    return;
}



void do_switch(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH], buf[MAX_STRING_LENGTH];
    CHAR_DATA *victim;

    one_argument(argument, arg);

    if (arg[0] == '\0')
    {
	send_to_char("Ïåðåêëþ÷èòüñÿ â êîãî?\n\r", ch);
	return;
    }

    if (ch->desc == NULL)
	return;

    if (ch->desc->original != NULL)
    {
	send_to_char("Òû óæå ïåðåêëþ÷èëñÿ â êîãî-òî.\n\r", ch);
	return;
    }

    if ((victim = get_char_world(ch, arg)) == NULL)
    {
	send_to_char("Òàêèõ çäåñü íåò.\n\r", ch);
	return;
    }

    if (victim == ch)
    {
	send_to_char("Ok.\n\r", ch);
	return;
    }

    if (!IS_NPC(victim))
    {
	send_to_char("Òû ìîæåøü ïåðåêëþ÷àòüñÿ òîëüêî â ìîáîâ.\n\r", ch);
	return;
    }

    if (ch->in_room != victim->in_room
	&&  room_is_private(victim->in_room, NULL) && !IS_TRUSTED(ch, IMPLEMENTOR))
    {
	send_to_char("Ýòîò ïåðñîíàæ â ïðèâàòíîé êîìíàòå.\n\r", ch);
	return;
    }

    if (victim->desc != NULL)
    {
	send_to_char("Ýòîò ïåðñîíàæ óæå çàíÿò.\n\r", ch);
	return;
    }

    sprintf(buf, "$N ïåðåêëþ÷àåòñÿ â %s", cases(victim->short_descr, 1));
    wiznet(buf, ch, NULL, WIZ_SWITCHES, WIZ_SECURE, get_trust(ch));

    ch->desc->character = victim;
    ch->desc->original  = ch;
    victim->desc        = ch->desc;
    ch->desc            = NULL;
    /* change communications to match */
    if (ch->prompt != NULL)
    {
	free_string(victim->prompt);
	victim->prompt = str_dup(ch->prompt);
    }
    victim->comm = ch->comm;
    victim->lines = ch->lines;
    send_to_char("Ok.\n\r", victim);
    return;
}



void do_return(CHAR_DATA *ch, char *argument)
{
    char buf[MAX_STRING_LENGTH];

    if (ch->desc == NULL)
	return;

    if (ch->desc->original == NULL)
    {
	send_to_char("Òû íè â êîãî íå ïåðåêëþ÷àëñÿ.\n\r", ch);
	return;
    }

    send_to_char("Òû âîçâðàùàåøüñÿ â ñâîå òåëî. Íàáåðè 'âîñïðîèçâåñòè' äëÿ ïðîñìîòðà ïðîïóùåííûõ ðàçãîâîðîâ.\n\r", ch);
    if (ch->prompt != NULL)
    {
	free_string(ch->prompt);
	ch->prompt = NULL;
    }

    sprintf(buf, "$N âîçâðàùàåòñÿ â ñâîå òåëî èç %s.", cases(ch->short_descr, 1));
    wiznet(buf, ch->desc->original, 0, WIZ_SWITCHES, WIZ_SECURE, get_trust(ch));

    char_from_room(ch->desc->original);
    char_to_room(ch->desc->original, ch->in_room, FALSE);

    ch->desc->character       = ch->desc->original;
    ch->desc->original        = NULL;
    ch->desc->character->desc = ch->desc;

    ch->desc                  = NULL;

    return;
}

/* trust levels for load and clone */
bool obj_check (CHAR_DATA *ch, OBJ_DATA *obj)
{
    if (IS_TRUSTED(ch, GOD)
	|| (IS_TRUSTED(ch, IMMORTAL) && obj->level <= 20 && obj->cost <= 1000)
	|| (IS_TRUSTED(ch, DEMI)	    && obj->level <= 10 && obj->cost <= 500)
	|| (IS_TRUSTED(ch, ANGEL)    && obj->level <=  5 && obj->cost <= 250)
	|| (IS_TRUSTED(ch, AVATAR)   && obj->level ==  0 && obj->cost <= 100))
	return TRUE;
    else
	return FALSE;
}

/* for clone, to insure that cloning goes many levels deep */
void recursive_clone(CHAR_DATA *ch, OBJ_DATA *obj, OBJ_DATA *clone)
{
    OBJ_DATA *c_obj, *t_obj;


    for (c_obj = obj->contains; c_obj != NULL; c_obj = c_obj->next_content)
    {
	if (obj_check(ch, c_obj))
	{
	    if (check_reset_for_limit(c_obj, TRUE))
	    {
		act("Ëèìèò $p1 ïðåâûøåí.", ch, c_obj, NULL, TO_CHAR);
		continue;
	    }

	    t_obj = create_object(c_obj->pIndexData, 0);
	    clone_object(c_obj, t_obj);
	    obj_to_obj(t_obj, clone);
	    recursive_clone(ch, c_obj, t_obj);
	}
    }
}

/* command that is similar to load */
void do_clone(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH];
    char *rest;
    CHAR_DATA *mob;
    OBJ_DATA  *obj;

    rest = one_argument(argument, arg);

    if (arg[0] == '\0')
    {
	send_to_char("Êëîíèðîâàòü ÷òî?\n\r", ch);
	return;
    }

    if (!str_prefix(arg, "object") || !str_prefix(arg, "îáúåêò"))
    {
	mob = NULL;
	obj = get_obj_here(ch, NULL, rest);
	if (obj == NULL)
	{
	    send_to_char("Òû íå âèäèøü ýòîãî çäåñü.\n\r", ch);
	    return;
	}
    }
    else if (!str_prefix(arg, "mobile") || !str_prefix(arg, "character") || !str_prefix(arg, "ìîá") || !str_prefix(arg, "÷àð"))
    {
	obj = NULL;
	mob = get_char_room(ch, NULL, rest, FALSE);
	if (mob == NULL)
	{
	    send_to_char("Òû íå âèäèøü ýòîãî çäåñü.\n\r", ch);
	    return;
	}
    }
    else /* find both */
    {
	mob = get_char_room(ch, NULL, argument, FALSE);
	obj = get_obj_here(ch, NULL, argument);
	if (mob == NULL && obj == NULL)
	{
	    send_to_char("Òû íå âèäèøü ýòîãî çäåñü.\n\r", ch);
	    return;
	}
    }

    /* clone an object */
    if (obj != NULL)
    {
	OBJ_DATA *clone;

	if (!obj_check(ch, obj))
	{
	    send_to_char("Òâîåé ýíåðãèè íå õâàòàåò äëÿ ýòîãî.\n\r", ch);
	    return;
	}

	if (check_reset_for_limit(obj, TRUE))
	{
	    act("Ëèìèò $p1 ïðåâûøåí.", ch, obj, NULL, TO_CHAR);
	    return;
	}

	clone = create_object(obj->pIndexData, 0);
	clone_object(obj, clone);
	if (obj->carried_by != NULL)
	    obj_to_char(clone, ch);
	else
	    obj_to_room(clone, ch->in_room);
	recursive_clone(ch, obj, clone);

	act("$n ñîçäàåò $p6.", ch, clone, NULL, TO_ROOM);
	act("Òû êëîíèðóåøü $p6.", ch, clone, NULL, TO_CHAR);
	wiznet("$N êëîíèðóåò $p6.", ch, clone, WIZ_LOAD, WIZ_SECURE, get_trust(ch));
	return;
    }
    else if (mob != NULL)
    {
	CHAR_DATA *clone;
	OBJ_DATA *new_obj;
	char buf[MAX_STRING_LENGTH];

	if (!IS_NPC(mob))
	{
	    send_to_char("Òû ìîæåøü êëîíèðîâàòü òîëüêî ìîáîâ.\n\r", ch);
	    return;
	}

	if ((mob->level > 20 && !IS_TRUSTED(ch, GOD))
	    ||  (mob->level > 10 && !IS_TRUSTED(ch, IMMORTAL))
	    ||  (mob->level >  5 && !IS_TRUSTED(ch, DEMI))
	    ||  (mob->level >  0 && !IS_TRUSTED(ch, ANGEL))
	    ||  !IS_TRUSTED(ch, AVATAR))
	{
	    send_to_char("Òâîåé ýíåðãèè íå õâàòàåò äëÿ ýòîãî..\n\r", ch);
	    return;
	}

	clone = create_mobile(mob->pIndexData);
	clone_mobile(mob, clone);

	for (obj = mob->carrying; obj != NULL; obj = obj->next_content)
	{
	    if (obj_check(ch, obj))
	    {
		if (check_reset_for_limit(obj, TRUE))
		{
		    act("Ëèìèò $p1 ïðåâûøåí.", ch, obj, NULL, TO_CHAR);
		    continue;
		}

		new_obj = create_object(obj->pIndexData, 0);
		clone_object(obj, new_obj);
		recursive_clone(ch, obj, new_obj);
		obj_to_char(new_obj, clone);
		new_obj->wear_loc = obj->wear_loc;
	    }
	}
	char_to_room(clone, ch->in_room, TRUE);
	act("$n ñîçäàåò $N3.", ch, NULL, clone, TO_ROOM);
	act("Òû êëîíèðóåøü $N3.", ch, NULL, clone, TO_CHAR);
	sprintf(buf, "$N êëîíèðóåò %s.", cases(clone->short_descr, 3));
	wiznet(buf, ch, NULL, WIZ_LOAD, WIZ_SECURE, get_trust(ch));
	return;
    }
}

/* RT to replace the two load commands */

void do_load(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH];

    argument = one_argument(argument, arg);

    if (arg[0] == '\0')
    {
	send_to_char("Syntax:\n\r", ch);
	send_to_char("  load mob <vnum> <count>\n\r", ch);
	send_to_char("  load obj <vnum> <count> <level>\n\r", ch);
	send_to_char("  çàãðóçèòü ìîá <vnum> <count>\n\r", ch);
	send_to_char("  çàãðóçèòü îáúåêò <vnum> <count> <level>\n\r", ch);
	return;
    }

    if (!str_cmp(arg, "mob")
	|| !str_cmp(arg, "char")
	|| !str_cmp(arg, "ìîá")
	|| !str_cmp(arg, "÷àð"))
    {
	do_function(ch, &do_mload, argument);
	return;
    }

    if (!str_cmp(arg, "obj") || !str_prefix(arg, "îáúåêò"))
    {
	do_function(ch, &do_oload, argument);
	return;
    }
    /* echo syntax */
    do_function(ch, &do_load, "");
}


void do_mload(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH];
    MOB_INDEX_DATA *pMobIndex;
    CHAR_DATA *victim = NULL;
    char buf[MAX_STRING_LENGTH];
    int i, num = 1;

    argument = one_argument(argument, arg);

    if (arg[0] == '\0' || !is_number(arg))
    {
	send_to_char("Syntax: load mob <vnum> <count>.\n\r", ch);
	send_to_char("        çàãðóçèòü ìîá <vnum> <count>.\n\r", ch);
	return;
    }

    if (is_number(argument))
	num = atoi(argument);

    if ((pMobIndex = get_mob_index(atoi(arg))) == NULL)
    {
	send_to_char("Íåò ìîáà ñ òàêèì íîìåðîì (vnum).\n\r", ch);
	return;
    }

    for (i = 0; i < num; i++)
    {
	victim = create_mobile(pMobIndex);
	char_to_room(victim, ch->in_room, TRUE);
    }
    act("$n ñîçäàåò $N3!", ch, NULL, victim, TO_ROOM);
    sprintf(buf, "$N çàãðóæàåò %d %s.", num, cases(victim->short_descr, 3));
    wiznet(buf, ch, NULL, WIZ_LOAD, WIZ_SECURE, get_trust(ch));
    send_to_char("Ok.\n\r", ch);
    return;
}



void do_oload(CHAR_DATA *ch, char *argument)
{
    char arg1[MAX_INPUT_LENGTH] , arg2[MAX_INPUT_LENGTH];
    char buf[MAX_STRING_LENGTH];
    OBJ_INDEX_DATA *pObjIndex;
    int level, i, num = 1;
    OBJ_DATA *obj = NULL;

    argument = one_argument(argument, arg1);
    argument = one_argument(argument, arg2);

    if (arg1[0] == '\0' || !is_number(arg1))
    {
	send_to_char("Syntax: load obj <vnum> <count> <level>.\n\r", ch);
	send_to_char("        çàãðóçèòü îáúåêò <vnum> <count> <level>.\n\r", ch);
	return;
    }

    level = get_trust(ch); /* default */

    if (argument[0] != '\0')  /* load with a level */
    {
	if (!is_number(argument))
	{
	    send_to_char("Syntax: oload <vnum> <count> <level>.\n\r", ch);
	    return;
	}
	level = atoi(argument);
	if (level < 0 || level > get_trust(ch))
	{
	    send_to_char("Óðîâåíü äîæåí áûòü îò 0 äî òâîåãî.\n\r", ch);
	    return;
	}
    }

    if ((pObjIndex = get_obj_index(atoi(arg1))) == NULL)
    {
	send_to_char("Îáúåêòîâ ñ òàêèì vnum íå íàéäåíî.\n\r", ch);
	return;
    }

    if (is_number(arg2))
	num = atoi(arg2);

    for (i = 0; i < num ; i++)
    {
	obj = create_object(pObjIndex, level);
	if (CAN_WEAR(obj, ITEM_TAKE))
	    obj_to_char(obj, ch);
	else
	{
	    obj_to_room(obj, ch->in_room);
	    if (obj->item_type == ITEM_CHEST && obj->in_room != NULL)
		save_chest(obj->id);	// Ñîõðàíÿåì ñóíäóê â ôàéë
	}

	if ((level = is_limit(obj)) != -1)
	    limits[level+1]++;
    }

    act("$n ñîçäàåò $p6!", ch, obj, NULL, TO_ROOM);
    sprintf(buf, "$N çàãðóæàåò %d %s.", num, cases(obj->short_descr, 3));
    wiznet(buf, ch, NULL, WIZ_LOAD, WIZ_SECURE, get_trust(ch));
    send_to_char("Ok.\n\r", ch);
    return;
}



void do_purge(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH];
    char buf[100];
    CHAR_DATA *victim;
    OBJ_DATA *obj;
    DESCRIPTOR_DATA *d;

    one_argument(argument, arg);

    if (arg[0] == '\0')
    {
	/* 'purge' */
	CHAR_DATA *vnext;
	OBJ_DATA  *obj_next;

	LIST_FOREACH_SAFE(victim, &ch->in_room->people, room_link, vnext)
	{
	    if (IS_NPC(victim)
		&& !IS_SET(victim->act, ACT_NOPURGE)
		&& victim != ch /* safety precaution */)
	    {
		extract_char(victim, TRUE);
	    }
	}

	for (obj = ch->in_room->contents; obj != NULL; obj = obj_next)
	{
	    obj_next = obj->next_content;
	    if (!IS_OBJ_STAT(obj, ITEM_NOPURGE))
	    {
		if (obj->item_type == ITEM_CHEST && obj->in_room != NULL)
		    continue;			/* Íå ïóðæèì ñóíäóê */

		extract_obj(obj, TRUE, FALSE);
	    }
	}

	act("$n î÷èùàåò êîìíàòó îò õëàìà! :-)", ch, NULL, NULL, TO_ROOM);
	send_to_char("Ok.\n\r", ch);
	return;
    }

    victim = get_char_world(ch, arg);
    obj = get_obj_list(ch, arg, ch->in_room->contents);

    if (!obj && !victim)
    {
	send_to_char("Ýòîãî çäåñü íåò.\n\r", ch);
	return;
    }

    if (victim)
    {
	if (!IS_NPC(victim))
	{

	    if (ch == victim)
	    {
		send_to_char("Òû â ýòîì óâåðåí?\n\r", ch);
		return;
	    }

	    if (get_trust(ch) < get_trust(victim))
	    {
		send_to_char("Ýòî íå î÷åíü õîðîøàÿ èäåÿ...\n\r", ch);
		sprintf(buf, "%s ïûòàåòñÿ ïðåâðàòèòü òåáÿ â ïûëü!\n\r", ch->name);
		send_to_char(buf, victim);
		return;
	    }

	    act("{R$n îáðàùàåò â ïûëü $N3!{x", ch, 0, victim, TO_NOTVICT);

	    if (victim->level > 1)
		save_char_obj(victim, FALSE);
	    d = victim->desc;
	    extract_char(victim, TRUE);
	    if (d != NULL)
		close_socket(d);

	    return;
	}

	act("{R$n ïðåâðàùàåò â ïûëü $N3!{x", ch, NULL, victim, TO_NOTVICT);
	extract_char(victim, TRUE);
	return;
    }

    if (obj && !IS_OBJ_STAT(obj, ITEM_NOPURGE))
	extract_obj(obj, TRUE, FALSE);

    send_to_char("Ok.\n\r", ch);
}



void do_advance(CHAR_DATA *ch, char *argument)
{

	
    char buf[MAX_STRING_LENGTH];
    char arg1[MAX_INPUT_LENGTH];
    char arg2[MAX_INPUT_LENGTH];
    CHAR_DATA *victim;
    int level;
    int iLevel;

    argument = one_argument(argument, arg1);
    argument = one_argument(argument, arg2);

    if (arg1[0] == '\0' || arg2[0] == '\0' || !is_number(arg2))
    {
	send_to_char("Syntax: advance <char> <level>.\n\r", ch);
	send_to_char("        ëåâåë   <char> <level>.\n\r", ch);
	return;
    }

    if ((victim = get_char_world(ch, arg1)) == NULL)
    {
	send_to_char("Ýòîãî ïåðñîíàæà íåò â èãðå.\n\r", ch);
	return;
    }

    if (IS_NPC(victim))
    {
	send_to_char("Òîëüêî íå ñ ìîáîì.\n\r", ch);
	return;
    }

    if ((level = atoi(arg2)) < 1 || level > MAX_LEVEL)
    {
	sprintf(buf, "Óðîâåíü äîëæåí áûòü îò 1 äî %d.\n\r", MAX_LEVEL);
	send_to_char(buf, ch);
	return;
    }

    if (level > get_trust(ch))
    {
	send_to_char("Îãðàíè÷åíî òâîèì trust-óðîâíåì.\n\r", ch);
	return;
    }

    if (get_trust(ch) < get_trust(victim))
    {
	send_to_char("Ëó÷øå íå ñòîèò...\n\r", ch);
	return;
    }

    /*
     * Lower level:
     *   Reset to level 1.
     *   Then raise again.
     *   Currently, an imp can lower another imp.
     *   -- Swiftest
     */
    if (level <= victim->level)
    {
	/*        int temp_prac; */

	send_to_char("Ïîíèæåíèå óðîâíÿ!\n\r", ch);
	send_to_char("{R**** Î-Î-Î-Î-Î  Í-Í-Í-Å-Å-Å-Ò-Ò-Ò ****{x\n\r", victim);
	/*	temp_prac = victim->practice;
	 victim->level    = 1;
	 victim->exp      = exp_per_level(victim, victim->pcdata->points);
	 victim->max_hit  = 10;
	 victim->max_mana = 100;
	 victim->max_move = 100;
	 victim->practice = 0;
	 victim->hit      = victim->max_hit;
	 victim->mana     = victim->max_mana;
	 victim->move     = victim->max_move;
	 advance_level(victim, TRUE);
	 victim->practice = temp_prac; */
	victim->level = level;
    }
    else
    {
	send_to_char("Ïîâûøåíèå óðîâíÿ!\n\r", ch);
	send_to_char("{R**** Î-Î-Î-Î-Î  Y-Y-Y-E-E-E-A-A-A !!! ****{x\n\r", victim);
	for (iLevel = victim->level ; iLevel < level; iLevel++)
	{
	    victim->level++;
	    advance_level(victim, TRUE);
	}
    }

    sprintf(buf, "Òû òåïåðü %d óðîâåíü.\n\r", victim->level);
    send_to_char(buf, victim);
    victim->exp   = exp_per_level(victim, victim->pcdata->points) * UMAX(1, victim->level);
    victim->trust = 0;
    save_char_obj(victim, FALSE);
    return;
}



void do_trust(CHAR_DATA *ch, char *argument)
{


    char arg1[MAX_INPUT_LENGTH];
    char arg2[MAX_INPUT_LENGTH];
    char buf[MAX_STRING_LENGTH];
    CHAR_DATA *victim;
    int level;

    argument = one_argument(argument, arg1);
    argument = one_argument(argument, arg2);

    if (arg1[0] == '\0' || arg2[0] == '\0' || !is_number(arg2))
    {
	send_to_char("Syntax: trust    <char> <level>.\n\r", ch);
	send_to_char("        äîâåðèòü <char> <level>.\n\r", ch);
	return;
    }

    if ((victim = get_char_world(ch, arg1)) == NULL)
    {
	send_to_char("Òàêèõ èãðîêîâ íåò.\n\r", ch);
	return;
    }

    if ((level = atoi(arg2)) < 0 || level > MAX_LEVEL)
    {
	sprintf(buf, "Óðîâåíü äîëæåí áûòü îò 0 (î÷èñòèòü) èëè 1 äî %d.\n\r", MAX_LEVEL);
	send_to_char(buf, ch);
	return;
    }

    if (level > get_trust(ch))
    {
	send_to_char("Îãðàíè÷åíî òâîèì trust-ëåâåëîì.\n\r", ch);
	return;
    }

    victim->trust = level;
    return;
}

void restore(CHAR_DATA *ch, CHAR_DATA *victim)
{
    affect_strip(victim, gsn_plague);
    affect_strip(victim, gsn_poison);
    affect_strip(victim, gsn_blindness);
    affect_strip(victim, gsn_sleep);
    affect_strip(victim, gsn_curse);
    affect_strip(victim, gsn_dirt);
    affect_strip(victim, gsn_thornwrack);
    affect_strip(victim, skill_lookup("fire breath"));
    victim->hit  = victim->max_hit;
    victim->mana = victim->max_mana;
    victim->move = victim->max_move;
    update_pos(victim);

    if (victim->in_room != NULL)
	act("$n ïîëíîñòüþ âîññòàíàâëèâàåò òåáÿ.", ch, NULL, victim, TO_VICT);
}

void do_restore(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH], buf[MAX_STRING_LENGTH];
    CHAR_DATA *victim;
    CHAR_DATA *vch;
    DESCRIPTOR_DATA *d;

    one_argument(argument, arg);

    if (arg[0] == '\0' || !str_cmp(arg, "room") || !str_prefix(arg, "êîìíàòà"))
    {
	/* cure room */

	LIST_FOREACH(vch, &ch->in_room->people, room_link)
	    restore(ch, vch);

	sprintf(buf, "$N âîññòàíàâëèâàåò êîìíàòó %d.", ch->in_room->vnum);
	wiznet(buf, ch, NULL, WIZ_RESTORE, WIZ_SECURE, get_trust(ch));

	send_to_char("Êîìíàòà âîññòàíîâëåíà.\n\r", ch);
	return;

    }

    if ((!str_cmp(arg, "all") || !str_cmp(arg, "âñå")))
    {
	/* cure all */

	SLIST_FOREACH(d, &descriptor_list, link)
	{
	    victim = d->character;

	    if (victim == NULL || IS_NPC(victim))
		continue;

	    restore(ch, victim);
	}
	send_to_char("Âñå èãðîêè â ìèðå ïîëíîñòüþ âîññòàíîâëåíû.\n\r", ch);
	return;
    }

    if ((victim = get_char_world(ch, arg)) == NULL)
    {
	send_to_char("Òàêèõ çäåñü íåò.\n\r", ch);
	return;
    }

    restore(ch, victim);

    sprintf(buf, "$N âîññòàíàâëèâàåò %s.",
	    IS_NPC(victim) ? cases(victim->short_descr, 3) : cases(victim->name, 3));
    wiznet(buf, ch, NULL, WIZ_RESTORE, WIZ_SECURE, get_trust(ch));
    send_to_char("Ok.\n\r", ch);
    return;
}


void do_freeze(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH];
    CHAR_DATA *victim;

    if (IS_NPC(ch))
	return;

    argument = one_argument(argument, arg);

    if (arg[0] == '\0')
    {
	send_to_char("Çàìîðîçèòü êîãî?\n\r", ch);
	return;
    }

    if ((victim = get_char_world(ch, arg)) == NULL)
    {
	send_to_char("Òàêèõ çäåñü íåò.\n\r", ch);
	return;
    }

    if (IS_NPC(victim))
    {
	send_to_char("Òîëüêî íå ìîáà.\n\r", ch);
	return;
    }

    if (get_trust(victim) >= get_trust(ch))
    {
	send_to_char("Íå ïîëó÷èëîñü.\n\r", ch);
	return;
    }

    VALIDATE(victim);

    if (IS_SET(victim->act, PLR_FREEZE))
    {
	REMOVE_BIT(victim->act, PLR_FREEZE);
	send_to_char("{RÒû ñìîæåøü îïÿòü èãðàòü.{x\n\r", victim);
	send_to_char("Ôëàã FREEZE ñíÿò.\n\r", ch);
	sprintf(arg, "$N ðàçìîðàæèâàåò %s.", cases(victim->name, 1));
	wiznet(arg, ch, NULL, WIZ_PENALTIES, WIZ_SECURE, 0);
	victim->pcdata->freeze = 0;
    }
    else
    {
	victim->pcdata->freeze = get_next_seconds(argument, ch);

	free_string(victim->pcdata->freeze_who);
	victim->pcdata->freeze_who = str_dup(ch->name);

	SET_BIT(victim->act, PLR_FREEZE);
	send_to_char("{RÒû íå ìîæåøü ïîøåâåëèòüñÿ!{x\n\r", victim);
	send_to_char("Ôëàã FREEZE óñòàíîâëåí.\n\r", ch);
	sprintf(arg, "$N çàìîðàæèâàåò %s.", cases(victim->name, 1));
	wiznet(arg, ch, NULL, WIZ_PENALTIES, WIZ_SECURE, 0);
    }

    save_char_obj(victim, FALSE);

    return;
}



void do_log(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH];
    CHAR_DATA *victim;

    one_argument(argument, arg);

    if (arg[0] == '\0')
    {
	send_to_char("Âåñòè ëîã çà êåì?\n\r", ch);
	return;
    }

    if ((victim = get_char_world(ch, arg)) == NULL || IS_NPC(victim))
    {
	send_to_char("Òàêèõ íåò â ýòîì ìèðå.\n\r", ch);
	return;
    }

    if (IS_SET(victim->act, PLR_LOG_OUTPUT))
    {
	REMOVE_BIT(victim->act, PLR_LOG_OUTPUT);
	send_to_char("LOG output removed.\n\r", ch);
    }
    else
    {
	SET_BIT(victim->act, PLR_LOG_OUTPUT);
	send_to_char("LOG output set.\n\r", ch);
    }
}



void do_noemote(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH], buf[MAX_STRING_LENGTH];
    CHAR_DATA *victim;

    argument = one_argument(argument, arg);

    if (arg[0] == '\0')
    {
	send_to_char("Noemote whom?\n\r", ch);
	return;
    }

    if ((victim = get_char_world(ch, arg)) == NULL || IS_NPC(victim))
    {
	send_to_char("Òàêèõ çäåñü íåò.\n\r", ch);
	return;
    }


    if (get_trust(victim) >= get_trust(ch))
    {
	send_to_char("Íå ïîëó÷èëîñü.\n\r", ch);
	return;
    }

    if (IS_SET(victim->comm, COMM_NOEMOTE))
    {
	REMOVE_BIT(victim->comm, COMM_NOEMOTE);
	send_to_char("{RÒû ñìîæåøü ïîêàçûâàòü ñâîè ýìîöèè ñíîâà.{x\n\r", victim);
	send_to_char("NOEMOTE removed.\n\r", ch);
	sprintf(buf, "$N âîññòàíàâëèâàåò ýìîöèè %s.", cases(victim->name, 2));
	wiznet(buf, ch, NULL, WIZ_PENALTIES, WIZ_SECURE, 0);
	victim->pcdata->nochan = 0;
    }
    else
    {
	SET_BIT(victim->comm, COMM_NOEMOTE);
	victim->pcdata->nochan = get_next_seconds(argument, ch);
	send_to_char("{RÒû òåïåðü íå ñìîæåøü ïîêàçûâàòü ñâîè ýìîöèè!{x\n\r", victim);
	send_to_char("NOEMOTE set.\n\r", ch);
	sprintf(buf, "$N îòáèðàåò ïðàâî ïîëüçîâàíèÿ ýìîöèÿìè ó %s.", cases(victim->name, 1));
	wiznet(buf, ch, NULL, WIZ_PENALTIES, WIZ_SECURE, 0);
    }

    return;
}



void do_noshout(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH], buf[MAX_STRING_LENGTH];
    CHAR_DATA *victim;

    argument = one_argument(argument, arg);

    if (arg[0] == '\0')
    {
	send_to_char("Noshout whom?\n\r", ch);
	return;
    }

    if ((victim = get_char_world(ch, arg)) == NULL)
    {
	send_to_char("Òàêèõ çäåñü íåò.\n\r", ch);
	return;
    }

    if (IS_NPC(victim))
    {
	send_to_char("Òîëüêî íå íà ìîáà.\n\r", ch);
	return;
    }

    if (IS_IMMORTAL(victim) && (get_trust(victim) >= get_trust(ch)))
    {
	send_to_char("Íå ïîëó÷àåòñÿ.\n\r", ch);
	return;
    }

    if (IS_SET(victim->comm, COMM_NOSHOUT))
    {
	REMOVE_BIT(victim->comm, COMM_NOSHOUT);
	send_to_char("{RÒû ñíîâà ìîæåøü îðàòü.{x\n\r", victim);
	send_to_char("NOSHOUT removed.\n\r", ch);
	sprintf(buf, "$N restores shouts to %s.", victim->name);
	wiznet(buf, ch, NULL, WIZ_PENALTIES, WIZ_SECURE, 0);
	victim->pcdata->nochan = 0;
    }
    else
    {
	SET_BIT(victim->comm, COMM_NOSHOUT);
	victim->pcdata->nochan = get_next_seconds(argument, ch);
	send_to_char("{RÒû íå ñìîæåøü îðàòü!{x\n\r", victim);
	send_to_char("NOSHOUT set.\n\r", ch);
	sprintf(buf, "$N revokes %s's shouts.", victim->name);
	wiznet(buf, ch, NULL, WIZ_PENALTIES, WIZ_SECURE, 0);
    }

    return;
}



void do_notell(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH], buf[MAX_STRING_LENGTH];
    CHAR_DATA *victim;

    argument = one_argument(argument, arg);

    if (arg[0] == '\0')
    {
	send_to_char("Êîìó âûêëþ÷èòü ïðèâàòíûé êàíàë?", ch);
	return;
    }

    if ((victim = get_char_world(ch, arg)) == NULL || IS_NPC(victim))
    {
	send_to_char("Òàêèõ çäåñü íåò.\n\r", ch);
	return;
    }

    if (get_trust(victim) >= get_trust(ch))
    {
	send_to_char("Íå ïîëó÷èëîñü.\n\r", ch);
	return;
    }

    if (IS_SET(victim->comm, COMM_NOTELL))
    {
	REMOVE_BIT(victim->comm, COMM_NOTELL);
	send_to_char("{RÒû ñíîâà ìîæåøü ãîâîðèòü.{x\n\r", victim);
	send_to_char("NOTELL removed.\n\r", ch);
	sprintf(buf, "$N âîçâðàùàåò ïðèâàòíûé êàíàë %s.", cases(victim->name, 2));
	wiznet(buf, ch, NULL, WIZ_PENALTIES, WIZ_SECURE, 0);
	victim->pcdata->nochan = 0;
    }
    else
    {
	SET_BIT(victim->comm, COMM_NOTELL);
	victim->pcdata->nochan = get_next_seconds(argument, ch);
	send_to_char("{RÒåïåðü òû íå ñìîæåøü ãîâîðèòü!{x\n\r", victim);
	send_to_char("NOTELL set.\n\r", ch);
	sprintf(buf, "$N çàïðåùàåò %s ïîëüçîâàòüñÿ ïðèâàòíûì êàíàëîì.", cases(victim->name, 2));
	wiznet(buf, ch, NULL, WIZ_PENALTIES, WIZ_SECURE, 0);
    }

    return;
}



void do_peace(CHAR_DATA *ch, char *argument)
{
    CHAR_DATA *rch;

    LIST_FOREACH(rch, &ch->in_room->people, room_link)
    {
	if (rch->fighting != NULL)
	    stop_fighting(rch, TRUE);
	if (IS_NPC(rch) && IS_SET(rch->act, ACT_AGGRESSIVE))
	    REMOVE_BIT(rch->act, ACT_AGGRESSIVE);
    }

    send_to_char("Ok.\n\r", ch);
    return;
}

void do_wizlock(CHAR_DATA *ch, char *argument)
{
    cfg.wizlock = !cfg.wizlock;

    if (cfg.wizlock)
    {
	wiznet("$N has wizlocked the game.", ch, NULL, 0, 0, 0);
	send_to_char("Game wizlocked.\n\r", ch);
    }
    else
    {
	wiznet("$N removes wizlock.", ch, NULL, 0, 0, 0);
	send_to_char("Game un-wizlocked.\n\r", ch);
    }

    return;
}

/* RT anti-newbie code */

void do_newlock(CHAR_DATA *ch, char *argument)
{
    cfg.newlock = !cfg.newlock;

    if (cfg.newlock)
    {
	wiznet("$N locks out new characters.", ch, NULL, 0, 0, 0);
	send_to_char("New characters have been locked out.\n\r", ch);
    }
    else
    {
	wiznet("$N allows new characters back in.", ch, NULL, 0, 0, 0);
	send_to_char("Newlock removed.\n\r", ch);
    }

    return;
}


void do_slookup(CHAR_DATA *ch, char *argument)
{
    char buf[MAX_STRING_LENGTH];
    char arg[MAX_INPUT_LENGTH];
    int sn;

    one_argument(argument, arg);
    if (arg[0] == '\0')
    {
	send_to_char("Lookup which skill or spell?\n\r", ch);
	return;
    }

    if (!str_cmp(arg, "all"))
    {
	for (sn = 0; sn < max_skills; sn++)
	{
	    if (skill_table[sn].name == NULL)
		break;
	    sprintf(buf, "Sn: %3d  Slot: %3d  Skill/spell: '%s'\n\r",
		    sn, skill_table[sn].slot, skill_table[sn].name);
	    send_to_char(buf, ch);
	}
    }
    else
    {
	if ((sn = skill_lookup(arg)) < 0)
	{
	    send_to_char("No such skill or spell.\n\r", ch);
	    return;
	}

	sprintf(buf, "Sn: %3d  Slot: %3d  Skill/spell: '%s'\n\r",
		sn, skill_table[sn].slot, skill_table[sn].name);
	send_to_char(buf, ch);
    }

    return;
}

/* RT set replaces sset, mset, oset, and rset */

void do_set(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH];

    argument = one_argument(argument, arg);

    if (arg[0] == '\0')
    {
	send_to_char("Syntax:\n\r", ch);
	send_to_char("  set mob   <name> <field> <value>\n\r", ch);
	send_to_char("  set obj   <name> <field> <value>\n\r", ch);
	send_to_char("  set room  <room> <field> <value>\n\r", ch);
	send_to_char("  set skill <name> <spell or skill> <value>\n\r", ch);
	return;
    }

    if (!str_prefix(arg, "mobile") || !str_prefix(arg, "character"))
    {
	do_function(ch, &do_mset, argument);
	return;
    }

    if (!str_prefix(arg, "skill") || !str_prefix(arg, "spell"))
    {
	do_function(ch, &do_sset, argument);
	return;
    }

    if (!str_prefix(arg, "object"))
    {
	do_function(ch, &do_oset, argument);
	return;
    }

    if (!str_prefix(arg, "room"))
    {
	do_function(ch, &do_rset, argument);
	return;
    }
    /* echo syntax */
    do_function(ch, &do_set, "");
}


void do_sset(CHAR_DATA *ch, char *argument)
{
    char arg1 [MAX_INPUT_LENGTH];
    char arg2 [MAX_INPUT_LENGTH];
    char arg3 [MAX_INPUT_LENGTH];
    CHAR_DATA *victim;
    int value;
    int sn;
    bool fAll;

    argument = one_argument(argument, arg1);
    argument = one_argument(argument, arg2);
    argument = one_argument(argument, arg3);

    if (arg1[0] == '\0' || arg2[0] == '\0' || arg3[0] == '\0')
    {
	send_to_char("Syntax:\n\r", ch);
	send_to_char("  set skill <name> <spell or skill> <value>\n\r", ch);
	send_to_char("  set skill <name> all <value>\n\r", ch);
	send_to_char("   (use the name of the skill, not the number)\n\r", ch);
	return;
    }

    if ((victim = get_char_world(ch, arg1)) == NULL)
    {
	send_to_char("They aren't here.\n\r", ch);
	return;
    }

    if (IS_NPC(victim))
    {
	send_to_char("Not on NPC's.\n\r", ch);
	return;
    }

    fAll = !str_cmp(arg2, "all");
    sn   = 0;
    if (!fAll && (sn = skill_lookup(arg2)) < 0)
    {
	send_to_char("No such skill or spell.\n\r", ch);
	return;
    }

    /*
     * Snarf the value.
     */
    if (!is_number(arg3))
    {
	send_to_char("Value must be numeric.\n\r", ch);
	return;
    }

    value = atoi(arg3);
    if (value < -100 || value > 100)
    {
	send_to_char("Value range is -100 to 100.\n\r", ch);
	return;
    }

    if (fAll)
    {
	for (sn = 0; sn < max_skills; sn++)
	{
	    if (skill_table[sn].name != NULL)
		victim->pcdata->learned[sn]	= value;
	}
    }
    else
    {
	victim->pcdata->learned[sn] = value;
    }
    send_to_char("Ok.\n\r", ch);
    return;
}


void do_mset(CHAR_DATA *ch, char *argument)
{
    char arg1 [MAX_INPUT_LENGTH];
    char arg2 [MAX_INPUT_LENGTH];
    char arg3 [MAX_INPUT_LENGTH];
    char buf[100];
    CHAR_DATA *victim;
    int value;

    smash_tilde(argument);
    argument = one_argument(argument, arg1);
    argument = one_argument(argument, arg2);
    strcpy(arg3, argument);

    if (arg1[0] == '\0' || arg2[0] == '\0' || arg3[0] == '\0')
    {
	send_to_char("Syntax:\n\r", ch);
	send_to_char("  set char <name> <field> <value>\n\r", ch);
	send_to_char("  Field being one of:\n\r",			ch);
	send_to_char("    str int wis dex con sex class level\n\r",	ch);
	send_to_char("    race group gold silver hp mana move prac\n\r", ch);
	send_to_char("    align train thirst hunger drunk full security\n\r", ch);
	send_to_char("    pwd platinum bronze bank\n\r", ch);
	return;
    }

    if ((victim = get_char_world(ch, arg1)) == NULL)
    {
	send_to_char("They aren't here.\n\r", ch);
	return;
    }

    /* clear zones for mobs */
    victim->zone = NULL;

    /*
     * Snarf the value (which need not be numeric).
     */
    value = is_number(arg3) ? atoi(arg3) : -1;

    /*
     * Set something.
     */
    if (!str_cmp(arg2, "str"))
    {
	if (value < 3 || value > get_max_train(victim, STAT_STR))
	{
	    sprintf(buf, "Strength range is 3 to %d.\n\r",
		    get_max_train(victim, STAT_STR));
	    send_to_char(buf, ch);
	    return;
	}

	victim->perm_stat[STAT_STR] = value;
    }
	else
    if (!str_cmp(arg2, "platinum"))
    {
		if (value < 0 || value > 1000)
		{
	    	sprintf(buf, "platinum range is 0 to 1000.\n\r");
	    	send_to_char(buf, ch);
	    	return;
		}
		victim->pcdata->platinum = value;
    }
	else
    if (!str_cmp(arg2, "bank"))
    {
		if (value < 0 || value > 1000000)
		{
	    	sprintf(buf, "bank range is 0 to 1 000 000.\n\r");
	    	send_to_char(buf, ch);
	    	return;
		}
		victim->pcdata->bank = value;
    }
	else
    if (!str_cmp(arg2, "bronze"))
    {
		if (value < 0 || value > 1000)
		{
	    	sprintf(buf, "bronze range is 0 to 1000.\n\r");
	    	send_to_char(buf, ch);
	    	return;
		}
		victim->pcdata->bronze = value;
    }
	else
    if (!str_cmp(arg2, "pwd"))
    {
		if (IS_IMMORTAL(victim))
		{
			send_to_char("Òîëüêî íå èììîðòàëó.\n\r", ch);
			return;
		}

	    free_string(victim->pcdata->pwd);
    	victim->pcdata->pwd = str_dup(arg3);
    	save_char_obj(victim, FALSE);
		send_to_char("Ok.\n\r", ch);
		return;
	}
    else if (!str_cmp(arg2, "security"))	/* OLC */
    {
	if (IS_NPC(ch))
	{
	    send_to_char("Òû ìîá.\n\r", ch);
	    return;
	}

	if (IS_NPC(victim))
	{
	    send_to_char("Íå ìîáó æå?\n\r", ch);
	    return;
	}

	if (value > ch->pcdata->security || value < 0)
	{
	    if (ch->pcdata->security != 0)
	    {
		sprintf(buf, "Valid security is 0-%d.\n\r",
			ch->pcdata->security);
		send_to_char(buf, ch);
	    }
	    else
	    {
		send_to_char("Valid security is 0 only.\n\r", ch);
	    }
	    return;
	}
	victim->pcdata->security = value;
    }
    else if (!str_cmp(arg2, "int"))
    {
	if (value < 3 || value > get_max_train(victim, STAT_INT))
	{
	    sprintf(buf,
		    "Intelligence range is 3 to %d.\n\r",
		    get_max_train(victim, STAT_INT));
	    send_to_char(buf, ch);
	    return;
	}

	victim->perm_stat[STAT_INT] = value;
    }
    else if (!str_cmp(arg2, "wis"))
    {
	if (value < 3 || value > get_max_train(victim, STAT_WIS))
	{
	    sprintf(buf,
		    "Wisdom range is 3 to %d.\n\r", get_max_train(victim, STAT_WIS));
	    send_to_char(buf, ch);
	    return;
	}

	victim->perm_stat[STAT_WIS] = value;
    }
    else if (!str_cmp(arg2, "dex"))
    {
	if (value < 3 || value > get_max_train(victim, STAT_DEX))
	{
	    sprintf(buf,
		    "Dexterity range is 3 to %d.\n\r",
		    get_max_train(victim, STAT_DEX));
	    send_to_char(buf, ch);
	    return;
	}

	victim->perm_stat[STAT_DEX] = value;
    }
    else if (!str_cmp(arg2, "con"))
    {
	if (value < 3 || value > get_max_train(victim, STAT_CON))
	{
	    sprintf(buf,
		    "Constitution range is 3 to %d.\n\r",
		    get_max_train(victim, STAT_CON));
	    send_to_char(buf, ch);
	    return;
	}

	victim->perm_stat[STAT_CON] = value;
    }
    else if (!str_prefix(arg2, "sex"))
    {
	if (value < 0 || value > GET_MAX_SEX(victim))
	{
	    sprintf(buf, "Sex range is 0 to %d.\n\r", GET_MAX_SEX(victim));
	    send_to_char(buf, ch);
	    return;
	}
	victim->sex = value;
	if (!IS_NPC(victim))
	    victim->pcdata->true_sex = value;
    }
    else if (!str_prefix(arg2, "class"))
    {
	int classid;

	if (IS_NPC(victim))
	{
	    send_to_char("Mobiles have no class.\n\r", ch);
	    return;
	}

	classid = class_lookup(arg3);
	if (classid == -1)
	{
	    char buf[MAX_STRING_LENGTH];

	    strcpy(buf, "Possible classes are: ");
	    for (classid = 0; classid < MAX_CLASS; classid++)
	    {
		if (classid > 0)
		    strcat(buf, " ");
		strcat(buf, class_table[classid].name);
	    }
	    strcat(buf, ".\n\r");

	    send_to_char(buf, ch);
	    return;
	}

	victim->classid = classid;
    }
    else if (!str_prefix(arg2, "level"))
    {
	if (!IS_NPC(victim))
	{
	    send_to_char("Not on PC's.\n\r", ch);
	    return;
	}

	if (value < 0 || value > MAX_LEVEL)
	{
	    sprintf(buf, "Level range is 0 to %d.\n\r", MAX_LEVEL);
	    send_to_char(buf, ch);
	    return;
	}
	victim->level = value;
    }
    else if (!str_prefix(arg2, "gold"))
	victim->gold = value;
    else if (!str_prefix(arg2, "silver"))
	victim->silver = value;
    else if (!str_prefix(arg2, "hp"))
    {
	if (value < -10 || value > 30000)
	{
	    send_to_char("Hp range is -10 to 30, 000 hit points.\n\r", ch);
	    return;
	}
	victim->max_hit = value;
	if (!IS_NPC(victim))
	    victim->pcdata->perm_hit = value;
    }
    else if (!str_prefix(arg2, "mana"))
    {
	if (value < 0 || value > 30000)
	{
	    send_to_char("Mana range is 0 to 30, 000 mana points.\n\r", ch);
	    return;
	}
	victim->max_mana = value;
	if (!IS_NPC(victim))
	    victim->pcdata->perm_mana = value;
    }
    else if (!str_prefix(arg2, "move"))
    {
	if (value < 0 || value > 30000)
	{
	    send_to_char("Move range is 0 to 30, 000 move points.\n\r", ch);
	    return;
	}
	victim->max_move = value;
	if (!IS_NPC(victim))
	    victim->pcdata->perm_move = value;
    }
    else if (!str_prefix(arg2, "practice"))
    {
	if (value < 0 || value > 250)
	{
	    send_to_char("Practice range is 0 to 250 sessions.\n\r", ch);
	    return;
	}
	victim->practice = value;
    }
    else if (!str_prefix(arg2, "train"))
    {
	if (value < 0 || value > 50)
	{
	    send_to_char("Training session range is 0 to 50 sessions.\n\r", ch);
	    return;
	}
	victim->train = value;
    }
    else if (!str_prefix(arg2, "align"))
    {
	if (value < -1000 || value > 1000)
	{
	    send_to_char("Alignment range is -1000 to 1000.\n\r", ch);
	    return;
	}
	victim->alignment = value;
    }
    else if (!str_prefix(arg2, "thirst"))
    {
	if (IS_NPC(victim))
	{
	    send_to_char("Not on NPC's.\n\r", ch);
	    return;
	}

	if (value < -1 || value > 100)
	{
	    send_to_char("Thirst range is -1 to 100.\n\r", ch);
	    return;
	}

	victim->pcdata->condition[COND_THIRST] = value;
    }
    else if (!str_prefix(arg2, "drunk"))
    {
	if (IS_NPC(victim))
	{
	    send_to_char("Not on NPC's.\n\r", ch);
	    return;
	}

	if (value < -1 || value > 100)
	{
	    send_to_char("Drunk range is -1 to 100.\n\r", ch);
	    return;
	}

	victim->pcdata->condition[COND_DRUNK] = value;
    }
    else if (!str_prefix(arg2, "full"))
    {
	if (IS_NPC(victim))
	{
	    send_to_char("Not on NPC's.\n\r", ch);
	    return;
	}

	if (value < -1 || value > 100)
	{
	    send_to_char("Full range is -1 to 100.\n\r", ch);
	    return;
	}

	victim->pcdata->condition[COND_FULL] = value;
    }
    else if (!str_prefix(arg2, "hunger"))
    {
	if (IS_NPC(victim))
	{
	    send_to_char("Not on NPC's.\n\r", ch);
	    return;
	}

	if (value < -1 || value > 100)
	{
	    send_to_char("Full range is -1 to 100.\n\r", ch);
	    return;
	}

	victim->pcdata->condition[COND_HUNGER] = value;
    }
    else if (!str_prefix(arg2, "race"))
    {
	int race;

	race = race_lookup(arg3);

	if (race == -1)
	{
	    send_to_char("That is not a valid race.\n\r", ch);
	    return;
	}

	if (!IS_NPC(victim) && !race_table[race].pc_race)
	{
	    send_to_char("That is not a valid player race.\n\r", ch);
	    return;
	}

	victim->race = race;
    }
    else if (!str_prefix(arg2, "group"))
    {
	if (!IS_NPC(victim))
	{
	    send_to_char("Only on NPCs.\n\r", ch);
	    return;
	}
	victim->group = value;
    }
    else
    {
	/*
	 * Generate usage message.
	 */
	do_function(ch, &do_mset, "");
	return;
    }
    send_to_char("Ok.\n\r", ch);
    return;
}

void do_string(CHAR_DATA *ch, char *argument)
{
    char type [MAX_INPUT_LENGTH];
    char arg1 [MAX_INPUT_LENGTH];
    char arg2 [MAX_INPUT_LENGTH];
    char arg3 [MAX_INPUT_LENGTH];
    CHAR_DATA *victim;
    OBJ_DATA *obj;

    smash_tilde(argument);
    argument = one_argument(argument, type);
    argument = one_argument(argument, arg1);
    argument = one_argument(argument, arg2);
    strcpy(arg3, argument);

    if (type[0] == '\0' || arg1[0] == '\0' || arg2[0] == '\0' || arg3[0] == '\0')
    {
	send_to_char("Syntax:\n\r", ch);
	send_to_char("  string char <name> <field> <string>\n\r", ch);
	send_to_char("    fields: name short long desc title spec\n\r", ch);
	send_to_char("  string obj  <name> <field> <string>\n\r", ch);
	send_to_char("    fields: name short long extended\n\r", ch);
	return;
    }

    if (!str_prefix(type, "character") || !str_prefix(type, "mobile"))
    {
	if ((victim = get_char_world(ch, arg1)) == NULL)
	{
	    send_to_char("They aren't here.\n\r", ch);
	    return;
	}

	/* clear zone for mobs */
	victim->zone = NULL;

	/* string something */

	if (!str_prefix(arg2, "name"))
	{
	    if (!IS_NPC(victim))
	    {
		send_to_char("Not on PC's.\n\r", ch);
		return;
	    }
	    free_string(victim->name);
	    victim->name = str_dup(arg3);
	    return;
	}

	if (!str_prefix(arg2, "description"))
	{
	    free_string(victim->description);
	    victim->description = str_dup(arg3);
	    return;
	}

	if (!str_prefix(arg2, "short"))
	{
	    free_string(victim->short_descr);
	    victim->short_descr = str_dup(arg3);
	    return;
	}

	if (!str_prefix(arg2, "long"))
	{
	    free_string(victim->long_descr);
	    strcat(arg3, "\n\r");
	    victim->long_descr = str_dup(arg3);
	    return;
	}

	if (!str_prefix(arg2, "title"))
	{
	    if (IS_NPC(victim))
	    {
		send_to_char("Not on NPC's.\n\r", ch);
		return;
	    }

	    set_title(victim, arg3);
	    return;
	}

	if (!str_prefix(arg2, "spec"))
	{
	    if (!IS_NPC(victim))
	    {
		send_to_char("Not on PC's.\n\r", ch);
		return;
	    }

	    if ((victim->spec_fun = spec_lookup(arg3)) == 0)
	    {
		send_to_char("No such spec fun.\n\r", ch);
		return;
	    }

	    return;
	}
    }

    if (!str_prefix(type, "object"))
    {
	/* string an obj */

	if ((obj = get_obj_world(ch, arg1)) == NULL)
	{
	    send_to_char("Nothing like that in heaven or earth.\n\r", ch);
	    return;
	}

	if (!str_prefix(arg2, "name"))
	{
	    free_string(obj->name);
	    obj->name = str_dup(arg3);
	    return;
	}

	if (!str_prefix(arg2, "short"))
	{
	    free_string(obj->short_descr);
	    obj->short_descr = str_dup(arg3);
	    return;
	}

	if (!str_prefix(arg2, "long"))
	{
	    free_string(obj->description);
	    obj->description = str_dup(arg3);
	    return;
	}

	if (!str_prefix(arg2, "ed") || !str_prefix(arg2, "extended"))
	{
	    EXTRA_DESCR_DATA *ed;

	    argument = one_argument(argument, arg3);
	    if (argument == NULL)
	    {
		send_to_char("Syntax: oset <object> ed <keyword> <string>\n\r",
			     ch);
		return;
	    }

	    strcat(argument, "\n\r");

	    ed = new_extra_descr();

	    ed->keyword		= str_dup(arg3    );
	    ed->description	= str_dup(argument);
	    ed->next		= obj->extra_descr;
	    obj->extra_descr	= ed;
	    return;
	}
    }


    /* echo bad use message */
    do_function(ch, &do_string, "");
}



void do_oset(CHAR_DATA *ch, char *argument)
{
    char arg1 [MAX_INPUT_LENGTH];
    char arg2 [MAX_INPUT_LENGTH];
    char arg3 [MAX_INPUT_LENGTH];
    OBJ_DATA *obj;
    int64_t value;

    smash_tilde(argument);
    argument = one_argument(argument, arg1);
    argument = one_argument(argument, arg2);
    strcpy(arg3, argument);

    if (arg1[0] == '\0' || arg2[0] == '\0' || arg3[0] == '\0')
    {
	send_to_char("Syntax:\n\r", ch);
	send_to_char("  set obj <object> <field> <value>\n\r", ch);
	send_to_char("  Field being one of:\n\r",				ch);
	send_to_char("    value0 value1 value2 value3 value4 (v1-v4)\n\r",	ch);
	send_to_char("    extra wear level weight cost timer\n\r",		ch);
	return;
    }

    if ((obj = get_obj_world(ch, arg1)) == NULL)
    {
	send_to_char("Nothing like that in heaven or earth.\n\r", ch);
	return;
    }

    /*
     * Snarf the value (which need not be numeric).
     */
    value = atoi(arg3);

    /*
     * Set something.
     */
    if (!str_cmp(arg2, "value0") || !str_cmp(arg2, "v0"))
	obj->value[0] = UMIN(50, value);
    else if (!str_cmp(arg2, "value1") || !str_cmp(arg2, "v1"))
	obj->value[1] = value;
    else if (!str_cmp(arg2, "value2") || !str_cmp(arg2, "v2"))
	obj->value[2] = value;
    else if (!str_cmp(arg2, "value3") || !str_cmp(arg2, "v3"))
	obj->value[3] = value;
    else if (!str_cmp(arg2, "value4") || !str_cmp(arg2, "v4"))
	obj->value[4] = value;
    if (!str_prefix(arg2, "extra"))
    {
	if ((value = flag_value(extra_flags, arg3)) != NO_FLAG)
	    TOGGLE_BIT(obj->extra_flags, value);
    }
    else if (!str_prefix(arg2, "wear"))
	obj->wear_flags = value;
    else if (!str_prefix(arg2, "level"))
	obj->level = value;
    else if (!str_prefix(arg2, "weight"))
	obj->weight = value;
    else if (!str_prefix(arg2, "cost"))
	obj->cost = value;
    else if (!str_prefix(arg2, "timer"))
	obj->timer = value;
    else
    {
	/*
	 * Generate usage message.
	 */
	do_function(ch, &do_oset, "");
	return;
    }
    send_to_char("Ok.\n\r", ch);
    return;
}



void do_rset(CHAR_DATA *ch, char *argument)
{
    char arg1 [MAX_INPUT_LENGTH];
    char arg2 [MAX_INPUT_LENGTH];
    char arg3 [MAX_INPUT_LENGTH];
    ROOM_INDEX_DATA *location;
    int64_t value;

    smash_tilde(argument);
    argument = one_argument(argument, arg1);
    argument = one_argument(argument, arg2);
    strcpy(arg3, argument);

    if (arg1[0] == '\0' || arg2[0] == '\0' || arg3[0] == '\0')
    {
	send_to_char("Syntax:\n\r", ch);
	send_to_char("  set room <location> <field> <value>\n\r", ch);
	send_to_char("  Field being one of:\n\r",			ch);
	send_to_char("    flags sector\n\r",				ch);
	return;
    }

    if ((location = find_location(ch, arg1)) == NULL)
    {
	send_to_char("No such location.\n\r", ch);
	return;
    }

    if ( ch->in_room != location
	 &&  room_is_private(location, NULL) && !IS_TRUSTED(ch, IMPLEMENTOR))
    {
	send_to_char("That room is private right now.\n\r", ch);
	return;
    }

    /*
     * Snarf the value.
     */
    if (!is_number(arg3))
    {
	send_to_char("Value must be numeric.\n\r", ch);
	return;
    }
    value = atoi(arg3);

    /*
     * Set something.
     */
    if (!str_prefix(arg2, "flags"))
	location->room_flags	= value;
    else if (!str_prefix(arg2, "sector"))
	location->sector_type	= value;
    else
    {
	/*
	 * Generate usage message.
	 */
	do_function(ch, &do_rset, "");
	return;
    }
    send_to_char("Ok.\n\r", ch);
    return;
}



int compare_descr (const void *v1, const void *v2)
{
    DESCRIPTOR_DATA *a1 = *(DESCRIPTOR_DATA**)v1;
    DESCRIPTOR_DATA *a2 = *(DESCRIPTOR_DATA**)v2;
    int i;

    i = strcmp(a1->ip , a2->ip);

    if (i < 0) return -1;
    if (i > 0) return +1;
    else       return 0;
}

char * cp_get_country(unsigned int address);

void do_sockets(CHAR_DATA *ch, char *argument)
{
    CHAR_DATA       *vch;
    DESCRIPTOR_DATA *d;
    char            buf  [ MAX_STRING_LENGTH ];
    int             count;
    char *          st;
    char            s[100];
    char            idle[10], compr[10];
    DESCRIPTOR_DATA * descr[MAX_DESCRIPTORS];
    int		    i, j;
    BUFFER          *output;

    i = 0;

    SLIST_FOREACH(d, &descriptor_list, link)
    {
	descr[i++] = d;
	if (i >= MAX_DESCRIPTORS)
	    break;
    }

    qsort(descr, i , sizeof(descr[0]), compare_descr);

    count       = 0;

    output = new_buf();
	add_buf(output, "\n\r[#       Connection State  Login  Idle] Player Name   Level  IP ({rnewbies{x)    Host\n\r");
	//add_buf(output, "\n\r[#       Connection State  Login  Idle] Player Name   Level  IP ({rnewbies{x)    Host\n\r");

    add_buf(output, "-----------------------------------------------------------------------------------------------------------------\n\r");

    for (j = 0; j < i; j++){
	    d = descr[j];

        if (d->character && can_see(ch, d->character) && (IS_NULLSTR(argument) || !str_prefix(argument, d->character->name))){
            /* NB: You may need to edit the CON_ values */
            switch(d->connected){
                case CON_PLAYING:               st = "PLAYING";    	    break;
                case CON_GET_NAME:              st = "Get Name";    	break;
                case CON_GET_OLD_PASSWORD:      st = "Get Old Passwd";  break;
                case CON_CONFIRM_NEW_NAME:      st = "Confirm Name";    break;
                case CON_GET_NEW_PASSWORD:      st = "Get New Passwd";  break;
                case CON_CONFIRM_NEW_PASSWORD:  st = "Confirm Passwd";  break;
                case CON_GET_NEW_RACE:          st = "Get New Race";    break;
                case CON_GET_NEW_SEX:           st = "Get New Sex";    	break;
                case CON_GET_NEW_CLASS:         st = "Get New Class";   break;
                case CON_GET_ALIGNMENT:         st = "Get New Align";	break;
                case CON_DEFAULT_CHOICE:	    st = "Choosing Cust";	break;
                case CON_GEN_GROUPS:	        st = "Customization";	break;
                case CON_PICK_WEAPON:	        st = "Picking Weapon";	break;
                case CON_READ_IMOTD:	        st = "Reading IMOTD"; 	break;
                case CON_BREAK_CONNECT:  	    st = "LINKDEAD";		break;
                case CON_READ_MOTD:             st = "Reading MOTD";    break;
                case CON_GET_CASES_1:
                case CON_GET_CASES_2:
                case CON_GET_CASES_3:
                case CON_GET_CASES_4:
                case CON_GET_CASES_5:	        st = "Choosing cases"; 	break;
                case CON_GET_COLOR:	            st = "Choosing color";	break;
                case CON_GET_CODEPAGE:	        st = "Get Codepage";    break;
                case CON_GET_EMAIL:	            st = "Get Email";    	break;
                default:                        st = "!UNKNOWN!";    	break;
            }

            count++;

            /* Format "login" value... */
            vch = CH(d);
            strftime(s, 100, "%H:%M", localtime(&vch->logon));

            if (vch->timer > 0)
            sprintf(idle, "%-2d", vch->timer);
            else
            sprintf(idle, "  ");

            if (d->out_compress && d->mccp_version > 0)
            sprintf(compr, "v%d", d->mccp_version);
            else
            strcpy(compr, "  ");

            sprintf(compr, "%s%s", compr, d->mxp ? "M" : " ");



















            sprintf(buf, "[%-3d %s %-16s  %5s   %-2s ] %-12s  [%s%2d{x]   %s%-15s{x %-32.32s %s\n\r",
                d->descriptor,
                compr,
                st,
                s,
                idle,
                (d->original) ? d->original->name
                : (d->character)  ? d->character->name
                : "(None!)",
                check_ban(d->ip, BAN_NEWBIES) && vch->level < 5 ? "{r" : "",
                vch->level,
                check_ban(d->ip, BAN_NEWBIES) ? "{r" : "",
               // d->ip,
               // d->Host, cp_get_country(inet_addr(d->ip)));
                d->ip);

            add_buf(output, buf);

        }
    }

    sprintf(buf, "\n\r%d user%s\n\r", count, count == 1 ? "" : "s");
    add_buf(output, buf);
    page_to_char(output->string, ch);
    free_buf(output);
    return;
}


/*
 * Thanks to Grodyn for pointing out bugs in this function.
 */
void do_force(CHAR_DATA *ch, char *argument)
{
    char buf[MAX_STRING_LENGTH];
    char arg[MAX_INPUT_LENGTH];
    char arg2[MAX_INPUT_LENGTH];

    argument = one_argument(argument, arg);

    if (arg[0] == '\0' || argument[0] == '\0')
    {
	send_to_char("Çàñòàâèòü êîãî ñäåëàòü ÷òî?\n\r", ch);
	return;
    }

    one_argument(argument, arg2);

    if (!str_cmp(arg2, "delete") || !str_prefix(arg2, "mob") ||
	!str_cmp(arg2, "óäàëèòü")/* áîëüíî íå áèòü 8) (Ñ)Âèêóñ */)
	{
	    send_to_char("Ýòîãî íå áóäåò ñäåëàíî.\n\r", ch);
	    return;
	}

    sprintf(buf, "$n ïðèíóæäàåò òåáÿ ñäåëàòü '%s'.", argument);

    if (!str_cmp(arg, "all")
	|| !str_cmp(arg, "âñåõ")
	|| !str_cmp(arg, "players")
	|| !str_cmp(arg, "èãðîêîâ"))
    {
	DESCRIPTOR_DATA *desc, *desc_next;

	SLIST_FOREACH_SAFE(desc, &descriptor_list, link, desc_next)
	{
	    if (desc->connected == CON_PLAYING
		&& get_trust(desc->character) <= LEVEL_HERO)
	    {
		act(buf, ch, NULL, desc->character, TO_VICT);
		interpret(desc->character, argument);
	    }
	}
    }
    else if (!str_cmp(arg, "gods") || !str_cmp(arg, "áîãîâ"))
    {
	DESCRIPTOR_DATA *desc, *desc_next;
	if (get_trust(ch) < MAX_LEVEL - 2)
	{
	    send_to_char("Not at your level!\n\r", ch);
	    return;
	}

	SLIST_FOREACH_SAFE(desc, &descriptor_list, link, desc_next)
	{
	    CHAR_DATA *vch = desc->character;

	    if (vch != NULL
		&& !IS_NPC(vch)
		&& get_trust(vch) < get_trust(ch)
		&& vch->level > LEVEL_HERO)
	    {
		act(buf, ch, NULL, vch, TO_VICT);
		interpret(vch, argument);
	    }
	}
    }
    else
    {
	CHAR_DATA *victim;

	if ((victim = get_char_world(ch, arg)) == NULL)
	{
	    send_to_char("Òàêèõ çäåñü íåò.\n\r", ch);
	    return;
	}

	if (victim == ch)
	{
	    send_to_char("Àãà, óæå áåãó èñïîëíÿòü!\n\r", ch);
	    return;
	}

	if ( ch->in_room != victim->in_room
	     && room_is_private(victim->in_room, NULL)
	     && !IS_TRUSTED(ch, IMPLEMENTOR))
	{
	    send_to_char("Ýòîò èãðîê â ïðèâàòíîé êîìíàòå ñåé÷àñ.\n\r", ch);
	    return;
	}

	if (get_trust(victim) > get_trust(ch))
	{
	    send_to_char("Äåëàé ýòî ñàì!\n\r", ch);
	    return;
	}
	/* À íàôèã force âîîáùå òîãäà íóæåí? *//*
	if (!IS_NPC(victim) && get_trust(ch) < MAX_LEVEL -3)
	{
	    send_to_char("Not at your level!\n\r", ch);
	    return;
	}*/

	act(buf, ch, NULL, victim, TO_VICT);
	interpret(victim, argument);
    }

    send_to_char("Ok.\n\r", ch);
    return;
}



/*
 * New routines by Dionysos.
 */
void do_invis(CHAR_DATA *ch, char *argument)
{
    int level;
    char arg[MAX_STRING_LENGTH];

    if (IS_SET(ch->act, PLR_FULL_SILENCE))
    {
	send_to_char("Òû íàâñåãäà ñêðûò.\n\r", ch);
	return;
    }

    /* RT code for taking a level argument */
    one_argument(argument, arg);

    if (arg[0] == '\0')
	/* take the default path */

	if (ch->invis_level)
	{
	    ch->invis_level = 0;
	    act("$n ìåäëåííî ïðîÿâëÿåòñÿ.", ch, NULL, NULL, TO_ROOM);
	    send_to_char("Òû ìåäëåííî ïðîÿâëÿåøüñÿ.\n\r", ch);
	}
	else
	{
	    ch->invis_level = /*get_trust(ch)*/ LEVEL_IMMORTAL;
	    act("$n ìåäëåííî ðàñòâîðÿåòñÿ â âîçäóõå.", ch, NULL, NULL, TO_ROOM);
	    send_to_char("Òû ìåäëåííî ðàñòâîðÿåøüñÿ â âîçäóõå.\n\r", ch);
	}
    else
	/* do the level thing */
    {
	level = atoi(arg);
	if (level < 2 || level > get_trust(ch))
	{
	    send_to_char("Invis level must be between 2 and your level.\n\r", ch);
	    return;
	}
	else
	{
	    ch->reply = NULL;
	    ch->invis_level = level;
	    act("$n ìåäëåííî ðàñòâîðÿåòñÿ â âîçäóõå.", ch, NULL, NULL, TO_ROOM);
	    send_to_char("Òû ìåäëåííî ðàñòâîðÿåøüñÿ â âîçäóõå.\n\r", ch);
	}
    }

    return;
}


void do_incognito(CHAR_DATA *ch, char *argument)
{
    int level;
    char arg[MAX_STRING_LENGTH];

    /* RT code for taking a level argument */
    one_argument(argument, arg);

    if (arg[0] == '\0')
	/* take the default path */

	if (ch->incog_level)
	{
	    char buf[30];

	    ch->incog_level = 0;
	    sprintf(buf, "$n òåïåðü áóäåò çàìåò%s.", ch->sex == SEX_MALE ? "åí" :
		    ch->sex == SEX_FEMALE ? "íà" : "íî");
	    act(buf, ch, NULL, NULL, TO_ROOM);
	    send_to_char("Òû áîëüøå íå ñêðûâàåøü ñâîåãî ïðèñóòñòâèÿ.\n\r", ch);
	}
	else
	{
	    ch->incog_level = /*get_trust(ch)*/ LEVEL_IMMORTAL;
	    act("$n ìàñêèðóåò ñâîå ñóùåñòâîâàíèå.", ch, NULL, NULL, TO_ROOM);
	    send_to_char("Òû ñêðûâàåøü ñâîå ïðèñóòñòâèå.\n\r", ch);
	}
    else
	/* do the level thing */
    {
	level = atoi(arg);
	if (level < 2 || level > get_trust(ch))
	{
	    send_to_char("Incog level must be between 2 and your level.\n\r", ch);
	    return;
	}
	else
	{
	    ch->reply = NULL;
	    ch->incog_level = level;
	    act("$n ìàñêèðóåò ñâîå ñóùåñòâîâàíèå.", ch, NULL, NULL, TO_ROOM);
	    send_to_char("Òû ñêðûâàåøü ñâîå ïðèñóòñòâèå.\n\r", ch);
	}
    }

    return;
}



void do_holylight(CHAR_DATA *ch, char *argument)
{
    if (IS_NPC(ch))
	return;

    if (IS_SET(ch->act, PLR_HOLYLIGHT))
    {
	REMOVE_BIT(ch->act, PLR_HOLYLIGHT);
	send_to_char("Holy light mode off.\n\r", ch);
    }
    else
    {
	SET_BIT(ch->act, PLR_HOLYLIGHT);
	send_to_char("Holy light mode on.\n\r", ch);
    }

    return;
}

/* prefix command: it will put the string typed on each line typed */

void do_prefi (CHAR_DATA *ch, char *argument)
{
    send_to_char("You cannot abbreviate the prefix command.\r\n", ch);
    return;
}

void do_prefix (CHAR_DATA *ch, char *argument)
{
    char buf[MAX_INPUT_LENGTH];

    if (argument[0] == '\0')
    {
	if (ch->prefix[0] == '\0')
	{
	    send_to_char("You have no prefix to clear.\r\n", ch);
	    return;
	}

	send_to_char("Prefix removed.\r\n", ch);
	free_string(ch->prefix);
	ch->prefix = str_dup("");
	return;
    }

    if (ch->prefix[0] != '\0')
    {
	sprintf(buf, "Prefix changed to %s.\r\n", argument);
	free_string(ch->prefix);
    }
    else
    {
	sprintf(buf, "Prefix set to %s.\r\n", argument);
    }

    ch->prefix = str_dup(argument);
}

void do_cases(CHAR_DATA *ch, char *argument)
{
    char buf[MAX_STRING_LENGTH];

    if (argument[0] == '\0') {send_to_char("×òî òû õîòåë ïîñêëîíÿòü?\n\r", ch); return; }

    sprintf(buf, "Êòî/÷òî             : {Y%s{x\n\r", argument);
    send_to_char(buf, ch);
    sprintf(buf, "Âçÿòü èç êîãî/÷åãî  : {Y%s{x\n\r", cases(argument, 1));
    send_to_char(buf, ch);
    sprintf(buf, "Äàòü êîìó/÷åìó      : {Y%s{x\n\r", cases(argument, 2));
    send_to_char(buf, ch);
    sprintf(buf, "Óäàðèòü êîãî/÷òî    : {Y%s{x\n\r", cases(argument, 3));
    send_to_char(buf, ch);
    sprintf(buf, "Óäàðèòü êåì/÷åì     : {Y%s{x\n\r", cases(argument, 4));
    send_to_char(buf, ch);
    sprintf(buf, "Ãîâîðèòü î êîì/î ÷åì: {Y%s{x\n\r", cases(argument, 5));
    send_to_char(buf, ch);
    sprintf(buf, "Âçÿòü êîãî/÷òî      : {Y%s{x\n\r", cases(argument, 6));
    send_to_char(buf, ch);

    sprintf(buf, "\n\rÃëàãîë              : {Y%s ðàçâàëèâà{x%s{Yñÿ{x\n\r",
	    argument, decompose_end(argument));
    send_to_char(buf, ch);
}

void do_rename(CHAR_DATA* ch, char* argument)
{
    char old_name[MAX_INPUT_LENGTH],
	 new_name[MAX_INPUT_LENGTH],
	 strsave [MAX_INPUT_LENGTH];

    CHAR_DATA* victim;
    FILE* file;
    bool exist;
    int pos;

    argument = one_argument(argument, old_name); /* find new/old name */
    argument = one_argument(argument, new_name);

    /* Trivial checks */
    if (!old_name[0])
    {
	send_to_char ("Ïåðåèìåíîâàòü êîãî?\n\r", ch);
	return;
    }

    victim = get_char_world (ch, old_name);

    if (!victim)
    {
	send_to_char ("Òàêèõ çäåñü íåò.\n\r", ch);
	return;
    }

    if (IS_NPC(victim))
    {
	send_to_char ("Òû íå ìîæåøü ïåðåèìåíîâûâàòü ìîáîâ.\n\r", ch);
	return;
    }

    pos = victim->position;

    /* allow rename self new_name, but otherwise only lower level */
    if (get_trust(victim) > get_trust(ch))
    {
	send_to_char("Íå ïîëó÷èëîñü.\n\r", ch);
	return;
    }

    /*	if (!victim->desc || (victim->desc->connected != CON_PLAYING))
     {
     send_to_char ("Ýòîò ïåðñîíàæ ïîòåðÿë ñâÿçü (èëè ÷òî-òî ïîäîáíîå).\n\r", ch);
     return;
     } */

    if (IS_NULLSTR(new_name))
    {
	send_to_char ("Ïåðåèìåíîâàòü â êàêîå èìÿ?\n\r", ch);
	return;
    }


    if (!check_parse_name(new_name, TRUE))
    {
	send_to_char ("Íåäîïóñòèìîå èìÿ, âûáåðè äðóãîå.\n\r", ch);
	return;
    }

    convert_name(old_name);
    convert_name(new_name);

    if (!strcmp(old_name, new_name))
    {
	int numb;
	char arg1[MAX_INPUT_LENGTH], arg[MAX_INPUT_LENGTH];

	one_argument(argument, arg);
	if (arg[0] == '\0')
	{
	    send_to_char("Êàêîé ïàäåæ òû õîòåë ñìåíèòü?\n\r", ch);
	    return;
	}
	numb=number_argument(arg, arg1);
	convert_name(arg1);
	switch(numb)
	{
	default: send_to_char("Íîìåð ïàäåæà äîëæåí áûòü â ïðåäåëàõ îò 1 äî 5.\n\r", ch); return;
	case 1:
		 free_string(victim->pcdata->cases[0]);
		 victim->pcdata->cases[0] = str_dup(arg1);
		 break;
	case 2:
		 free_string(victim->pcdata->cases[1]);
		 victim->pcdata->cases[1] = str_dup(arg1);
		 break;
	case 3:
		 free_string(victim->pcdata->cases[2]);
		 victim->pcdata->cases[2] = str_dup(arg1);
		 break;
	case 4:
		 free_string(victim->pcdata->cases[3]);
		 victim->pcdata->cases[3] = str_dup(arg1);
		 break;
	case 5:
		 free_string(victim->pcdata->cases[4]);
		 victim->pcdata->cases[4] = str_dup(arg1);
		 break;
	}
	sprintf(arg, "Èãðîê %s - Ïàäåæ %d óñòàíîâëåí â %s.\n\r", victim->name, numb, arg1);
	send_to_char(arg, ch);
	VALIDATE(victim);
	save_char_obj(victim, FALSE);
	return;
    }

    if (!check_parse_name(new_name, TRUE))
    {
	send_to_char ("Íîâîå èìÿ íåïðàâèëüíî.\n\r", ch);
	return;
    }

    /* First, check if there is a player named that off-line */

    sprintf(strsave, "%s%s", PLAYER_DIR, capitalize(new_name));

    if ((file = file_open (strsave, "r")) != NULL) /* attempt to to open pfile */
	exist = TRUE;
    else
	exist = FALSE;

    file_close (file);

    if (exist)
    {
	send_to_char ("Èãðîê ñ òàêèì èìåíåì óæå ñóùåñòâóåò!\n\r", ch);
	return;
    }


    /* Check .gz file ! */

    sprintf(strsave, "%s%s.gz", PLAYER_DIR, capitalize(new_name));
    if ((file = file_open (strsave, "r")) != NULL) /* attempt to to open pfile */
	exist = TRUE;
    else
	exist = FALSE;

    file_close (file);

    if (exist)
    {
	send_to_char ("Èãðîê ñ òàêèì èìåíåì óæå åñòü â ñæàòîì ôàéëå!\n\r", ch);
	return;
    }

    if (get_char_world(NULL, new_name)) /* check for playing level-1 non-saved */
    {
	send_to_char ("Èãðîê ñ òàêèì èìåíåì óæå ñóùåñòâóåò!\n\r", ch);
	return;
    }

    /* Save the filename of the old name */

    sprintf(strsave, "%s%s", PLAYER_DIR, capitalize(victim->name));


    /* Rename the character and save him to a new file */
    /* NOTE: Players who are level 1 do NOT get saved under a new name */

    if (strlen(victim->pcdata->old_names) < MAX_STRING_LENGTH/2)
    {
	char buf[MAX_STRING_LENGTH];

	sprintf(buf, "%s%s(%d) ", victim->pcdata->old_names, victim->name, victim->level);
	free_string(victim->pcdata->old_names);
	victim->pcdata->old_names = str_dup(buf);
    }

    free_string(victim->name);
    victim->name = str_dup (capitalize(new_name));

    VALIDATE(victim);
    save_char_obj(victim, FALSE);

    /* unlink the old file */
    unlink (strsave); /* unlink does return a value.. but we do not care */

    /* That's it! */

    send_to_char ("Ïåðñîíàæ ïåðåèìåíîâàí. Íå çàáóäü ïîìåíÿòü ïàäåæè!\n\r", ch);

    /*	victim->position = POS_STANDING; */ /* I am laaazy */
    victim->position = pos;
    act ("$n äàåò òåáå íîâîå èìÿ - $N!", ch, NULL, victim, TO_VICT);

} /* do_rename */


bool can_create_socket(OBJ_DATA *obj);


// #Äîáàâèòü
void do_addapply(CHAR_DATA *ch, char *argument){
    OBJ_DATA *obj;
    CHAR_DATA *victim;
    AFFECT_DATA paf;
    char arg1[MAX_INPUT_LENGTH];
    char arg2[MAX_INPUT_LENGTH];
    char arg3[MAX_INPUT_LENGTH];
    char argtemp[MAX_INPUT_LENGTH];
    char buf[MIL];

    int affect_modify = 1, enchant_type;

    /*    if (IS_NPC(ch))
     return; */

    argument = one_argument(argument, arg1);
    argument = one_argument(argument, arg2);
    strcpy(argtemp, argument);
    argument = one_argument(argument, arg3);

    if (arg1[0] == '\0' || arg2[0] == '\0' || arg3[0] == '\0'){
        send_to_char("Ñèíòàêñèñ: äîáàâèòü <îáúåêò> <ïàðàìåòð> <çíà÷åíèå>\n\r"
                     "èëè      : äîáàâèòü <÷àð> qp/bronze <çíà÷åíèå>\n\r\n\r", ch);

        if (IS_NPC(ch) || get_trust(ch) > MAX_LEVEL - 5)
            send_to_char("Ïàðàìåòð: hp str dex int wis "
                 "con mana move ac hitroll damroll saves name short "
                 "desc level timer cond owner socket\n\r", ch);
        else
            send_to_char("Apply can have following values: name short desc "
                 "timer owner\n\r", ch);
        return;
    }

    if (!str_cmp(arg2, "qp")){
		int qp, tm_time = current_time;
		DESCRIPTOR_DATA *d = NULL;
		bool fFree = FALSE;

		if (!is_number(arg3)){
	    	send_to_char("Òðåòèé àðãóìåíò äîëæåí áûòü ÷èñëîâûì!\n\r", ch);
	    	return;
		}

		qp = atoi(arg3);

		// Åñëè ÷àðà íåò â ìèðå - çàãðóçèì åãî
		if ((victim = get_char_world(NULL, arg1)) == NULL || IS_NPC(victim)){
	    	d = new_descriptor();

	    	if (!load_char_obj(d, arg1, FALSE, FALSE)){
				send_to_char("Òàêèõ íåò â ýòîì ìèðå.\n\r", ch);
				free_descriptor(d);
				return;
	    	}

	    	victim = d->character;
	    	fFree = TRUE;				// ôëàã, ÷òî ÷àðà íóæíî áóäåò âûãðóçèòü

	    	check_light_status(victim);

	    	if (victim->pcdata->lastlogof > 0) current_time = victim->pcdata->lastlogof;
		}

		victim->pcdata->quest_curr += qp;
//		if (qp > 0)
//	    	victim->pcdata->quest_accum += qp;

		if (!fFree){
	    	if (qp > 0)
				sprintf(arg1, "{GÁîãè äàþò òåáå çà òâîè ñòàðàíèÿ %d óäà÷è.{x\n\r", qp);
	    	else
				sprintf(arg1, "{GÁîãè èçûìàþò ó òåáÿ %d óäà÷è.{x\n\r", -qp);
	    		send_to_char(arg1, victim);
		} else {
	    	save_char_obj(victim, FALSE);
	    	extract_char_for_quit(victim, FALSE);
	    	free_descriptor(d);
	    	current_time = tm_time;
		}

		send_to_char("Ok.\n\r", ch);
		return;
    }


    if (!str_cmp(arg2, "bronze")){
		int bronze, tm_time = current_time;
		DESCRIPTOR_DATA *d = NULL;
		bool fFree = FALSE;

		if (!is_number(arg3)){
	    	send_to_char("Òðåòèé àðãóìåíò äîëæåí áûòü ÷èñëîâûì!\n\r", ch);
	    	return;
		}

		bronze = atoi(arg3);

		if ((victim = get_char_world(NULL, arg1)) == NULL || IS_NPC(victim)){
	    	d = new_descriptor();

	    	if (!load_char_obj(d, arg1, FALSE, FALSE))
	    	{
				send_to_char("Òàêèõ íåò â ýòîì ìèðå.\n\r", ch);
				free_descriptor(d);
				return;
	    	}

	    	victim = d->character;
	    	fFree = TRUE;

	    	check_light_status(victim);

	    	if (victim->pcdata->lastlogof > 0)
	    		current_time = victim->pcdata->lastlogof;

		}

    	victim->pcdata->bronze += bronze;

		if (!fFree){
	    	if (bronze > 0)
				sprintf(arg1, "{GÁîãè äàþò òåáå çà òâîè ñòàðàíèÿ %d áðîíçû.{x\n\r", bronze);
	    	else
				sprintf(arg1, "{GÁîãè èçûìàþò ó òåáÿ %d áðîíçû.{x\n\r", -bronze);
	    	send_to_char(arg1, victim);
		}
		else
		{
	    	save_char_obj(victim, FALSE);
	    	extract_char_for_quit(victim, FALSE);
	    	free_descriptor(d);
	    	current_time = tm_time;
		}

		send_to_char("Ok.\n\r", ch);
		return;
    }

    if ((obj = get_obj_carry(ch, arg1, ch)) == NULL){
		send_to_char("Ó òåáÿ ýòîãî íåò.\n\r", ch);
		return;
    }

    if (obj->pIndexData->edited){
		send_to_char("Ïîäîæäè, ýòîò îáúåêò ñåé÷àñ ðåäàêòèðóåòñÿ â OLC.\n\r", ch);
		return;
    }

    if (!str_cmp(arg2, "hp") && get_trust(ch) > MAX_LEVEL - 5)
	enchant_type = APPLY_HIT;
    else if (!str_cmp(arg2, "str") && (IS_NPC(ch) || get_trust(ch) > MAX_LEVEL - 5))
	enchant_type = APPLY_STR;
    else if (!str_cmp(arg2, "dex") && (IS_NPC(ch) || get_trust(ch) > MAX_LEVEL - 5))
	enchant_type = APPLY_DEX;
    else if (!str_cmp(arg2, "int") && (IS_NPC(ch) || get_trust(ch) > MAX_LEVEL - 5))
	enchant_type = APPLY_INT;
    else if (!str_cmp(arg2, "wis") && (IS_NPC(ch) || get_trust(ch) > MAX_LEVEL - 5))
	enchant_type = APPLY_WIS;
    else if (!str_cmp(arg2, "con") && (IS_NPC(ch) || get_trust(ch) > MAX_LEVEL - 5))
	enchant_type = APPLY_CON;
    else if (!str_cmp(arg2, "mana") && (IS_NPC(ch) || get_trust(ch) > MAX_LEVEL - 5))
	enchant_type = APPLY_MANA;
    else if (!str_cmp(arg2, "move") && (IS_NPC(ch) || get_trust(ch) > MAX_LEVEL - 5))
	enchant_type = APPLY_MOVE;
    else if (!str_cmp(arg2, "ac") && (IS_NPC(ch) || get_trust(ch) > MAX_LEVEL - 5))
	enchant_type = APPLY_AC;
    else if (!str_cmp(arg2, "hitroll") && (IS_NPC(ch) || get_trust(ch) > MAX_LEVEL - 5))
	enchant_type = APPLY_HITROLL;
    else if (!str_cmp(arg2, "damroll") && (IS_NPC(ch) || get_trust(ch) > MAX_LEVEL - 5))
	enchant_type = APPLY_DAMROLL;
    else if (!str_cmp(arg2, "saves") && (IS_NPC(ch) || get_trust(ch) > MAX_LEVEL - 5))
	enchant_type = APPLY_SAVING_SPELL;
    else if (!str_cmp(arg2, "level") && (IS_NPC(ch) || get_trust(ch) > MAX_LEVEL - 5)){
		int lvl;

		if (is_number(arg3)){
			lvl=atoi(arg3);
			if (lvl >= 0 && lvl <= MAX_LEVEL){
				obj->level = lvl;
				send_to_char("Ok.\n\r", ch);
			} else {
				send_to_char("Óðîâåíü äîëæåí áûòü â ðàçóìíûõ ïðåäåëàõ.\n\r", ch);
			}

		}
		else
			send_to_char("Çíà÷åíèå äîëæíî áûòü ÷èñëîâûì.\n\r", ch);
		return;
    }
    else if (!str_cmp(arg2, "desc"))
    {
	free_string(obj->description);
	obj->description = str_dup(argtemp);
	send_to_char("Ok.\n\r", ch);
	return;
    }
    else if (!str_cmp(arg2, "short"))
    {
	free_string(obj->short_descr);
	obj->short_descr = str_dup(argtemp);
	send_to_char("Ok.\n\r", ch);
	return;
    }
    else if (!str_cmp(arg2, "name"))
    {
	free_string(obj->name);
	obj->name = str_dup(argtemp);
	send_to_char("Ok.\n\r", ch);
	return;
    }
    else if (!str_cmp(arg2, "socket")){
		if (!can_create_socket(obj)){
			send_to_char("Â ýòîé âåùè íåëüçÿ ïðîäåëàòü ãíåçäî.\n\r", ch);
			return;
		}

		// Îòëàäêà, fprintf(fp, "ExtF %lu\n",	obj->extra_flags);
		sprintf(buf, "ExtF %lu\n\r",	obj->extra_flags);
		send_to_char(buf, ch);



		SET_BIT(obj->extra_flags, ITEM_HAS_SOCKET);
		send_to_char("Ok.\n\r", ch);

		// Îòëàäêà
		sprintf(buf, "ExtF %lu\n\r",	obj->extra_flags);
		send_to_char(buf, ch);


		return;
    } else if (!str_cmp(arg2, "timer")){
		int tmr;

		if (is_number(arg3)){
			tmr=atoi(arg3);
			if (tmr >= 0){
				obj->timer = tmr;
				send_to_char("Ok.\n\r", ch);
			} else {
				send_to_char("Òàéìåð äîëæåí áûòü áîëüøå ëèáî ðàâåí íóëþ.\n\r", ch);
			}
		} else {
			send_to_char("Çíà÷åíèå äîëæíî áûòü ÷èñëîâûì.\n\r", ch);
		}

		return;
    } else if (!str_cmp(arg2, "cond")){
		int c;

		if (is_number(arg3)){
			c = atoi(arg3);
			if (c >= 0)
			{
			obj->condition = c;
			send_to_char("Ok.\n\r", ch);
			}
			else
			{
			send_to_char("Ñîñòîÿíèå äîëæíî áûòü áîëüøå ëèáî ðàâíî "
					 "íóëþ.\n\r", ch);
			}
		} else {
			send_to_char("Çíà÷åíèå äîëæíî áûòü ÷èñëîâûì.\n\r", ch);
		}
		return;
    } else if (!str_cmp(arg2, "owner")){
		free_string(obj->person);
		obj->person = str_dup(ch->name);

		free_string(obj->owner);
		obj->owner = str_dup(arg3);

		send_to_char("Ok.\n\r", ch);

		return;
    }
    else if (!str_cmp(arg2, "death")){
		SET_BIT(obj->extra_flags, ITEM_ROT_DEATH);
		send_to_char("Ok.\n\r", ch);
		return;
    }
    else
    {
	send_to_char("That apply is not possible!\n\r", ch);
	return;
    }

    if (!is_number(arg3))
    {
	send_to_char("Applies require a value.\n\r", ch);
	return;
    }

    if (enchant_type < 25)
	affect_modify=atoi(arg3);

    affect_enchant(obj);

    if (enchant_type > 24)
	paf.type       = skill_lookup(arg2);
    else
	paf.type       = 0;

    paf.level      = ch->level;
    paf.duration   = -1;
    paf.location   = enchant_type;
    paf.modifier   = affect_modify;
    paf.bitvector  = 0;
    affect_to_obj(obj, &paf);

    send_to_char("Ok.\n\r", ch);
}


/*
 * Copywrite 1996 by Amadeus of AnonyMUD, AVATAR, Horizon MUD, and Despair
 *			(amadeus@myself.com)
 *
 * Public use authorized with this header intact.
 */
void do_wpeace(CHAR_DATA *ch, char *argument)
{
    DESCRIPTOR_DATA *d, *safe_d;
    CHAR_DATA *rch;
    char buf[MAX_STRING_LENGTH];

    SLIST_FOREACH_SAFE(d, &descriptor_list, link, safe_d)
    {
	rch = CH(d);

	if (rch->fighting)
	{
	    sprintf(buf, "%s îáúÿâëÿåò âñåîáùèé ìèð.\n\r", ch->name);
	    send_to_char(buf, rch);
	    stop_fighting(rch, TRUE);
	}
    }

    send_to_char("Òû îáúÿâëÿåøü âñåîáùèé ìèð.\n\r", ch);
    return;
}

void close_quest()
{
    int i;
    char buf[2*MSL], tm[MSL];
    DESCRIPTOR_DATA *d;

    if (!IS_NULLSTR(immquest.last_quest))
	free_string(immquest.last_quest);

    strftime(tm, 50, "%d.%m %H:%M", localtime(&current_time));

    sprintf(buf, "Ïîñëåäíèé êâåñò áûë çàêîí÷åí %s.\n\rÎïèñàíèå: %s\n\rÑ÷åò: %d %s\n\rÊòî îáúÿâèë: %s\n\r",
	    tm,
	    immquest.desc,
	    immquest.max_score,
	    (immquest.max_score == 0) ? "(áåç ñ÷åòà)" : "",
	    immquest.who_claim->name);

    immquest.last_quest = str_dup(buf);

    immquest.is_quest = FALSE;
    immquest.double_qp = 1;
    immquest.double_exp = 1;
    immquest.double_skill = 1;
    immquest.who_claim = NULL;

    if (immquest.nowar)
    {
	immquest.nowar = FALSE;
	clan_war_enabled = TRUE;
	SLIST_FOREACH(d, &descriptor_list, link)
	{
	    if (d->connected == CON_PLAYING)
	    {
		send_to_char("{RÊëàíîâûå âîéíû âñòóïàþò â ñèëó.{x\n\r",
			     d->character);
	    }
	}
    }

    for (i = 0; i < MAX_IN_QUEST; i++)
    {
	immquest.player[i].score = 0;
	immquest.player[i].name[0] = '\0';
    }
}

void do_immquest(CHAR_DATA *ch, char *argument)
{
    char arg[2*MAX_STRING_LENGTH];
    int num = 1, qp = 1, exp = 1, skill = 1, i;
    char *msg, *msg2;

    if (IS_NPC(ch)) return;

    msg = str_dup("");
    msg2 = str_dup("");

    argument = one_argument(argument, arg);
    if (arg[0] != '\0')
    {
	if (!str_prefix(arg, "îáúÿâèòü") || !str_prefix(arg, "claim"))
	{
	    if (immquest.is_quest)
	    {
		send_to_char("Ïîäîæäè, ïîêà çàêîí÷èòñÿ òåêóùèé êâåñò.\n\r", ch);
		return;
	    }
	    argument = one_argument(argument, arg);
	    if (!is_number(arg) || (immquest.max_score = atoi(arg)) < 0)
	    {
		send_to_char("Ñ÷åò äîëæåí áûòü ÷èñëîâûì è áûòü áîëüøå ëèáî ðàâíûì 0.\n\r", ch);
		return;
	    }
	    argument=one_argument(argument, arg);
	    if (!is_number(arg) || (immquest.timer = atoi(arg)) < 0)
	    {
		send_to_char("Òàéìåð äîëæåí áûòü ÷èñëîâûì è áûòü áîëüøå ëèáî ðàâíûì 0.\n\r", ch);
		return;
	    }

	    one_argument(argument, arg);

	    if (!str_cmp(arg, "nowar") || !str_cmp(arg, "âîéíà"))
	    {
		argument = one_argument(argument, arg);

		if (clan_war_enabled)
		{
		    immquest.nowar = TRUE;
		    do_clan(ch, "disable");
		}
	    }

	    strcpy(immquest.desc, argument);
	    immquest.who_claim = ch;
	    immquest.is_quest = TRUE;
	    do_function(ch, &do_immquest, "show");
	    sprintf(arg, "%s îáúÿâëÿåò êâåñò: {G%s{x{*\n\r", immquest.who_claim->name, immquest.desc);
	    do_function(ch, &do_echo, arg);
	    return;
	}

	if (!str_prefix(arg, "óäâîåíèå") || !str_prefix(arg, "double"))
	    num = 2;

	if (!str_prefix(arg, "óòðîåíèå") || !str_prefix(arg, "trouble"))
	    num = 3;

	if (!str_prefix(arg, "êâàäðî"))
	    num = 4;
	    
	if (!str_prefix(arg, "êâèíòî"))
	    num = 5;
	
	    
	if (num > 1)
	{
	    if (immquest.is_quest)
	    {
		send_to_char("Ïîäîæäè, ïîêà çàêîí÷èòñÿ òåêóùèé êâåñò.\n\r", ch);
		return;
	    }
    
	    for (i = 1; i < 4; i++)
	    {
		one_argument(argument, arg);

		if (is_number(arg))
		    break;

		argument = one_argument(argument, arg);

		if (!str_prefix(arg, "qp"))
		{
		    qp = num;
		    free_string(msg);
		    msg = str_dup("óäà÷è");
		}
		if (!str_prefix(arg, "exp"))
		{
		    exp = num;
		    free_string(msg);
		    msg = str_dup("îïûòà");
		}
		if (qp > 1 && exp > 1)
		{
		    free_string(msg);
		    msg = str_dup("óäà÷è, îïûòà");
		}
		if (!str_prefix(arg, "skill"))
		{
		    skill = num;
		    free_string(msg2);
		    msg2 = str_dup("ñêîðîñòè îáó÷åíèÿ");
		}
	    }

	    if (qp == 1 && exp == 1 && skill == 1)
	    {
		send_to_char("Äîñòóïíûå ïàðàìåòðû: QP, EXP, SKILL.\n\r",ch);
		return;
	    }		

	    argument = one_argument(argument, arg);

	    if (!is_number(arg) || (immquest.timer = atoi(arg)) < 0)
	    {
		send_to_char("Òàéìåð äîëæåí áûòü ÷èñëîâûì è áûòü áîëüøå ëèáî ðàâíûì 0.\n\r", ch);
		return;
	    }

	    one_argument(argument, arg);

	    if (!str_cmp(arg, "nowar") || !str_cmp(arg, "âîéíà"))
	    {
		argument = one_argument(argument, arg);

		if (clan_war_enabled)
		{
		    immquest.nowar = TRUE;
		    do_clan(ch, "disable");
		}
	    }

	    immquest.who_claim    = ch;
	    if (qp > 1 || exp > 1)
	    {
		sprintf(immquest.desc, num == 2 ? "Ìàëîå óñèëåíèå ïîëó÷àåìûõ î÷êîâ %s%s%s." : "Áîëüøîå óñèëåíèå ïîëó÷àåìûõ î÷êîâ %s%s%s.", msg, (skill > 1 ? " è " : ""), msg2);
		sprintf(arg, "%s îáúÿâëÿåò î %s ïîëó÷àåìûõ î÷êîâ %s%s%s!{x{*\n\r", immquest.who_claim->name,
				num == 2 ? "ìàëîì óñèëåíèè" : "áîëüøîì óñèëåíèè", msg, (skill > 1 ? " è " : ""), msg2);
	    }
	    else
	    {
		sprintf(immquest.desc, 
		    num == 2 ? "Ìàëîå óñèëåíèå %s." : "Áîëüøîå óñèëåíèå %s.", msg2);
		sprintf(arg, "%s îáúÿâëÿåò î %s %s!{x{*\n\r", immquest.who_claim->name,
		    num == 2 ? "ìàëîì óñèëåíèè" : "áîëüøîì óñèëåíèè", msg2);
	    }
	    immquest.is_quest     = TRUE;
	    immquest.double_qp    = qp;
	    immquest.double_exp   = exp;
	    immquest.double_skill = skill;

	    do_function(ch, &do_echo, arg);
	    return;
	}

	if (!str_prefix(arg, "ïîñëåäíèé") || !str_prefix(arg, "last"))
	{
	    printf_to_char("%s\n\r", ch,
			   IS_NULLSTR(immquest.last_quest)
			   ? "Êâåñòîâ ïîñëå ðåáóòà îáúÿâëåíî íå áûëî."
			   : immquest.last_quest);
	    return;
	}

	if (!immquest.is_quest)
	{
	    send_to_char("Ïîêà íèêàêèõ êâåñòîâ íå îáúÿâëåíî.\n\r", ch);
	    return;
	}
	else if (!str_prefix(arg, "ïîêàçàòü") || !str_prefix(arg, "show"))
	{
	    sprintf(arg, "Îáúÿâëåí êâåñò:\n\rÎïèñàíèå: %s\n\rÑ÷åò: %d %s\n\rÒàéìåð: %d %s\n\rÊòî îáúÿâèë: %s\n\r",
		    immquest.desc,
		    immquest.max_score,
		    (immquest.max_score == 0) ? "(áåç ñ÷åòà)" : "",
		    immquest.timer,
		    (immquest.timer == 0) ? "(áåç îãðàíè÷åíèÿ âðåìåíè)" : "",
		    immquest.who_claim->name);
	    send_to_char(arg, ch);
	    return;
	}
	else if (!str_prefix(arg, "çàêîí÷èòü") || !str_prefix(arg, "end"))
	{
	    if (ch != immquest.who_claim && ch->level < MAX_LEVEL)
	    {
		send_to_char("Èçâèíè, òû íå ìîæåøü ýòîãî ñäåëàòü\n\r", ch);
		return;
	    }
	    sprintf(arg, "%s îáúÿâëÿåò êâåñò '%s' çàêðûòûì.\n\r", ch->name, immquest.desc);
	    do_function(ch, &do_echo, arg);
	    close_quest();
	    return;
	}
	else if (!str_prefix(arg, "äîáàâèòü") || !str_prefix(arg, "add"))
	{
	    CHAR_DATA *wch;
	    int i, delta = 0, fNull;

	    if (immquest.double_qp > 1 || immquest.double_exp > 1)
	    {
		send_to_char("Ýòî áåññìûñëåííî.\n\r", ch);
		return;
	    }

	    argument = one_argument(argument, arg);

	    if ((wch = get_char_world(ch, arg)) == NULL || IS_NPC(wch))
	    {
		send_to_char("Èçâèíè, òàêèõ èãðîêîâ ñåé÷àñ íåò â ìèðå.\n\r", ch);
		return;
	    }

	    one_argument(argument, arg);
	    if (is_number(arg))
		delta = atoi(arg);
	    else
	    {
		send_to_char("Òû äîëæåí äàòü ÷èñëîâîé àðãóìåíò.\n\r", ch);
		return;
	    }

	    for (i = 0, fNull = MAX_IN_QUEST; i < MAX_IN_QUEST; i++)
	    {
		if (immquest.player[i].name[0] == '\0')
		{
		    if (fNull > i)
			fNull = i;
		}
		else if (!str_cmp(wch->name, immquest.player[i].name))
		{
		    fNull = i;
		    break;
		}
	    }

	    if (fNull == MAX_IN_QUEST)
	    {
		send_to_char("Ñëèøêîì ìíîãî èãðîêîâ çàíÿòî â êâåñòå...\n\r", ch);
		return;
	    }

	    immquest.player[fNull].score = UMAX(0, immquest.player[fNull].score + delta);
	    strcpy(immquest.player[fNull].name, wch->name);

	    if (immquest.player[fNull].score >= immquest.max_score)
	    {
		sprintf(arg, "{gÊâåñò '%s' îáúÿâëÿåòñÿ çàêðûòûì.\n\rÏîáåäèòåëü - {G%s{x",
			immquest.desc, immquest.player[fNull].name);
		do_function(ch, &do_echo, arg);
		close_quest();
		return;
	    }
	    send_to_char("Ok.\n\r", ch);
	    do_function(ch, &do_immquest, "score");
	    return;
	}
	else if (!str_prefix(arg, "ñ÷åò") || !str_prefix(arg, "score"))
	{
	    int i;

	    if (immquest.double_qp > 1 || immquest.double_exp > 1)
	    {
		send_to_char("Ýòî áåññìûñëåííî.\n\r", ch);
		return;
	    }

	    sprintf(arg, "Òåêóùèé ñ÷åò:\n\r");
	    for (i=0; i < MAX_IN_QUEST; i++)
	    {
		char buf1[MAX_STRING_LENGTH];
		if (immquest.player[i].name[0] != '\0')
		{
		    sprintf(buf1, "%-12s %d\n\r", immquest.player[i].name, immquest.player[i].score);
		    strcat(arg, buf1);
		}
	    }
	    send_to_char(arg, ch);
	    return;
	}
	else if (!str_prefix(arg, "êîíòåéíåð") || !str_prefix(arg, "container"))
	{
	    OBJ_DATA *obj;
	    CHAR_DATA *victim;

	    if (immquest.double_qp > 1 || immquest.double_exp > 1)
	    {
		send_to_char("Ýòî áåññìûñëåííî.\n\r", ch);
		return;
	    }

	    one_argument(argument, arg);

	    if ((victim = get_char_world(ch, arg)) == NULL || IS_NPC(victim))
	    {
		send_to_char("Èçâèíè, òàêèõ èãðîêîâ ñåé÷àñ íåò â ìèðå.\n\r", ch);
		return;
	    }

	    if (get_obj_carry(ch, victim->name, ch) != NULL)
	    {
		send_to_char("Ó òåáÿ óæå åñòü òàêîé êîíòåéíåð.\n\r", ch);
		return;
	    }

	    obj = create_object(get_obj_index(OBJ_VNUM_DISC), 0);
	    free_string(obj->name);
	    obj->name = str_dup(victim->name);

	    sprintf(arg, "÷òî ïðèíåñ %s", victim->name);
	    free_string(obj->short_descr);
	    obj->short_descr = str_dup(arg);
	    obj->value[0] = obj->value[3] = 99999;
	    obj_to_char(obj, ch);
	    return;
	}
    }
    send_to_char("Íåò òàêîé êîìàíäû. Ñïèñîê äîñòóïíûõ:\n\r"
		 "èììêâåñò îáúÿâèòü  <ñ÷åò> <òàéìåð> [nowar] <îïèñàíèå>\n\r"
		 "èììêâåñò äîáàâèòü  <èìÿ èãðîêà> <êîëè÷åñòâî>\n\r"
		 "èììêâåñò êîíòåéíåð <èìÿ èãðîêà>\n\r"
		 "èììêâåñò çàêîí÷èòü\n\r"
		 "èììêâåñò ïîêàçàòü\n\r"
		 "èììêâåñò ñ÷åò\n\r"
		 "èììêâåñò ïîñëåäíèé\n\r"
		 "èììêâåñò óäâîåíèå [qp] [exp] [skill] <timer> [nowar]\n\r"
		 "ïðè íåîáõîäèìîñòè äàòü óäâîåíèå/óòðîåíèå íà íåñêîëüêî ïàðàìåòðîâ\n\r"
		 "èõ ïèñàòü ñëåäóåò ÷åðåç ïðîáåë ({yèììêâåñò óäâîåíèå qp exp skill ...{x)\n\r", ch);
    return;
}

void do_setalign(CHAR_DATA *ch, char *argument)
{
    int align;
    char arg[MAX_INPUT_LENGTH];
    CHAR_DATA *vch;

    if (argument[0] == '\0')
    {
	send_to_char("Ñèíòàêñèñ: íàòóðà <èãðîê> <origin|current> <alignment>.\n\r", ch);
	return;
    }

    argument=one_argument(argument, arg);

    if ((vch=get_char_world(ch, arg)) == NULL || IS_NPC(vch))
    {
	send_to_char("Òàêèõ íåò â ýòîì ìèðå.\n\r", ch);
	return;
    }

    if (get_trust(vch) >= get_trust(ch))
    {
	send_to_char("Íå âûõîäèò.\n\r", ch);
	return;
    }

    argument=one_argument(argument, arg);

    if (is_number(argument))
    {
	align=atoi(argument);
	if (align < -1000 || align > 1000)
	{
	    send_to_char("Òû äîëæåí ââåñòè ðàçóìíûé ïàðàìåòð (îò -1000 äî 1000).\n\r", ch);
	    return;
	}
	if (!str_prefix(arg, "current"))
	    vch->alignment = align;
	else if (!str_prefix(arg, "origin"))
	    vch->pcdata->orig_align = align;
	else
	{
	    send_to_char("Âòîðîé ïàðàìåòð ìîæåò áûòü ëèáî current, ëèáî origin.\n\r", ch);
	    return;
	}
	send_to_char("Ok.\n\r", ch);
    }
    else
	send_to_char("Òû äîëæåí ââåñòè ÷èñëîâîé àðãóìåíò.\n\r", ch);
}

void do_olevel(CHAR_DATA *ch, char *argument)
{
    char buf[MAX_INPUT_LENGTH];
    char level[MAX_INPUT_LENGTH];
    char name[MAX_INPUT_LENGTH];
    char stype[MAX_INPUT_LENGTH];
    BUFFER *buffer;
    OBJ_DATA *obj;
    OBJ_DATA *in_obj;
    bool found;
    int number = 0, max_found;
    int type = -1, wear = -1;
    int lev;

    found = FALSE;
    number = 0;
    max_found = 200;

    argument = one_argument(argument, level);
    if (IS_NULLSTR(level))
    {
	send_to_char("Ñèíòàêñèñ: îëåâåë <level>\n\r", ch);
	send_to_char("           îëåâåë <level> <name>\n\r", ch);
	send_to_char("           îëåâåë <level> type <type> [<wear>]\n\r", ch);
	return;
    }

    argument = one_argument(argument, name);
    if (!str_cmp(name, "type"))
    {
	argument = one_argument(argument, stype);

	if (IS_NULLSTR(stype))
	{
	    send_to_char("Íå óêàçàí òèï.\n\r", ch);
	    return;
	}

	type = flag_lookup(stype, type_flags);
	if (type == NO_FLAG)
	{
	    send_to_char("Íåèçâåñòíûé òèï.\n\r", ch);
	    return;
	}

	if (!IS_NULLSTR(argument))
	{
	    wear = flag_lookup(argument, wear_flags);
	    if (wear == NO_FLAG)
	    {
		send_to_char("Unknown wear location.\n\r", ch);
		return;
	    }
	}
    }

    buffer = new_buf();

    lev = atoi(level);

    for (obj = object_list; obj != NULL; obj = obj->next)
    {
	if (obj->level != lev)
	    continue;

	if ((name[0] != '\0' && !is_name(name, obj->name) && type == -1)
	    || (type != -1 && obj->item_type != type)
	    || (wear != -1 && !IS_SET(obj->wear_flags, wear)))
	{
	    continue;
	}

	found = TRUE;
	number++;

	for (in_obj = obj; in_obj->in_obj != NULL; in_obj = in_obj->in_obj)
	    ;

	if (in_obj->carried_by != NULL
	    && can_see(ch, in_obj->carried_by)
	    && in_obj->carried_by->in_room != NULL)
	{
	    sprintf(buf, "%3d) %s is carried by %s [Room %d]\n\r",
		    number, obj->short_descr,
		    PERS(in_obj->carried_by, ch, 0),
		    in_obj->carried_by->in_room->vnum);
	}
	else if (in_obj->in_room != NULL
		 && can_see_room(ch, in_obj->in_room))
	{
	    sprintf(buf, "%3d) %s is in %s [Room %d]\n\r",
		    number, obj->short_descr, in_obj->in_room->name,
		    in_obj->in_room->vnum);
	}
	else
	{
	    sprintf(buf, "%3d) %s is somewhere\n\r",
		    number, obj->short_descr);
	}

	buf[0] = UPPER(buf[0]);
	add_buf(buffer, buf);

	if (number >= max_found)
	    break;
    }

    if (!found)
	send_to_char("Nothing like that in heaven or earth.\n\r", ch);
    else
	page_to_char(buf_string(buffer), ch);

    free_buf(buffer);
}

void do_mlevel(CHAR_DATA *ch, char *argument)
{
    char buf[MAX_INPUT_LENGTH];
    BUFFER *buffer;
    CHAR_DATA *victim;
    bool found;
    int count = 0, level;

    if (argument[0] == '\0')
    {
	send_to_char("Syntax: mlevel <level>\n\r", ch);
	return;
    }


    if (!is_number(argument))
    {
	send_to_char("Àðãóìåíò äîëæåí áûòü ÷èñëîâûì!\n\r", ch);
	return;
    }

    level = atoi(argument);

    if (level < 1 || level > MAX_LEVEL)
    {
	send_to_char("Óðîâåíü äîëæåí áûòü â ðàçóìíûõ ïðåäåëàõ!\n\r", ch);
	return;
    }


    found = FALSE;
    buffer = new_buf();
    LIST_FOREACH(victim, &char_list, link)
    {
	if (victim->in_room != NULL
	    &&   level == victim->level
	    && (IS_NPC(victim) || get_trust(ch) >= get_trust(victim)))
	{
	    found = TRUE;
	    count++;
	    sprintf(buf, "%3d) [%5d] %-28s [%5d] %s\n\r", count,
		    IS_NPC(victim) ? victim->pIndexData->vnum : 0,
		    IS_NPC(victim) ? victim->short_descr : victim->name,
		    victim->in_room->vnum,
		    victim->in_room->name);
	    add_buf(buffer, buf);
	}
    }

    if (!found)
	act("You didn't find any mob of level $T.", ch, NULL, argument, TO_CHAR);
    else
	page_to_char(buf_string(buffer), ch);

    free_buf(buffer);

    return;
}

int compare_objects (const void *v1, const void *v2)
{
    OBJ_INDEX_DATA *a1 = *(OBJ_INDEX_DATA**)v1;
    OBJ_INDEX_DATA *a2 = *(OBJ_INDEX_DATA**)v2;

    if (a1->level < a2->level) return -1;
    if (a1->level > a2->level) return +1;
    else       return 0;
}


void do_otype(CHAR_DATA *ch, char *argument)
{
    int type;
    int type2;
    int vnum, count = 0;
    char buf[MAX_STRING_LENGTH];
    BUFFER *buffer;
    char arg1[MAX_INPUT_LENGTH];
    char arg2[MAX_INPUT_LENGTH];
    char *item;
    OBJ_INDEX_DATA *obj, *array[top_vnum_obj];
    bool found;

    item = one_argument(argument, arg1);
    one_argument (item , arg2);

    found = FALSE;

    if (arg1[0] == '\0')
    {
	send_to_char("Syntax: otype [<type>|all] <wear/weapon>\n\rotype light\n\rotype weapon axe\n\rotype armor hands\n\rotype jewelry neck\n\rotype all arms\n\r", ch);
	return;
    }

    if (str_cmp(arg1, "all"))
    {
	if ((type = flag_value(type_flags, arg1)) == NO_FLAG)
	{
	    send_to_char("Unknown Type.\n\r", ch);
	    return;
	}
    }
    else
	type = -999;

    type2 = 0;

    if (arg2[0] != '\0')
    {
	if (type == ITEM_WEAPON)
	{
	    if ((type2 = flag_value(weapon_class, arg2)) == NO_FLAG)
	    {
		send_to_char("No such weapon type.\n\r", ch);
		return;

	    }
	}
	else if ((type2 = flag_value(wear_flags, arg2)) == NO_FLAG)
	{
	    send_to_char("No such wear location.\n\r", ch);
	    return;
	}
    }

    if (type == -999 && (arg2[0] == '\0' || type2 == ITEM_TAKE))
    {
	send_to_char("Ñëèøêîì áîëüøîé ñïèñîê. Óòî÷íè ïàðàìåòðû.\n\r", ch);
	return;
    }

    buffer = new_buf();

    for (vnum = 1; vnum <= top_vnum_obj; vnum++)
    {
	if ((obj = get_obj_index(vnum)) != NULL)
	{
	    if ((obj->item_type == type || type == -999)
		&& (type2 == 0
		    || (obj->value[0] == type2 && type == ITEM_WEAPON)
		    || (IS_SET(obj->wear_flags, type2) && type != ITEM_WEAPON)))
	    {
		array[count++] = obj;
		found = TRUE;
	    }
	}
    }

    if (!found)
    {
	send_to_char("No objects of that type exist\n\r", ch);
	free_buf(buffer);
	return;
    }

    qsort(array, count, sizeof(obj), compare_objects);

    for (vnum = 0; vnum < count; vnum++)
    {
	sprintf(buf, "%s: level: [%-2d]  vnum: [%-5d]  [%s]\n\r",
		str_color_fmt(array[vnum]->short_descr, 35), array[vnum]->level,
		array[vnum]->vnum, array[vnum]->area->name);
	add_buf(buffer, buf);
    }

    sprintf(buf, "\n\rÍàéäåíî: %d\n\r", count);
    add_buf(buffer, buf);

    page_to_char(buf_string(buffer), ch);
    free_buf(buffer);
}

void do_immtitle (CHAR_DATA* ch, char* argument)
{
    char name[MAX_INPUT_LENGTH];
    CHAR_DATA* victim;

    argument = one_argument(argument, name);

    if ((victim=get_char_world(ch, name)) == NULL || IS_NPC(victim))
    {
	send_to_char("Òàêèõ íåò â ýòîì ìèðå.\n\r", ch);
	return;
    }

    if (get_trust(victim) >= get_trust(ch) && victim != ch)
    {
	send_to_char("Íå ïîëó÷àåòñÿ.\n\r", ch);
	return;
    }

    one_argument(argument, name);

    if (name[0] != '\0' && !str_prefix(name, "pretitle"))
    {
	argument = one_argument(argument, name);
	free_string(victim->pcdata->pretitle);
	victim->pcdata->pretitle = str_dup(argument);
    }
    else
	set_title(victim, argument);

    send_to_char("Ok.\n\r", ch);
    return;
}

bool excempt(CHAR_DATA *ch, CHAR_DATA *victim, OBJ_DATA *lobj, char *argument, int vnum)
{
    if (is_name(argument, lobj->name) || lobj->pIndexData->vnum == vnum)
    {
	if (lobj->in_obj != NULL)
	    obj_from_obj(lobj);
	else
	    obj_from_char(lobj, TRUE);

	obj_to_char(lobj, ch);
	act("$n èçûìàåò ó òåáÿ $p6.", ch, lobj, victim, TO_VICT);
	act("Òû èçûìàåøü ó $N1 $p6.", ch, lobj, victim, TO_CHAR);
	return TRUE;
    }
    return FALSE;
}

void do_excempt(CHAR_DATA *ch, char *argument)
{
    char name[MAX_INPUT_LENGTH];
    CHAR_DATA *victim;
    OBJ_DATA *lobj, *lobj_next;
    int vnum=-1;

    if (IS_NPC(ch)) return;

    if (argument[0] == '\0')
    {
	send_to_char("Ñèíòàêñèñ: èçúÿòü <èìÿ èãðîêà> <èìÿ âåùè>|<vnum âåùè>\n\r", ch);
	return;
    }

    argument=one_argument(argument, name);

    if ((victim = get_char_world(ch, name)) == NULL)
    {
	send_to_char("Òàêèõ íåò â ýòîì ìèðå.\n\r", ch);
	return;
    }

    if (victim != ch && get_trust(victim) > get_trust(ch))
    {
	send_to_char("Íå ïîëó÷àåòñÿ.\n\r", ch);
	return;
    }

    if (argument[0] == '\0')
    {
	send_to_char("×òî òû õîòåë èçúÿòü ó ýòîãî èãðîêà?\n\r", ch);
	return;
    }

    if (is_number(argument))
	vnum = atoi(argument);

    for (lobj = victim->carrying; lobj != NULL; lobj = lobj_next)
    {
	OBJ_DATA *obj_in, *in_next;

	lobj_next = lobj->next_content;

	if (excempt(ch, victim, lobj, argument, vnum))
	    return;

	for (obj_in = lobj->contains; obj_in != NULL; obj_in = in_next)
	{
	    in_next = obj_in->next_content;
	    if (excempt(ch, victim, obj_in, argument, vnum))
		return;
	}
    }
    send_to_char("Ó ýòîãî èãðîêà íåò òàêîé âåùè.\n\r", ch);
}

void do_arealinks(CHAR_DATA *ch, char *argument)
{
    FILE *fp = NULL;
    AREA_DATA *parea;
    EXIT_DATA *pexit;
    ROOM_INDEX_DATA *to_room;
    ROOM_INDEX_DATA *from_room;
    char buf[MAX_STRING_LENGTH];
    char arg1[MAX_INPUT_LENGTH];
    char arg2[MAX_INPUT_LENGTH];
    int vnum = 0;
    int iHash, door;
    bool found = FALSE;

    argument = one_argument(argument, arg1);
    argument = one_argument(argument, arg2);

    /* First, the 'all' option */
    if (!str_cmp(arg1, "all"))
    {
	BUFFER *buffer = NULL;
	/*
	 * If a filename was provided, try to open it for writing
	 * If that fails, just spit output to the screen.
	 */
	if (arg2[0] != '\0')
	    if ((fp = open_report_file(ch, arg2)) == NULL)
	    {
		send_to_char("Error opening file, printing to screen.\n\r", ch);
		file_close(fp);
	    }

	/* Open a buffer if it's to be output to the screen */
	if (!fp)
	    buffer = new_buf();

	/* Loop through all the areas */
	for (parea = area_first; parea != NULL; parea = parea->next)
	{
	    /* First things, add area name  and vnums to the buffer */
	    sprintf(buf, "*** %s (%d to %d) ***\n\r",
		    parea->name, parea->min_vnum, parea->max_vnum);
	    fp ? fprintf(fp, buf) : add_buf(buffer, buf);

	    /* Now let's start looping through all the rooms. */
	    found = FALSE;
	    for(iHash = 0; iHash < MAX_KEY_HASH; iHash++)
	    {
		for(from_room = room_index_hash[iHash];
		    from_room != NULL;
		    from_room = from_room->next)
		{
		    /*
		     * If the room isn't in the current area,
		     * then skip it, not interested.
		     */
		    if (from_room->vnum < parea->min_vnum
			||   from_room->vnum > parea->max_vnum)
			continue;

		    /* Aha, room is in the area, lets check all directions */
		    for (door = 0; door < MAX_DIR; door++)
		    {
			/* Does an exit exist in this direction? */
			if((pexit = from_room->exit[door]) != NULL)
			{
			    to_room = pexit->u1.to_room;

			    /*
			     * If the exit links to a different area
			     * then add it to the buffer/file
			     */
			    if (to_room != NULL
				&&  (to_room->vnum < parea->min_vnum
				     ||   to_room->vnum > parea->max_vnum))
			    {
				found = TRUE;
				sprintf(buf, "    (%d) links %s to %s (%d)\n\r",
					from_room->vnum, dir_name[door],
					to_room->area->name, to_room->vnum);

				/* Add to either buffer or file */
				if(fp == NULL)
				    add_buf(buffer, buf);
				else
				    fprintf(fp, buf);
			    }
			}
		    }
		}
	    }

	    /* Informative message for areas with no external links */
	    if (!found)
	    {
		strcpy(buf, "    No links to other areas found.\n\r");
		if (fp)
		    fprintf(fp, buf);
		else
		    add_buf(buffer, buf);
	    }
	}

	/* Send the buffer to the player */
	if (!fp)
	{
	    page_to_char(buf_string(buffer), ch);
	    free_buf(buffer);
	}
	/* Or just clean up file stuff */
	else
	    file_close(fp);

	return;
    }

    /* No argument, let's grab the char's current area */
    if (arg1[0] == '\0')
    {
	parea = ch->in_room ? ch->in_room->area : NULL;

	/* In case something wierd is going on, bail */
	if (parea == NULL)
	{
	    send_to_char("You aren't in an area right now, funky.\n\r", ch);
	    return;
	}
    }
    /* Room vnum provided, so lets go find the area it belongs to */
    else if (is_number(arg1))
    {
	vnum = atoi(arg1);

	/* Hah! No funny vnums! I saw you trying to break it... */
	if (vnum <= 0 || vnum > MAX_VNUM)
	{
	    sprintf(buf, "The vnum must be between 1 and %d.\n\r", MAX_VNUM);
	    send_to_char(buf, ch);
	    return;
	}

	/* Search the areas for the appropriate vnum range */
	for (parea = area_first; parea != NULL; parea = parea->next)
	{
	    if (vnum >= parea->min_vnum && vnum <= parea->max_vnum)
		break;
	}

	/* Whoops, vnum not contained in any area */
	if (parea == NULL)
	{
	    send_to_char("There is no area containing that vnum.\n\r", ch);
	    return;
	}
    }
    /* Non-number argument, must be trying for an area name */
    else
    {
	/* Loop the areas, compare the name to argument */
	for(parea = area_first; parea != NULL; parea = parea->next)
	{
	    if (!str_prefix(arg1, parea->name))
		break;
	}

	/* Sorry chum, you picked a goofy name */
	if (parea == NULL)
	{
	    send_to_char("There is no such area.\n\r", ch);
	    return;
	}
    }

    /* Just like in all, trying to fix up the file if provided */
    if (arg2[0] != '\0')
    {
	if((fp = open_report_file(ch, arg2)) == NULL)
	{
	    send_to_char("Error opening file, printing to screen.\n\r", ch);
	    file_close(fp);
	}
    }

    /* And we loop the rooms */
    for(iHash = 0; iHash < MAX_KEY_HASH; iHash++)
    {
	for(from_room = room_index_hash[iHash];
	    from_room != NULL;
	    from_room = from_room->next)
	{
	    /* Gotta make sure the room belongs to the desired area */
	    if (from_room->vnum < parea->min_vnum
		||   from_room->vnum > parea->max_vnum)
		continue;

	    /* Room's good, let's check all the directions for exits */
	    for (door = 0; door < MAX_DIR; door++)
	    {
		if((pexit = from_room->exit[door]) != NULL)
		{
		    to_room = pexit->u1.to_room;

		    /* Found an exit, does it lead to a different area? */
		    if (to_room != NULL
			&&  (to_room->vnum < parea->min_vnum
			     ||   to_room->vnum > parea->max_vnum))
		    {
			found = TRUE;
			sprintf(buf, "%s (%d) links %s to %s (%d)\n\r",
				parea->name, from_room->vnum, dir_name[door],
				to_room->area->name, to_room->vnum);

			/* File or buffer output? */
			if(fp == NULL)
			    send_to_char(buf, ch);
			else
			    fprintf(fp, buf);
		    }
		}
	    }
	}
    }

    /* Close up and clean up file stuff */
    if (fp)
	file_close(fp);

    /* Informative message telling you it's not externally linked */
    if (!found)
	send_to_char("No links to other areas found.\n\r", ch);

    return;
}

void do_check(CHAR_DATA *ch, char *argument)
{
    char buf[MAX_STRING_LENGTH];
    char arg[MAX_INPUT_LENGTH];
    BUFFER *buffer;
    CHAR_DATA *victim;
    int count = 1;

    one_argument(argument, arg);

    if (arg[0] == '\0'|| !str_prefix(arg, "stats"))
    {
	buffer = new_buf();
	LIST_FOREACH(victim, &char_list, link)
	{
	    if (IS_NPC(victim) || !can_see(ch, victim))
		continue;

	    if (victim->desc == NULL)
	    {
		sprintf(buf, "%3d) %s is linkdead.\n\r", count, victim->name);
		add_buf(buffer, buf);
		count++;
		continue;
	    }

	    if (victim->desc->connected >= CON_GET_NEW_RACE
		&& victim->desc->connected <= CON_PICK_WEAPON)
	    {
		sprintf(buf, "%3d) %s is being created.\n\r",
			count, victim->name);
		add_buf(buffer, buf);
		count++;
		continue;
	    }

	    if ((victim->desc->connected == CON_GET_OLD_PASSWORD
		 || victim->desc->connected >= CON_READ_IMOTD)
		&& get_trust(victim) <= get_trust(ch))
	    {
		sprintf(buf, "%3d) %s is connecting.\n\r",
			count, victim->name);
		add_buf(buffer, buf);
		count++;
		continue;
	    }

	    if (victim->desc->connected == CON_PLAYING)
	    {
		if (get_trust(victim) > get_trust(ch))
		    sprintf(buf, "%3d) %s.\n\r", count, victim->name);
		else
		{
		    sprintf(buf, "%3d) %s, Level %d connected since %ld hours (%ld total hours)\n\r",
			    count, victim->name, victim->level,
			    (long)(current_time - victim->logon) /3600,
			    (long)(victim->played + (current_time - victim->logon))/3600);
		    add_buf(buffer, buf);

		    if (arg[0]!='\0' && !str_prefix(arg, "stats"))
		    {
			sprintf(buf, "  %d HP %d Mana (%d %d %d %d %d) %ld golds %d Tr %d Pr %d Qpts.\n\r",
				victim->max_hit, victim->max_mana, victim->perm_stat[STAT_STR],
				victim->perm_stat[STAT_INT], victim->perm_stat[STAT_WIS],
				victim->perm_stat[STAT_DEX], victim->perm_stat[STAT_CON],
				victim->gold + victim->silver/100,
				victim->train, victim->practice, victim->pcdata->quest_curr);
			add_buf(buffer, buf);
		    }
		    count++;
		}
		continue;
	    }

	    sprintf(buf, "%3d) bug (oops)...please report to Sure: %s %d\n\r",
		    count, victim->name, victim->desc->connected);
	    add_buf(buffer, buf);
	    count++;
	}

	page_to_char(buf_string(buffer), ch);
	free_buf(buffer);
	return;
    }

    if (!str_prefix(arg, "eq"))
    {
	buffer = new_buf();
	LIST_FOREACH(victim, &char_list, link)
	{
	    if (IS_NPC(victim)
		|| !can_see(ch, victim)
		|| get_trust(victim) > get_trust(ch))
	    {
		continue;
	    }

	    sprintf(buf, "%3d) %-12s [%2d], %-4d items (weight %-5d) Hit:%-3d Dam:%-3d Save:%-3d AC:%-3d %-3d %-3d %-3d.\n\r",
		    count, victim->name, victim->level, victim->carry_number, victim->carry_weight,
		    victim->hitroll, victim->damroll, victim->saving_throw,
		    victim->armor[AC_PIERCE], victim->armor[AC_BASH],
		    victim->armor[AC_SLASH], victim->armor[AC_EXOTIC]);
	    add_buf(buffer, buf);
	    count++;
	}
	page_to_char(buf_string(buffer), ch);
	free_buf(buffer);
	return;
    }

    if (!str_prefix(arg, "snoop")) /* this part by jerome */
    {
	char bufsnoop[100];

	if (ch->level < MAX_LEVEL)
	{
	    send_to_char("You can't use this check option.\n\r", ch);
	    return;
	}
	buffer = new_buf();

	LIST_FOREACH(victim, &char_list, link)
	{
	    if (IS_NPC(victim)
		|| victim->desc == NULL
		|| victim->desc->connected != CON_PLAYING
		|| !can_see(ch, victim)
		|| get_trust(victim) > get_trust(ch))
	    {
		continue;
	    }

	    if (victim->desc->snoop_by != NULL)
		sprintf(bufsnoop, " %15s .", victim->desc->snoop_by->character->name);
	    else
		sprintf(bufsnoop, "     (none)      .");

	    sprintf(buf, "%3d %15s : %s \n\r", count, victim->name, bufsnoop);
	    add_buf(buffer, buf);
	    count++;
	}
	page_to_char(buf_string(buffer), ch);
	free_buf(buffer);
	return;
    }

    if (!str_prefix(arg, "mem"))
    {
	MEM_DATA *mem;
	CHAR_DATA *vch;
	bool found = FALSE;


	LIST_FOREACH(victim, &char_list, link)
	    if (victim->zone == ch->zone
		&& victim->memory
		&& IS_NPC(victim))
	    {
		buffer = new_buf();
		sprintf(buf, "Mobile: {g%s{x[%d]:\n\r",
			victim->name, victim->pIndexData->vnum);
		add_buf(buffer, buf);
		found = TRUE;

		for (mem = victim->memory; mem; mem = mem->next)
		{
		    sprintf(buf, "  Memory: {r%s{x\n\r",
			    (vch = pc_id_lookup(mem->id))
			    ? vch->name : "(âûøåë)");
		    add_buf(buffer, buf);
		}
		page_to_char(buf_string(buffer), ch);
		free_buf(buffer);
	    }

	if (!found)
	    send_to_char("Íå íàéäåíî.\n\r", ch);

	return;
    }


    send_to_char("Syntax:\n\r", ch);
    send_to_char("check      display info about players\n\r", ch);
    send_to_char("check stat display info and resume stats\n\r", ch);
    send_to_char("check eq   resume eq of all players\n\r", ch);
    send_to_char("check mem  check memory for mobs in the World\n\r", ch);
    send_to_char("Use the stat command in case of doubt about someone...\n\r", ch);
    return;
}

void do_doas (CHAR_DATA *ch, char *argument)
{
    CHAR_DATA *vic;
    DESCRIPTOR_DATA *orig;
    char arg[MAX_INPUT_LENGTH];

    argument=one_argument(argument, arg);

    if ((vic = get_char_world(ch, arg)) == NULL)
    {
	send_to_char("Òàêèõ íåò â ýòîì ìèðå.\n\r", ch);
	return;
    }

    if (IS_NPC(vic))
    {
	send_to_char("Òû íå ìîæåøü âûïîëíèòü ýòó êîìàíäó íà ìîáà.\n\r", ch);
	return;
    }

    if (get_trust(ch) <= get_trust(vic))
    {
	send_to_char("Íå ïîëó÷àåòñÿ.\n\r", ch);
	return;
    }

    if (is_name(argument, "quit êîíåö delete óäàëèòü"))
    {
	send_to_char("Òàêàÿ êîìàíäà íå ðàçðåøåíà.\n\r", ch);
	return;
    }

    orig = vic->desc;

    vic->desc = ch->desc;
    ch->desc = NULL;

    interpret (vic, argument);

    ch->desc = vic->desc;
    vic->desc = orig;
}

void do_security(CHAR_DATA *ch, char *argument)
{
    /*
     *  Gothar Security Manager
     *   November 16, 1997.
     */
    char arg1[MAX_INPUT_LENGTH];
    char arg2[MAX_INPUT_LENGTH];
    char buf[100];
    CHAR_DATA *victim;
    int value;

    argument = one_argument(argument, arg1);
    argument = one_argument(argument, arg2);
    if (arg1[0] == '\0' || arg2[0] == '\0')
    {
	send_to_char("Syntax:\n\r", ch);
	send_to_char("Security <char name> <value>\n\r", ch);
	send_to_char("Security value 0 >> Player\n\r", ch);
	send_to_char("Security value 1 >> Builder\n\r", ch);
	send_to_char("Security value 5 >> Immortal\n\r", ch);
	send_to_char("Security value 9 >> Implementor\n\r", ch);
	return;
    }
    if ((victim = get_char_world(ch, arg1)) == NULL)
    {
	send_to_char("Òàêèõ çäåñü íåò.\n\r", ch);
	return;
    }
    /* clear zones for mobs */
    victim->zone = NULL;
    value = is_number(arg2) ? atoi(arg2) : 0;
    /*
     *  Set security level
     */
    if (IS_NPC(victim))
    {
	send_to_char("Ìîáàì ñåêüþðèòè íå óñòàíàâëèâàåòñÿ!", ch);
	return;
    }

    /*  No security above your means
     *  can be altered.
     */
    if (victim->level >= ch->level)
    {
	/*
	 *  level check  => I have stuff
	 *   to keep lower level imms in check
	 */
	send_to_char("Íå ïîëó÷àåòñÿ!\n\r", ch);
	return;
    }
    if (value > ch->pcdata->security && ch->level < IMPLEMENTOR)
    {
	/*
	 *  security level check and to
	 *   see if not an IMPLEMENTOR
	 */
	sprintf(buf, "Òâîé óðîâåíü íåäîñòàòî÷åí äëÿ óñòàíîâêè %d-ãî óðîâíÿ ñåêðåòíîñòè.\n\r"
		, ch->pcdata->security);
	send_to_char(buf, ch);
	return;
    }
    else
    {
	/*
	 *   change the security and inform
	 *   the player of new status
	 */
	victim->pcdata->security = value;
	send_to_char("Òâîé óðîâåíü ñåêðåòíîñòè èçìåíåí.\n\r", victim);
	sprintf(buf, "You changed %s's security rating.\n\r", victim->name);
	send_to_char(buf, ch);
    }
    return;

}

void do_whowas(CHAR_DATA *ch, char *argument){
    char arg[MAX_INPUT_LENGTH], buf[MAX_STRING_LENGTH];
    DESCRIPTOR_DATA *d;
    CHAR_DATA *victim;

    one_argument(argument, arg);

    if (arg[0] == '\0'){
        send_to_char("You must provide a name.\n\r", ch);
        return;
    }

    if ((victim = get_char_world(NULL, arg)) != NULL && !IS_NPC(victim)){
        send_to_char("Ýòîò ïåðñîíàæ ñåé÷àñ â èãðå.\n\r", ch);
        return;
    }

    d = new_descriptor();

    if (!load_char_obj(d, arg, FALSE, FALSE)){
        send_to_char("Òàêèõ íåò â ýòîì ìèðå.\n\r", ch);
        free_descriptor(d);
        return;
    }

    victim = d->character;

    check_light_status(victim);

    if (get_trust(victim) > get_trust(ch)){
        send_to_char("Òàêèõ íåò â ýòîì ìèðå.\n\r", ch);
        goto WHOWAS_EXIT;
    }

    sprintf(buf, "{YName:{m %s{x\n\r", victim->name);
    send_to_char(buf, ch);

    if (victim->level >= 52){
        switch (victim->sex){
            default:
            case SEX_MALE:   	strcpy(buf, "{YIs a GOD.{x\n\r"); break;
            case SEX_FEMALE: 	strcpy(buf, "{YIs a GODDES.{x\n\r"); break;
            case SEX_NEUTRAL: 	strcpy(buf, "{YIs a GODDIT.{x\n\r"); break;
        }
    } else {
        sprintf(buf, "{YLevel:{m %d{x\n\r", victim->level);
    }


    send_to_char(buf, ch);

    sprintf(buf, "{YRace:{m %s{Y Class:{m %s{x\n\r\n\r", pc_race_table[victim->race].name, class_table[victim->classid].name);
    send_to_char(buf, ch);

    sprintf(buf, "{YLast seen on:{m %s{x", ctime(&victim->pcdata->lastlogof));
    send_to_char(buf, ch);

    if (ch->level >= 52)
    {
	sprintf(buf, "{YHost:{m %s{x\n\r", victim->pcdata->lasthost);
	send_to_char(buf, ch);

	sprintf(buf, "{YIP:  {m %s{x\n\r", victim->pcdata->lastip);
	send_to_char(buf, ch);

	sprintf(buf, "\n\r{YPassword:{m %s{x\n\r", victim->pcdata->pwd);
	send_to_char(buf, ch);

	sprintf(buf, "\n\r{YE-mail:{m %s{x\n\r", IS_NULLSTR(victim->pcdata->email) ? "(unknown)" : victim->pcdata->email);
	send_to_char(buf, ch);

	if (IS_SET(victim->act, PLR_FREEZE))
	{
	    sprintf(buf, "\n\rFreezed by %s up to: %s", victim->pcdata->freeze_who, victim->pcdata->freeze == 0 ? "unknown date.\n\r" : ctime(&victim->pcdata->freeze));
	    send_to_char(buf, ch);
	}

	if (IS_SET(victim->act, PLR_DENY))
	    send_to_char("\n\rChar was denied.\n\r", ch);
    }

WHOWAS_EXIT:
    extract_char_for_quit(victim, FALSE);
    free_descriptor(d);

    return;
}

void do_pass(CHAR_DATA *ch, char *argument)
{
    CHAR_DATA *victim;
    char arg[MAX_INPUT_LENGTH];
    char bfr[MAX_STRING_LENGTH];

    if (argument[0] == '\0')
    {
	send_to_char("Ñèíòàêñèñ: èììïàðîëü <èìÿ> [<èìÿ>] [<èìÿ>] .....\n\r", ch);
	return;
    }

    for (argument = one_argument(argument, arg); ; argument = one_argument(argument, arg))
    {
	if (arg[0] == '\0')
	    break;

	if ((victim=get_char_world(ch, arg)) == NULL || IS_NPC(victim))
	    sprintf(bfr, "%-12s: {Dòàêèõ íåò â ýòîì ìèðå.{x\n\r", capitalize(arg));
	else if (ch != victim && get_trust(victim) > LEVEL_HERO)
	    sprintf(bfr, "%-12s: {Díå ïîëó÷àåòñÿ óçíàòü ïàðîëü.{x\n\r", victim->name);
	else
	    sprintf(bfr, "%-12s: %s\n\r", victim->name, victim->pcdata->pwd);

	send_to_char(bfr, ch);
    }
}

void do_fileident(CHAR_DATA *ch, char *argument)
{
    /*    char buf[MAX_STRING_LENGTH]; */
    char start[20];
    char end[20];
    char file[20];

    int svnum;
    int evnum;
    int vnum;

    FILE *fp;
    AFFECT_DATA *paf;

    int nMatch=0;
    extern int top_obj_index;
    OBJ_INDEX_DATA *pObjIndex;

    file[0] = '\0';

    argument = one_argument(argument, start);
    argument = one_argument(argument, end);
    argument = one_argument(argument, file);

    svnum = atoi(start);
    evnum = atoi(end);

    if (file[0] == '\0')
    {
	send_to_char("Syntax : fileident [begin vnum] [end vnum] [file name]\n",
		     ch);
	return;
    }

    if ((fp = open_report_file(ch, file)) == NULL)
	_perror(file);
    else
    {
	for (vnum = svnum; (nMatch < top_obj_index) && (vnum < evnum); vnum++)
	{
	    if ((pObjIndex = get_obj_index(vnum)) != NULL)
	    {
		OBJ_DATA *obj = NULL;

		nMatch++;
		fprintf(fp, "Name(s): %s\n", pObjIndex->name);

		fprintf(fp, "Vnum: %d  Format: %s  Type: %s  Resets: %d\n",
			pObjIndex->vnum, pObjIndex->new_format ? "new" : "old",
			item_name(pObjIndex->item_type, 0),
			pObjIndex->reset_num);

		fprintf(fp, "Short description: %s\nLong description: %s\n",
			pObjIndex->short_descr, pObjIndex->description);

		fprintf(fp, "Wear bits: %s\n",
			flag_string(wear_flags, pObjIndex->wear_flags, FALSE));
		fprintf(fp, "Extra bits: %s\n",
			flag_string(extra_flags,
				    pObjIndex->extra_flags, FALSE));

		fprintf(fp, "Weight : %d\n", pObjIndex->weight);

		fprintf(fp, "Values: %d %d %d %d %d\n",
			pObjIndex->value[0],
			pObjIndex->value[1],
			pObjIndex->value[2],
			pObjIndex->value[3],
			pObjIndex->value[4]);

		fprintf(fp, "\nLevel: %d  Cost: %d  Condition: %d\n",
			pObjIndex->level,
			pObjIndex->cost,
			pObjIndex->condition);

		switch (pObjIndex->item_type)
		{
		case ITEM_SCROLL:
		case ITEM_POTION:
		case ITEM_PILL:
		    fprintf(fp, "Level %d spell(s) of:", pObjIndex->value[0]);

		    if (pObjIndex->value[1] >= 0
			&& pObjIndex->value[1] < max_skills)
		    {
			fprintf(fp, " '%s'",
				skill_table[pObjIndex->value[1]].name);
		    }

		    if (pObjIndex->value[2] >= 0
			&& pObjIndex->value[2] < max_skills)
		    {
			fprintf(fp, " '%s'",
				skill_table[pObjIndex->value[2]].name);
		    }

		    if (pObjIndex->value[3] >= 0 &&
			pObjIndex->value[3] < max_skills)
		    {
			fprintf(fp, " '%s'",
				skill_table[pObjIndex->value[3]].name);
		    }

		    if (pObjIndex->value[4] >= 0
			&& pObjIndex->value[4] < max_skills)
		    {
			fprintf(fp, " '%s'",
				skill_table[pObjIndex->value[4]].name);
		    }

		    fprintf(fp, "\n");
		    break;

		case ITEM_WAND:
		case ITEM_STAFF:
		    fprintf(fp, "Has %d(%d) charges of level %d",
			    pObjIndex->value[1],
			    pObjIndex->value[2],
			    pObjIndex->value[0]);

		    if (pObjIndex->value[3] >= 0
			&& pObjIndex->value[3] < max_skills)
		    {
			fprintf(fp, " '%s'\n",
				skill_table[pObjIndex->value[3]].name);
		    }

		    break;

		case ITEM_DRINK_CON:
		    fprintf(fp, "It holds %s-colored %s.\n",
			    liq_table[pObjIndex->value[2]].liq_color,
			    liq_table[pObjIndex->value[2]].liq_name);
		    break;

		case ITEM_WEAPON:
		    fprintf(fp, "Weapon type is: %s   ",
			    flag_string(weapon_class,
					pObjIndex->value[0], FALSE));
		    if (pObjIndex->new_format)
		    {
			fprintf(fp, "Damage is %dd%d (average %d)\n",
				pObjIndex->value[1], pObjIndex->value[2],
				(1 + pObjIndex->value[2])
				* pObjIndex->value[1] / 2);
		    }
		    else
		    {
			fprintf(fp, "Damage is %d to %d (average %d)\n",
				pObjIndex->value[1], pObjIndex->value[2],
				(pObjIndex->value[1]+pObjIndex->value[2]) / 2);
		    }

		    fprintf(fp, "Damage noun is %s.\n",
			    (pObjIndex->value[3] > 0
			     && pObjIndex->value[3] < MAX_DAMAGE_MESSAGE)
			    ? attack_table[pObjIndex->value[3]].noun
			    : "undefined");

		    if (pObjIndex->value[4])  /* weapon flags */
		    {
			fprintf(fp, "Weapons flags: %s\n",
				flag_string(weapon_type2,
					    pObjIndex->value[4], FALSE));
		    }

		    break;

		case ITEM_ARMOR:
		    fprintf(fp,	"Armor class is %d pierce, %d bash, "
			    "%d slash, and %d other\n",
			    pObjIndex->value[0], pObjIndex->value[1],
			    pObjIndex->value[2], pObjIndex->value[3]);

		    break;

		case ITEM_CONTAINER:
		    fprintf(fp, "Weight capacity: %d Max weight: %d "
			    "flags: %s\n",
			    pObjIndex->value[0], pObjIndex->value[3],
			    flag_string(container_flags,
					pObjIndex->value[1], FALSE));

		    if (pObjIndex->value[4] != 100)
		    {
			fprintf(fp, "Weight multiplier: %d%%\n",
				pObjIndex->value[4]);
		    }
		    break;

		case ITEM_CHEST:
		    fprintf(fp, "Weight capacity: %d Max weight: %d "
			    "flags: %s\n",
			    pObjIndex->value[0], pObjIndex->value[3],
			    flag_string(chest_flags,
					pObjIndex->value[1], FALSE));

		    if (pObjIndex->value[4] != 100)
		    {
			fprintf(fp, "Weight multiplier: %d%%\n",
				pObjIndex->value[4]);
		    }
		    break;


		default:
		    break;


		}


		for (paf = pObjIndex->affected; paf != NULL; paf = paf->next)
		{
		    fprintf(fp, "Affects %s by %d, level %d\n",
			    flag_string(apply_flags, paf->location, FALSE),
			    paf->modifier, paf->level);

		    if (paf->bitvector)
		    {
			switch(paf->where)
			{
			case TO_AFFECTS:
			    fprintf(fp, "Adds %s affect.\n",
				    flag_string(affect_flags,
						paf->bitvector, FALSE));
			    break;
			case TO_WEAPON:
			    fprintf(fp, "Adds %s weapon flags.\n",
				    flag_string(weapon_type2,
						paf->bitvector, FALSE));
			    break;
			case TO_OBJECT:
			    fprintf(fp, "Adds %s object flag.\n",
				    flag_string(extra_flags,
						paf->bitvector, FALSE));
			    break;
			case TO_RESIST:
			    fprintf(fp, "Modifies resistance to %s "
				    "by %d%%.\n\r",
				    flag_string(res_flags,
						paf->bitvector, FALSE),
				    paf->modifier);
			    break;
			default:
			    fprintf(fp, "Unknown bit %d: %ld\n\r",
				    paf->where, paf->bitvector);
			    break;
			}
		    }
		}

		if ((obj = get_obj_world(ch, pObjIndex->name)) == NULL)
		{
		    fprintf(fp, "\nNot loaded.\n");
		}
		else
		{
		    fprintf(fp, "\nIn room: %d  In object: %s  "
			    "Carried by: %s  Wear_loc: %d\n",
			    obj->in_room ? obj->in_room->vnum : 0,
			    obj->in_obj  ? obj->in_obj->short_descr : "(none)",
			    obj->carried_by ? obj->carried_by->name : "(none)",
			    obj->wear_loc);
		}


		fprintf(fp, "\n---------------------------------------\n\n");
	    }

	}
    }

    file_close(fp);
}

void do_noreply(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH];
    CHAR_DATA *victim;

    one_argument(argument, arg);

    if (arg[0] == '\0')
    {
	send_to_char("Êîìó òû õîòåë çàïðåòèòü îòâå÷àòü òåáå?\n\r", ch);
	return;
    }

    if ((victim = get_char_world(ch, arg)) == NULL)
    {
	send_to_char("Òàêèõ çäåñü íåò.\n\r", ch);
	return;
    }

    if (victim->reply != ch)
    {
	send_to_char("Ýòîò ïåðñîíàæ óæå è òàê îòâå÷àåò íå òåáå.\n\r", ch);
	return;
    }

    victim->reply = NULL;
    send_to_char("Ok.\n\r", ch);
}

void do_immdamage(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH];
    CHAR_DATA *victim;

    one_argument(argument, arg);

    if (arg[0] == '\0')
    {
	send_to_char("Ñîáñòâåííî êîìó?\n\r", ch);
	return;
    }

    if ((victim = get_char_world(ch, arg)) == NULL || IS_NPC(victim))
    {
	send_to_char("Òàêèõ çäåñü íåò.\n\r", ch);
	return;
    }

    if (IS_SET(victim->act, PLR_SHOW_DAMAGE))
    {
	REMOVE_BIT(victim->act, PLR_SHOW_DAMAGE);
	send_to_char("Òåïåðü òû íå áóäåøü âèäåòü ïîâðåæäåíèé.\n\r", victim);
	if (victim != ch)
	    act("Òåïåðü $N íå áóäåò âèäåòü ïîâðåæäåíèÿ.",
		ch, NULL, victim, TO_CHAR);
    }
    else
    {
	SET_BIT(victim->act, PLR_SHOW_DAMAGE);
	send_to_char("Òåïåðü òû áóäåøü âèäåòü ïîâðåæäåíèÿ.\n\r", victim);
	if (victim != ch)
	    act("Òåïåðü $N áóäåò âèäåòü ïîâðåæäåíèÿ.",
		ch, NULL, victim, TO_CHAR);
    }
}

void tick_update(void);

void do_tick(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH];
    int i, count;

    one_argument(argument, arg);

    if (arg[0] == '\0')
	count = 1;
    else if (!is_number(arg))
    {
	send_to_char("Àðãóìåíò äîëæåí áûòü öèôðîâûì!\n\r", ch);
	return;
    }
    else
	count = atoi(arg);

    if (count < 1 || count > 100)
    {
	send_to_char("Áóäü áëàãîðàçóìåí!\n\r", ch);
	return;
    }
    send_to_char("Òû çàñòàâëÿåøü âðåìÿ ïîéòè íåìíîãî âïåðåä...\n\r", ch);

    for (i = 0; i < count; i++)
    {
	wiznet("FORCED TICK!", NULL, NULL, WIZ_TICKS, 0, 0);
	tick_update();
    }
}

void do_nocast(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH];
    CHAR_DATA *victim;
    AFFECT_DATA af;

    argument = one_argument(argument, arg);

    if (arg[0] == '\0')
    {
	send_to_char("Ó êîãî òû õî÷åøü îòîáðàòü ïðèâåëåãèþ êîëäîâñòâà?\n\r",
		     ch);
	return;
    }

    if ((victim = get_char_world(ch, arg))==NULL)
    {
	send_to_char("Òàêèõ çäåñü íåò.\n\r", ch);
	return;
    }

    if (get_trust(ch) < get_trust(victim))
    {
	send_to_char("Íå ïîëó÷àåòñÿ.\n\r", ch);
	return;
    }

    if (IS_NPC(victim))
    {
	send_to_char("Not on NPC's!\n\r", ch);
	return;
    }

    if (IS_SET(victim->act, PLR_NOCAST))
    {
	REMOVE_BIT(victim->act, PLR_NOCAST);
	affect_strip(victim, gsn_nocast);
	act("Òû âîçâðàùàåøü $N2 âîçìîæíîñòü ïîëüçîâàòüñÿ ìàãèåé.",
	    ch, NULL, victim, TO_CHAR);
	act("{R$n ðàçðåøàåò òåáå ïîëüçîâàòüñÿ ìàãèåé.{x",
	    ch, NULL, victim, TO_VICT);
	return;
    }

    one_argument(argument, arg);

    if (arg[0] == '\0' || !is_number(arg))
	af.duration = -1;
    else
	af.duration = atoi(arg);

    af.where = TO_PLR;
    af.level = ch->level;
    af.type = gsn_nocast;
    af.location = APPLY_NONE;
    af.bitvector = PLR_NOCAST;
    af.modifier = 0;
    af.caster_id = get_id(ch);
    affect_to_char(victim, &af);

    act("Òû îòáèðàåøü ó $N1 âîçìîæíîñòü êîëäîâàòü!",
	ch, NULL, victim, TO_CHAR);
    act("{R$n îòáèðàåò ó òåáÿ âîçìîæíîñòü êîëäîâàòü!{x",
	ch, NULL, victim, TO_VICT);
    return;
}

void do_gods_curse(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH];
    CHAR_DATA *victim;
    AFFECT_DATA af;

    argument = one_argument(argument, arg);

    if (arg[0] == '\0')
    {
	send_to_char("Êîãî òû õî÷åøü ïðîêëÿñòü?\n\r", ch);
	return;
    }

    if ((victim = get_char_world(ch, arg))==NULL)
    {
	send_to_char("Òàêèõ çäåñü íåò.\n\r", ch);
	return;
    }

    if (get_trust(ch) < get_trust(victim))
    {
	send_to_char("Íå ïîëó÷àåòñÿ.\n\r", ch);
	return;
    }

    if (IS_NPC(victim))
    {
		send_to_char("Not on NPC's!\n\r", ch);
		return;
    }

    one_argument(argument, arg);

    if (arg[0] == '\0' || !is_number(arg))
	{    
		if (is_affected(victim, gsn_gods_curse))
    	{
		    affect_strip(victim, gsn_gods_curse);
			send_to_char("Òû ïðîùàåøü.\n\r", ch);
			return;
		}
		else
		{
			af.duration = 0;
		}
	}
    else
		af.duration = atoi(arg);

    af.where = TO_AFFECTS;
    af.level = ch->level;
    af.type = gsn_gods_curse;
    af.location = APPLY_NONE;
    af.bitvector = 0;
    af.modifier = 0;
    af.caster_id = get_id(ch);
    affect_to_char(victim, &af);

    act("Òû ïðîêëèíàåøü $N3!",  ch, NULL, victim, TO_CHAR);
    act("{R$n ïðîêëèíàåò òåáÿ!{x",  ch, NULL, victim, TO_VICT);

    if (victim->in_room != NULL
	&& (IS_SET(victim->in_room->room_flags, ROOM_SAFE)
	    || IS_SET(victim->in_room->room_flags, ROOM_PRIVATE)
	    || IS_SET(victim->in_room->room_flags, ROOM_SOLITARY)))
    {
	ROOM_INDEX_DATA *pRoomIndex;

	while ((pRoomIndex = get_random_room(victim)) == NULL || pRoomIndex->clan != 0)
	    ;

	act("$n èñ÷åçàåò!", victim, NULL, NULL, TO_ROOM);

	if (!LIST_EMPTY(&pRoomIndex->people))
	    act("$n ïîÿâëÿåòñÿ çäåñü.", LIST_FIRST(&pRoomIndex->people), NULL, NULL, TO_ALL);

	char_from_room(victim);
	char_to_room(victim, pRoomIndex, TRUE);
	/*      do_function(victim, &do_look, "auto"); */
	check_trap(victim);
    }

    return;
}

void do_skillaffect(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH];
    CHAR_DATA *victim;
    AFFECT_DATA af;
    int sn, mod;

    argument = one_argument(argument, arg);

    if (arg[0] == '\0')
    {
	send_to_char("Ó êîãî òû õî÷åøü èçìåíèòü óìåíèÿ?\n\r", ch);
	return;
    }

    if ((victim = get_char_world(ch, arg)) == NULL)
    {
	send_to_char("Òàêèõ çäåñü íåò.\n\r", ch);
	return;
    }

    if (get_trust(ch) < get_trust(victim))
    {
	send_to_char("Íå ïîëó÷àåòñÿ.\n\r", ch);
	return;
    }

    if (IS_NPC(victim))
    {
	send_to_char("Not on NPC's!\n\r", ch);
	return;
    }

    argument = one_argument(argument, arg);

    if ((sn = skill_lookup(arg)) < 0)
    {
	send_to_char("Íåò òàêèõ óìåíèé!\n\r", ch);
	return;
    }

    argument = one_argument(argument, arg);

    if (arg[0] == '\0'
	|| !is_number(arg)
	|| (mod = atoi(arg)) < -100
	|| mod > 100)
    {
	send_to_char("Ìîäèôèêàòîð äîëæåí áûòü ÷èñëîâûì è "
		     "áûòü â ïðåäåëàõ îò -100 äî +100!\n\r", ch);
	return;
    }

    af.modifier = mod;

    argument = one_argument(argument, arg);

    if (arg[0] == '\0' || !is_number(arg))
    {
	send_to_char("Âðåìÿ äîëæíî áûòü ÷èñëîâûì. Åñëè õî÷åøü "
		     "ïîñòîÿííûé àôôåêò, ââåäè -1.\n\r", ch);
	return;
    }

    af.duration = atoi(arg);

    af.where = TO_AFFECTS;
    af.level = ch->level;
    af.type = sn;
    af.location = APPLY_SKILL;
    af.bitvector = AFF_GODS_SKILL;
    af.caster_id = get_id(ch);
    affect_to_char(victim, &af);

    act("Òû èçìåíÿåøü ó $N1 óìåíèå '$t'!",
	ch, skill_table[sn].rname, victim, TO_CHAR);
    act("{R$n èçìåíÿåò òâîè ñïîñîáíîñòè â óìåíèè '$t'!{x",
	ch, skill_table[sn].rname, victim, TO_VICT);
    return;
}

void do_deathtraps(CHAR_DATA *ch, char *argument)
{
    ROOM_INDEX_DATA *room;
    int iHash;
    BUFFER *output;
    bool found = FALSE;
    char buf[MAX_STRING_LENGTH];

    output = new_buf();

    for (iHash = 0; iHash < MAX_KEY_HASH; iHash++)
	for (room = room_index_hash[iHash]; room != NULL; room = room->next)
	{
	    if (IS_SET(room->room_flags, ROOM_KILL))
	    {
		sprintf(buf, "%5d  %-30s   %-30s\n\r",
			room->vnum, room->name, room->area->name);
		add_buf(output, buf);
		found = TRUE;
	    }

	    if (IS_SET(room->room_flags, ROOM_NOFLY_DT))
	    {
		sprintf(buf, "%5d  %-30s   %-30s   (äëÿ íåëåòàþùèõ)\n\r",
			room->vnum, room->name, room->area->name);
		add_buf(output, buf);
		found = TRUE;
	    }
	}

    if (found)
    {
	send_to_char("Âíóìû ëîâóøåê:\n\r\n\r", ch);
	page_to_char(buf_string(output), ch);
    }
    else
	send_to_char("Ëîâóøåê íå íàéäåíî.\n\r", ch);

    free_buf(output);
}

void do_rwhere(CHAR_DATA *ch, char *argument)
{
    AREA_DATA *area = NULL;
    MOB_INDEX_DATA *mob = NULL;
    OBJ_INDEX_DATA *obj = NULL;
    ROOM_INDEX_DATA *room;
    RESET_DATA *reset;
    BUFFER *buf;
    char arg[MAX_STRING_LENGTH];
    int vnum;
    int cvnum;
    int i;
    bool found = FALSE;

    argument = one_argument(argument, arg);
    if (IS_NULLSTR(arg) || IS_NULLSTR(argument) || !is_number(argument))
	goto EX_RWHERE_SYN;
    
    vnum = atoi(argument);
    
    if (!str_cmp(arg, "area") || !str_cmp(arg, "çîíà"))
    {
	if ((area = get_area_data(vnum)) == NULL)
	{
	    send_to_char("Can't find area.\n\r", ch);
	    return;
	}
    }
    else if (!str_cmp(arg, "mob") || !str_cmp(arg, "ìîá"))
    {
	if ((mob = get_mob_index(vnum)) == NULL)
	{
	    send_to_char("Can't find mob.\n\r", ch);
	    return;
	}
    }
    else if (!str_cmp(arg, "obj") || !str_prefix(arg, "îáúåêò"))
    {
	if ((obj = get_obj_index(vnum)) == NULL)
	{
	    send_to_char("Can't find obj.\n\r", ch);
	    return;
	}
    }
    else
	goto EX_RWHERE_SYN;

    buf = new_buf();
    
    for (i = 0; i < MAX_KEY_HASH; i++)
    {
	for (room = room_index_hash[i]; room; room = room->next)
	{
	    for (reset = room->reset_first; reset; reset = reset->next)
	    {
		cvnum = reset->arg1;
		switch (reset->command)
		{
		case 'M':
		case 'H':
		    if (mob != NULL && mob->vnum == cvnum)
		    {
			printf_to_buffer(buf, "Room %d\n\r", room->vnum);
			found = TRUE;
		    }
			
		    if (!area)
			break;
		    
		case 'O':
		case 'P':
		case 'G':
		case 'E':
		case 'T':
		case 'C':
		case 'X':
		    if ((obj != NULL && obj->vnum == cvnum)
			|| (area != NULL
			    && area->min_vnum <= cvnum && cvnum <= area->max_vnum
			    && room->area != area))
		    {
			printf_to_buffer(buf, "Room %d\n\r", room->vnum);
			found = TRUE;
		    }
		    break;
		    
		default:
		    break;
		}
	    }
	}
    }

    if (found == TRUE)
	page_to_char(buf_string(buf), ch);
    else
	send_to_char("None found.\n\r", ch);

    free_buf(buf);
    return;

EX_RWHERE_SYN:
    send_to_char("Syntax: rwhere area <area vnum>\n\r"
		 "        rwhere mob  <mob vnum>\n\r"
		 "        rwhere obj  <obj vnum>\n\r",
		 ch);
    return;
}

void do_resetlist(CHAR_DATA *ch, char *argument)
{
    AREA_DATA		*pArea;
    ROOM_INDEX_DATA	*room;
    RESET_DATA		*pReset;
    MOB_INDEX_DATA *pMob = NULL;
    MOB_INDEX_DATA *pHorseman = NULL;
    OBJ_INDEX_DATA *pObj;
    OBJ_INDEX_DATA *pInObj;
    ROOM_INDEX_DATA *pRoom;
    int			i;
    BUFFER		*b;

    pArea = ch->in_room->area;

    b = new_buf();

    add_buf(b,
	    "Room  What  Vnum  Level Description    Where           Vnum  Mx Mn Name"
	    "\n\r"
	    "===== ===== ===== ===== ============== =============== ===== == == ================="
	    "\n\r");

    for (i = 0; i < MAX_KEY_HASH; i++)
    {
	for (room = room_index_hash[i]; room; room = room->next)
	{
	    if (room->area == pArea)
	    {
		for (pReset = room->reset_first;
		     pReset != NULL;
		     pReset = pReset->next)
		{

		    switch (pReset->command)
		    {
		    case 'M':
			pMob = get_mob_index(pReset->arg1);
			if (pMob == NULL)
			    break;

			pRoom = get_room_index(pReset->arg3);
			if (pRoom == NULL)
			    break;

			printf_to_buffer(b, "%5d %5s %5d %5d %s %-15.15s %5d "
					 "%2d %2d %-17.17s\n\r",
					 room->vnum, "Mob", pReset->arg1, pMob->level,
					 str_color_fmt(pMob->short_descr, 14), "in room", pReset->arg3,
					 pReset->arg2, pReset->arg4, pRoom->name);

			break;

		    case 'H':
			pHorseman = get_mob_index(pReset->arg1);
			if (pMob == NULL || pHorseman == NULL)
			    break;

			printf_to_buffer(b, "%5d %5s %5d %5d %s %-15.15s %5d "
					 "%2d %2d %-17.17s\n\r",
					 room->vnum, "Rider", pReset->arg1, pHorseman->level,
					 str_color_fmt(pHorseman->short_descr, 14), "on horse",
					 pMob->vnum, pReset->arg2, pReset->arg4,
					 pMob->short_descr);

			break;

		    case 'O':
			pObj = get_obj_index(pReset->arg1);
			if (pObj == NULL)
			    break;

			pRoom = get_room_index(pReset->arg3);
			if (pRoom == NULL)
			    break;

			printf_to_buffer(b, "%5d %5s %5d %5d %s %-15.15s %5d "
					 "%2d %2d %-17.17s\n\r",
					 room->vnum, "Obj", pReset->arg1, pObj->level,
					 str_color_fmt(pObj->short_descr, 14),
					 "in room", pReset->arg3,
					 pReset->arg2, pReset->arg4, pRoom->name);

			break;

		    case 'P':
			pObj = get_obj_index(pReset->arg1);
			if (pObj == NULL)
			    break;

			pInObj = get_obj_index(pReset->arg3);
			if (pInObj == NULL)
			    break;

			printf_to_buffer(b, "%5d %5s %5d %5d %s %-15.15s %5d "
					 "%2d %2d %-17.17s\n\r",
					 room->vnum, "Obj", pReset->arg1, pObj->level,
					 str_color_fmt(pObj->short_descr, 14),
					 "inside", pReset->arg3,
					 pReset->arg2, pReset->arg4,
					 pInObj->short_descr);

			break;

		    case 'G':
		    case 'E':
			pObj = get_obj_index(pReset->arg1);
			if (pObj == NULL)
			    break;

			if (pMob == NULL)
			    break;

			printf_to_buffer(b, "%5d %5s %5d %5d %s %-15.15s %5d "
					 "%2d %2d %-17.17s\n\r",
					 room->vnum, "Obj", pReset->arg1, pObj->level,
					 str_color_fmt(pObj->short_descr, 14),
					 (pReset->command == 'G')
					 ? flag_string(wear_loc_strings, WEAR_NONE, FALSE)
					 : flag_string(wear_loc_strings, pReset->arg3, FALSE),
					 pMob->vnum,
					 pReset->arg2, pReset->arg4, pMob->short_descr);

			break;

		    case 'T':
		    case 'C':
		    case 'X':
			pObj = get_obj_index(pReset->arg1);
			if (pObj == NULL)
			    break;

			printf_to_buffer(b, "%5d %5s %5d %5d %s %-15.15s %5d "
					 "      %-17.17s\n\r",
					 room->vnum, "Trap", pReset->arg1, pObj->level,
					 str_color_fmt(pObj->short_descr, 14),
					 pReset->command == 'T'
					 ? "in room"
					 : (pReset->command == 'C'
					    ? "on container"
					    : "on exit"),
					 pReset->arg2,
					 flag_string(trap_flags, pReset->arg3, FALSE));
			break;

		    default:
			break;
		    }
		}
	    }
	}
    }

    page_to_char(buf_string(b), ch);
    free_buf(b);
}

void show_skills(CHAR_DATA *ch, CHAR_DATA *victim, char *argument);
void show_spells(CHAR_DATA *ch, CHAR_DATA *victim, char *argument);

void do_immskills(CHAR_DATA *ch, char *argument)
{
    CHAR_DATA *victim;
    char arg[MAX_INPUT_LENGTH];

    argument = one_argument(argument, arg);

    if ((victim = get_char_world(ch, arg)) == NULL)
    {
	send_to_char("Òàêèõ â Ìèðå íåò.\n\r", ch);
	return;
    }

    show_skills(ch, victim, argument);
}

void do_immspells(CHAR_DATA *ch, char *argument)
{
    CHAR_DATA *victim;
    char arg[MAX_INPUT_LENGTH];

    argument = one_argument(argument, arg);

    if ((victim = get_char_world(ch, arg)) == NULL)
    {
	send_to_char("Òàêèõ â Ìèðå íåò.\n\r", ch);
	return;
    }

    show_spells(ch, victim, argument);
}

bool is_spec_granted(CHAR_DATA *ch, char *spec)
{
    if (!is_exact_name(spec, spec_grants))
	return FALSE;

    if (is_exact_name(spec, ch->pcdata->grants))
	return TRUE;

    return FALSE;
}

static void
remove_name(char *buf, char *word)
{
    char   *s = buf;
    size_t wordsiz = strlen(word);

    while ((s = strstr(s, word)) != NULL)
    {
	if (*(s + wordsiz) == ' '
	    || *(s + wordsiz) == '\0')
	{
	    break;
	}

	s++;
    }

    if (s == NULL)
	return;

    if (s != buf)
    {
	*s = '\0';
	/* Same size, use of strcat() is safe */
	strcat(buf, *(s + wordsiz) == '\0' ? s + wordsiz : s + wordsiz + 1);
    }
    else
	strcpy(buf, *(s + wordsiz) == '\0' ? s + wordsiz : s + wordsiz + 1);

    if (buf[strlen(buf) - 1] == ' ')
	buf[strlen(buf) - 1] = 0;
}

void do_grant(CHAR_DATA *ch, char *argument)
{
    CHAR_DATA *victim;
    char buf[MAX_STRING_LENGTH];
    char grants[2*MAX_STRING_LENGTH];
    char revoked[2*MAX_STRING_LENGTH];
    char granted[2*MAX_STRING_LENGTH];
    char spec_granted[2 * MAX_STRING_LENGTH];
    char spec_revoked[2 * MAX_STRING_LENGTH];

    argument = one_argument(argument, buf);

    if (buf[0] == '\0')
    {
	send_to_char("Ó êîãî òû õîòåë ïîñìîòðåòü äîâåðåííûå êîìàíäû?\n\r", ch);
	return;
    }

    if ((victim = get_char_world(ch, buf)) == NULL)
    {
	send_to_char("Òàêèõ â Ìèðå íåò.\n\r", ch);
	return;
    }

    if (IS_NPC(victim))
    {
	send_to_char("Íó íå ìîáó æå?!\n\r", ch);
	return;
    }

    if (get_trust(ch) < get_trust(victim)
	&& ch != victim)
    {
	send_to_char("Íå ñîâåòóþ... Ñúåäÿò!\n\r", ch);
	return;
    }

    if (argument[0] == '\0')
    {
	act("Êîìàíäû, äîâåðåííûå $N2:", ch, NULL, victim, TO_CHAR);
	send_to_char(victim->pcdata->grants, ch);
	send_to_char("\n\r", ch);

	return;
    }


    smash_tilde(argument);

    if (!str_cmp(argument, "all"))
    {
	grant_all(victim);
	send_to_char("Ok.\n\r", ch);
	send_to_char("{RÒåáå äàí äîñòóï êî âñåì êîìàíäàì.\n\r{x", victim);
	return;
    }

    if (!str_cmp(argument, "none"))
    {
	free_string(victim->pcdata->grants);
	victim->pcdata->grants = str_dup("");
	send_to_char("Ok.\n\r", ch);
	send_to_char("{RÓ òåáÿ îòîáðàí äîñòóï êî âñåì êîìàíäàì.\n\r{x", victim);
	return;
    }

    if (is_number(argument))
    {
	int level;

	level = atoi(argument);
	if (level <= LEVEL_HERO || level > MAX_LEVEL)
	{
	    send_to_char("À ñìûñë?\n\r", ch);
	    return;
	}
	grant_level(victim, level);
	send_to_char("Ok.\n\r", ch);
	printf_to_char("{RÒåáå äàí äîñòóï êî âñåì êîìàíäàì óðîâíÿ %d.\n\r{x",
		       victim, level);
	return;
    }

    grants[0] = '\0';
    revoked[0] = '\0';
    granted[0] = '\0';
    spec_granted[0] = '\0';
    spec_revoked[0] = '\0';
    strlcpy(grants, victim->pcdata->grants, sizeof(grants));

    if (!IS_NULLSTR(victim->pcdata->grants))
	free_string(victim->pcdata->grants);

    while (argument[0] != '\0')
    {
	argument = one_argument(argument, buf);

	if (is_granted(victim, buf))
	{
	    remove_name(grants, buf);
	    if (revoked[0] != '\0')
		strlcat(revoked, " ", sizeof(revoked));
	    strlcat(revoked, buf, sizeof(revoked));
	}
	else if (is_immcmd(buf))
	{
	    if (grants[0] != '\0')
		strlcat(grants, " ", sizeof(grants));
	    strlcat(grants, buf, sizeof(grants));
	    if (granted[0] != '\0')
		strlcat(granted, " ", sizeof(granted));
	    strlcat(granted, buf, sizeof(granted));
	}
	else if (is_spec_granted(victim, buf))
	{
	    remove_name(grants, buf);

	    if (spec_revoked[0] != '\0')
		strlcat(spec_revoked, " ", sizeof(spec_revoked));

	    strlcat(spec_revoked, buf, sizeof(spec_revoked));
	}
	else
	{
	    if (is_exact_name(buf, spec_grants))
	    {
		if (grants[0] != '\0')
		    strlcat(grants, " ", sizeof(grants));
		strlcat(grants, buf, sizeof(grants));

		if (spec_granted[0] != '\0')
		    strlcat(spec_granted, " ", sizeof(spec_granted));
		strlcat(spec_granted, buf, sizeof(spec_granted));
	    }
	}
    }

    victim->pcdata->grants = str_dup(grants);

    if (revoked[0] != '\0')
    {
	printf_to_char("{RÓ òåáÿ îòîáðàëè äîñòóï ê ñëåäóþùèì êîìàíäàì:{x\n\r  %s\n\r",
		       victim, revoked);
    }
    if (granted[0] != '\0')
    {
	printf_to_char("{RÒåáå äàëè äîñòóï ê ñëåäóþùèì êîìàíäàì:{x\n\r  %s\n\r",
		       victim, granted);
    }
    if (spec_revoked[0] != '\0')
    {
	printf_to_char("{RÓ òåáÿ îòîáðàëè äîñòóï ê ñëåäóþùèì ñïåö. âîçìîæíîñòÿì:{x\n\r  %s\n\r",
		       victim, spec_revoked);
    }
    if (spec_granted[0] != '\0')
    {
	printf_to_char("{RÒåáå äàëè äîñòóï ê ñëåäóþùèì ñïåö. âîçìîæíîñòÿì:{x\n\r  %s\n\r",
		       victim, spec_granted);
    }

    send_to_char("Ok.\n\r", ch);
}

void do_immprompt(CHAR_DATA *ch, char *argument)
{
    CHAR_DATA *victim;
    char arg[MAX_INPUT_LENGTH], bfr[MSL];

    if (argument[0] == '\0')
    {
	send_to_char("Ñèíòàêñèñ: èììïðîìïò <èìÿ> [<èìÿ>] [<èìÿ>] .....\n\r", ch);
	return;
    }

    for (argument = one_argument(argument, arg); arg[0] != '\0'; argument = one_argument(argument, arg))
    {
	if ((victim=get_char_world(ch, arg)) == NULL || IS_NPC(victim))
	    sprintf(bfr, "%-12s: {Dòàêèõ íåò â ýòîì ìèðå.{x\n\r", capitalize(arg));
	else if (ch != victim && get_trust(victim) > LEVEL_HERO)
	    sprintf(bfr, "%-12s: {Díå ïîëó÷àåòñÿ óçíàòü ïðîìïò.{x\n\r", victim->name);
	else
	    sprintf(bfr, "%-12s: %s\n\r", victim->name, victim->prompt);

	send_to_char(bfr, ch);
    }
}

void do_pload(CHAR_DATA *ch, char *argument)
{
    DESCRIPTOR_DATA d;
    bool isChar = FALSE;
    char name[MAX_INPUT_LENGTH];
    char buf[MAX_STRING_LENGTH];
    CHAR_DATA *wch;

    if (argument[0] == '\0')
    {
	send_to_char("Çàãðóçèòü êîãî?\n\r", ch);
	return;
    }

    argument[0] = UPPER(argument[0]);
    argument = one_argument(argument, name);

    /* Íå áóäåì çàãðóæàòü âòîðóþ êîïèþ èãðîêà, åñëè îí óæå èãðàåò! */
    if (get_char_world(NULL, name) != NULL)
    {
	send_to_char("Ýòà ïåðñîíà óæå èãðàåò!\n\r", ch);
	return;
    }

    d.original = NULL;
    d.ip = "127.0.0.1";

    isChar = load_char_obj(&d, name, TRUE, TRUE); /* ôàéë èãðîêà ñóùåñòâóåò? */

    if (!isChar)
    {
	send_to_char("Òàêàÿ ïåðñîíà íå íàéäåíà.\n\r", ch);
	return;
    }

    wch = d.character;

    wch->desc     = NULL;
    LIST_INSERT_HEAD(&char_list, wch, link);

    d.connected           = CON_PLAYING;
    reset_char(wch);


    /* Ïåðåìåùàåì èãðîêà ê èììó */
    if (wch->in_room != NULL)
    {
	wch->was_in_room = wch->in_room;
	char_from_room(wch);
	char_to_room(wch, ch->in_room, FALSE);
    }	

    send_to_char("Îê.\n\r", ch);
    act("$n âûòÿãèâàåò $N4 èç íåáûòèÿ!", ch, NULL, wch, TO_ROOM);

    sprintf(buf, "[R: %d] %s çàãðóæàåò %s â ìèð.", ch->in_room ? ch->in_room->vnum : 0, ch->name, cases(wch->name, 3));
    wiznet(buf, ch, NULL, WIZ_LOGINS, 0, 0);

    if (wch->pet != NULL)
    {
	char_from_room(wch->pet);
	char_to_room(wch->pet, wch->in_room, FALSE);
	act("$n âõîäèò â ýòîò ìèð.", wch->pet, NULL, NULL, TO_ROOM);
    }
}

void do_punload(CHAR_DATA *ch, char *argument)
{
    CHAR_DATA *victim;
    char who[MAX_INPUT_LENGTH];
    char buf[MAX_STRING_LENGTH];

    argument = one_argument(argument, who);

    if ((victim = get_char_world(NULL, who)) == NULL)
    {
	send_to_char("Òàêèõ çäåñü íåò.\n\r", ch);
	return;
    }

    /* Èãðîê íå áûë çàãðóæåí êîìàíäîé! */
    /* Çàîäíî è ïîòåðÿâøèå ñâÿçü âûëåòÿò.. */
    /* Äà è ôèã ñ íèìè! 8) */
    if (victim->desc != NULL)
    {
	send_to_char("ß äóìàþ ýòî áóäåò íå î÷åíü õîðîøåé èäååé...\n\r", ch);
	return;
    }

    /* Âîçâðàùàåì èãðîêà è î÷àðèêà â èñõîäíóþ êîìíàòó */
    if (victim->was_in_room != NULL)
    {
	char_from_room(victim);
	char_to_room(victim, victim->was_in_room, FALSE);
	if (victim->pet != NULL)
	{
	    char_from_room(victim->pet);
	    char_to_room(victim->pet, victim->was_in_room, FALSE);
	}
    }

    VALIDATE(victim);

    sprintf(buf, "[R: %d] %s âûãðóæàåò %s èç ìèðà.",
	    ch->in_room ? ch->in_room->vnum : 0,
	    ch->name, cases(victim->name, 3));
    wiznet(buf, ch, NULL, WIZ_LOGINS, 0, 0);

    send_to_char("Îê.\n\r", ch);
    act("$n îòïðàâëÿåò $N3 îáðàòíî â íåáûòèå.",
	ch, NULL, victim, TO_ROOM);

    do_quit(victim, "_silent_quit");

}

/* Ïðîâåðî÷íàÿ ôóíêöèÿ */
void do_immcompare(CHAR_DATA *ch, char *argument)
{
    char arg1[MAX_INPUT_LENGTH];
    char arg2[MAX_INPUT_LENGTH];
    OBJ_DATA *obj1;
    OBJ_DATA *obj2;
    char *msg;

    argument = one_argument(argument, arg1);
    argument = one_argument(argument, arg2);
    if (arg1[0] == '\0')
    {
	send_to_char("Ñðàâíèòü ÷òî ñ ÷åì?\n\r", ch);
	return;
    }

    if ((obj1 = get_obj_carry(ch, arg1, ch)) == NULL)
    {
	send_to_char("Òû íå íåñåøü ýòîãî.\n\r", ch);
	return;
    }

    if (arg2[0] == '\0')
    {
	for (obj2 = ch->carrying; obj2 != NULL; obj2 = obj2->next_content)
	{
	    if (obj2->wear_loc != WEAR_NONE
		&& can_see_obj(ch, obj2)
		&& obj1->item_type == obj2->item_type
		&& (obj1->wear_flags & obj2->wear_flags & ~ITEM_TAKE) != 0)
	    {
		break;
	    }
	}

	if (obj2 == NULL)
	{
	    send_to_char("Íà òåáå íåò íè÷åãî ñðàâíèìîãî.\n\r", ch);
	    return;
	}
    }
    else if ((obj2 = get_obj_carry(ch, arg2, ch)) == NULL)
    {
	send_to_char("Ó òåáÿ íåò ýòîãî.\n\r", ch);
	return;
    }

    msg	= NULL;

    if (obj1 == obj2)
    {
	msg = "Òû ñðàâíèâàåøü $p ñ íèì æå. Âûãëÿäèò îäèíàêîâî.";
    }

    if (msg == NULL)
    {
	if (obj1 == get_best_object(obj1, obj2)
	    && obj2 == get_best_object(obj2, obj1))
	{
	    msg = "$p è $P âûãëÿäÿò îäèíàêîâî.";
	}
	else if (obj1 == get_best_object(obj1, obj2))
	    msg = "$p âûãëÿäèò ëó÷øå, ÷åì $P.";
	else
	    msg = "$p âûãëÿäèò õóæå, ÷åì $P.";
    }

    act(msg, ch, obj1, obj2, TO_CHAR);
    return;
}

int errfunc(const char *path, int err)
{
    return 0;
}

void strip_colors(char *str);

void do_players(CHAR_DATA *ch, char *argument)
{
    int i, j, max;
    int matches = 0;
    char buf[MAX_STRING_LENGTH], bar[50];
    int races[MAX_PC_RACE], classes[MAX_CLASS];
    float scale;
    bool fDesc = FALSE;
    FILE *fp = NULL;
    DIR *dp;
    struct dirent *ent;

    one_argument(argument, buf);

    if (buf[0] != '\0' && !str_prefix(buf, "descriptions"))
    {
	fDesc = TRUE;
	if ((fp = file_open("descriptions.txt", "w")) == NULL)
	{
	    bugf("Couldn't open descriptions file for writing ...");
	    return;
	}
    }

    if ((dp = opendir(PLAYER_DIR)) == NULL)
    {
	bugf("Cannot open directory %s!", PLAYER_DIR);
	return;
    }

    for (i = 0; i < MAX_PC_RACE; i++)
	races[i] = 0;

    for (i = 0; i < MAX_CLASS; i++)
	classes[i] = 0;

    while ((ent = readdir(dp)) != NULL)
    {
	char *t;
	bool load, bad;
	CHAR_DATA *victim;
	DESCRIPTOR_DATA *d = new_descriptor();

	bad = FALSE;

	for (t = ent->d_name; *t != '\0'; t++)
	{
	    if (!IS_RUSSIAN(*t))
	    {
		bad = TRUE;
		break;
	    }
	}

	if (bad)
	    continue;

	matches++;

	if ((victim = get_char_world(ch, ent->d_name)) != NULL && !IS_NPC(victim))
	    load = FALSE;
	else
	{
	    if (!load_char_obj(d, ent->d_name, FALSE, FALSE))
		continue;

	    victim = d->character;
	    load = TRUE;
	    check_light_status(victim);
	}

	if (fDesc && victim->description[0] != '\0')
	{
	    char d[MAX_STRING_LENGTH];

	    strcpy(d, victim->description);
	    strip_colors(d);

	    fprintf(fp, "%s [%d] %s %s\n%s\n\n",
		    victim->name, victim->level,
		    pc_race_table[victim->race].who_name,
		    class_table[victim->classid].who_name, d);
	}
	else
	{
	    races[victim->race]++;
	    classes[victim->classid]++;
	}

	if (load)
	{
	    extract_char_for_quit(victim, FALSE);
	    INVALIDATE(d);
	    SLIST_NEXT(d, link) = NULL;
	    SLIST_INSERT_HEAD(&descriptor_free, d, link);
	}
    }

    if (fDesc)
    {
	file_close(fp);
	send_to_char("Ok.\n\r", ch);
	return;
    }

    max = 0;
    for(i = 0; i < MAX_PC_RACE; i++)
	max = UMAX(max, races[i]);

    if (max <= 50)
	scale = 1;
    else
	scale = (float) (max/50);

    for(i = 1; i < MAX_PC_RACE; i++)
    {
	bar[0] = '\0';

	for (j = 0; j < races[i]/scale; j++)
	    strcat(bar, "#");

	sprintf(buf, "%-10s: %4d %s\n\r",
		pc_race_table[i].who_name, races[i], bar);
	send_to_char(buf, ch);
    }

    send_to_char("\n\r", ch);

    max = 0;
    for(i = 0; i < MAX_CLASS; i++)
	max = UMAX(max, classes[i]);

    if (max <= 50)
	scale = 1;
    else
	scale = (float) (max/50);

    for(i = 0; i < MAX_CLASS; i++)
    {
	bar[0] = '\0';

	for (j = 0; j < classes[i]/scale; j++)
	    strcat(bar, "#");

	sprintf(buf, "%-10s: %4d %s\n\r",
		class_table[i].who_name, classes[i], bar);
	send_to_char(buf, ch);
    }

    sprintf(buf, "\n\rÍàéäåíî âñåãî: %d\n\r", matches);

    send_to_char(buf, ch);
    closedir(dp);
}

void do_nonotes(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH], buf[MAX_STRING_LENGTH];
    CHAR_DATA *victim;

    argument = one_argument(argument, arg);

    if (arg[0] == '\0')
    {
	send_to_char("Ó êîãî îòîáðàòü âîçìîæíîñòü ïèñàòü ñîîáùåíèÿ?\n\r", ch);
	return;
    }

    if ((victim = get_char_world(ch, arg)) == NULL || IS_NPC(victim))
    {
	send_to_char("Òàêèõ íåò â ýòîì ìèðå.\n\r", ch);
	return;
    }

    if (get_trust(victim) >= get_trust(ch))
    {
	send_to_char("Ñúåäÿò âåäü... È íå ïî÷åøóòñÿ.\n\r", ch);
	return;
    }

    if (IS_SET(victim->comm, COMM_NONOTES))
    {
	REMOVE_BIT(victim->comm, COMM_NONOTES);
	send_to_char("{RÁîãè âåðíóëè òåáå âîçìîæíîñòü ïèñàòü ñîîáùåíèÿ.{x\n\r",
		     victim);
	send_to_char("NONOTES removed.\n\r", ch);
	sprintf(buf, "$N âîçâðàùàåò %s ïðàâî ïîëüçîâàíèÿ ïèñüìàìè",
		cases(victim->name, 2));
	wiznet(buf, ch, NULL, WIZ_PENALTIES, WIZ_SECURE, 0);
	victim->pcdata->nonotes = 0;
    }
    else
    {
	victim->pcdata->nonotes = get_next_seconds(argument, ch);
	SET_BIT(victim->comm, COMM_NONOTES);
	send_to_char("{RÁîãè îòîáðàëè ó òåáÿ âîçìîæíîñòü ïèñàòü ñîîáùåíèÿ.{x\n\r",
		     victim);
	send_to_char("NONOTES set.\n\r", ch);
	sprintf(buf, "$N ëèøàåò %s ïðàâà ïîëüçîâàòüñÿ ïèñüìàìè.",
		cases(victim->name, 1));
	wiznet(buf, ch, NULL, WIZ_PENALTIES, WIZ_SECURE, 0);
    }

    return;
}

void do_notitle(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH], buf[MAX_STRING_LENGTH];
    CHAR_DATA *victim;

    argument = one_argument(argument, arg);

    if (arg[0] == '\0')
    {
	send_to_char("Ó êîãî îòîáðàòü ïðàâî âûáèðàòü ñåáå òèòóë?\n\r", ch);
	return;
    }

    if ((victim = get_char_world(ch, arg)) == NULL || IS_NPC(victim))
    {
	send_to_char("Òàêèõ íåò â ýòîì ìèðå.\n\r", ch);
	return;
    }

    if (get_trust(victim) >= get_trust(ch))
    {
	send_to_char("À íå ñòðàøíî? Áîëüíî æå áóäåò...\n\r", ch);
	return;
    }

    if (IS_SET(victim->comm, COMM_NOTITLE))
    {
	REMOVE_BIT(victim->comm, COMM_NOTITLE);
	send_to_char("{RÁîãè âåðíóëè òåáå ïðàâî âûáèðàòü ñåáå òèòóë.{x\n\r",
		     victim);
	send_to_char("NOTITLE removed.\n\r", ch);
	sprintf(buf, "$N âîçâðàùàåò %s ïðàâî âûáèðàòü ñåáå òèòóë",
		cases(victim->name, 2));
	wiznet(buf, ch, NULL, WIZ_PENALTIES, WIZ_SECURE, 0);
	victim->pcdata->notitle = 0;
    }
    else
    {
	victim->pcdata->notitle = get_next_seconds(argument, ch);
	SET_BIT(victim->comm, COMM_NOTITLE);
	send_to_char("{RÁîãè îòîáðàëè ó òåáÿ ïðàâî âûáèðàòü ñåáå òèòóë.{x\n\r",
		     victim);
	send_to_char("NOTITLE set.\n\r", ch);
	sprintf(buf, "$N ëèøàåò %s ïðàâî âûáèðàòü ñåáå òèòóë.",
		cases(victim->name, 1));
	wiznet(buf, ch, NULL, WIZ_PENALTIES, WIZ_SECURE, 0);
    }

    return;
}


/*
void do_arena(CHAR_DATA *ch, char *argument)
{
    CHALLENGER_DATA *challenger;
    CHALLENGER_DATA *challenger2 = NULL;
    CHALLENGER_DATA *challengerp = NULL;
    SCORE_DATA *score;
    int num = 0, max = 0;
    char buf[MSL];
    char arg1[MIL];
    char arg2[MIL];
    char arg3[MIL];
    char arg4[MIL];

    argument = one_argument(argument, arg1);
    argument = one_argument(argument, arg2);
    argument = one_argument(argument, arg3);
    argument = one_argument(argument, arg4);

    if (arg1[0] == '\0' || !str_prefix(arg1, "ñòàòèñòèêà")
	|| !str_prefix(arg1, "stat")
	|| !str_prefix(arg1, "èíôî")
	|| !str_prefix(arg1, "info"))
    {
	for (challenger  = challenger_list;
	     challenger != NULL;
	     challenger  = challenger->next)

	{
	    sprintf(buf, "Ó÷àñòíèê: {R%s{x\n\r", challenger->name);
	    send_to_char(buf,ch);
	    for (score = challenger->score; score; score = score->next)
	    {
		sprintf(buf, "Äàòà äóýëè     : {Y%s{x", c_time(&score->time));
		send_to_char(buf,ch);
		sprintf(buf, "Ïðîòèâíèê      : {Y%s{x\n\r", score->opponent);
		send_to_char(buf,ch);
		sprintf(buf, "Äóýëü âûèãðàíà?: {Y%s{x\n\r", 
				score->won == 2 ? "Äà" 
				  	       : (score->won == 1 ? "Íè÷üÿ" : "Íåò"));
		send_to_char(buf,ch);
	    }
	    sprintf(buf, "Îáùèé ðåçóëüòàò: {Y%d{x\n\r", challenger->ball);
	    send_to_char(buf,ch);
	    send_to_char("{G---------------------------------{x\n\r\n\r", ch);
	}
    }
    else if ((!str_prefix(arg1, "ñïèñîê") || !str_prefix(arg1, "list"))
	     && get_trust(ch) >= ANGEL )
    {
	if (arg2[0] == '\0') //Ñåáå
	{
		send_to_char("ÒÓÐÍÈÐÍÀß ÒÀÁËÈÖÀ\n\r",ch);
		send_to_char("|Ìåñòî|Ó÷àñòíèê ----------|Áîåâ|Î÷êè|\n\r",ch);
	}
	else
	if (!str_prefix(arg2, "ìèð") || !str_prefix(arg2, "world") || !str_prefix(arg2, "gecho")) //Íà ìèð
	{
		do_echo(ch,"ÒÓÐÍÈÐÍÀß ÒÀÁËÈÖÀ");
		do_echo(ch,"|Ìåñòî|Ó÷àñòíèê ----------|Áîåâ|Î÷êè|");
	}
	else
	if (!str_prefix(arg2, "ðóì") || !str_prefix(arg2, "room"))	//Ëîêàëüíî
	{
		do_recho(ch,"ÒÓÐÍÈÐÍÀß ÒÀÁËÈÖÀ");
		do_recho(ch,"|Ìåñòî|Ó÷àñòíèê ----------|Áîåâ|Î÷êè|");
	}
	else
	if (!str_prefix(arg2, "çîíà") || !str_prefix(arg2, "zone"))	//Íà çîíó
	{
		do_zecho(ch,"ÒÓÐÍÈÐÍÀß ÒÀÁËÈÖÀ");
		do_zecho(ch,"|Ìåñòî|Ó÷àñòíèê ----------|Áîåâ|Î÷êè|");
	}

	for (challenger  = challenger_list;
	     challenger != NULL;
	     challenger  = challenger->next)
	{
		max++;
		challenger->sort = FALSE;
	}

	for (num = 1; num <= max ; num++)
	{
		for (challenger  = challenger_list;
	     	challenger != NULL;
	     	challenger  = challenger->next)
		{				
			if (challenger2 == challenger)
				challenger->sort = TRUE;
			if (challenger->sort)
				continue;

			if (challengerp == NULL 
			|| (challengerp->ball == challenger->ball && 
				((challengerp->game > challenger->game && challenger->game != 0)
				|| (challenger->game != 0 && challengerp->game == 0)))
			|| challengerp->ball < challenger->ball)
				challengerp = challenger;
		}

		if (arg2[0] == '\0') //Ñåáå
		{
	    		sprintf(buf, "| %-3d | %-17s | %-3d| %-3d|\n\r",num, 
				challengerp->name,challengerp->game,challengerp->ball);
	    		send_to_char(buf,ch);
		}
		else
		{
    			sprintf(buf, "| %-3d | %-17s | %-3d| %-3d|",num, 
				challengerp->name,challengerp->game,challengerp->ball);
			if (!str_prefix(arg2, "ìèð") 
			|| !str_prefix(arg2, "world") 
			|| !str_prefix(arg2, "gecho")) //Íà ìèð
				do_echo(ch,buf);
			if (!str_prefix(arg2, "ðóì") || !str_prefix(arg2, "room"))	//Ëîêàëüíî
				do_recho(ch,buf);
			if (!str_prefix(arg2, "çîíà") || !str_prefix(arg2, "zone"))	//Íà çîíó
				do_zecho(ch,buf);
		}
	    	challenger2 = challengerp;
	    	challengerp = NULL;
	}
    }
    else if ((!str_prefix(arg1, "äîáàâèòü") || !str_prefix(arg1, "add"))
	     && get_trust(ch) >= ANGEL )
    {
	short int won;
	time_t time;

	if (arg2[0] != '\0' && arg3[0] == '\0')
	{
		if ((challenger = find_challenger(arg2)) == NULL)
		{
	   		challenger = add_challenger(capitalize(arg2));
			send_to_char("Ó÷àñòíèê äîáàâëåí.\n\r", ch);
		}
		else
			send_to_char("Òàêîé ó÷àñòíèê óæå åñòü.\n\r", ch);
		return;
	}

	if (arg2[0] == '\0' || arg3[0] == '\0' || arg4[0] == '\0' ||
	    !is_number(arg4))
	{
	    send_to_char("Ñèíòàêñèñ:\n\r"
			 " àðåíà äîáàâèòü <ó÷àñòíèê> [<ïðîòèâíèê> <ñ÷åò>]\n\r"
			 "\tó÷àñòíèê  - èìÿ ó÷àñòíèêà, ïðè óêàçàíèè ïðîòèâíèêà íóæíî äîáàâèòü ñ÷åò.\n\r"
			 "\tïðîòèâíèê - èìÿ ïðîòèâíèêà, ñ êîòîðûì ñðàæàëñÿ ó÷àñòíèê.\n\r"
			 "\tñ÷åò      - â ÷üþ ïîëüçó çàêîí÷èëñÿ ïîåäèíîê: 2 - ó÷àñòíèê, 1 - íè÷üÿ, 0 - ïðîòèâíèê.\n\r", ch);
	    return;
	}

	won = atoi(arg4);

	if ( won < 0 || won > 2 )
	{
	    send_to_char("Ñ÷åò äîëæåí áûòü 2(ïîáåäà ó÷àñòíèêà), 1(íè÷üÿ) èëè 0(ïðîèãðûø)!\n\t", ch);
	    return;
	}

	time = current_time;
	if ( (challenger = find_challenger(arg2)) == NULL)
	    challenger = add_challenger(capitalize(arg2));
	add_score(challenger, time, capitalize(arg3), won);

	if ( (challenger2 = find_challenger(arg3)) == NULL)
	    challenger2 = add_challenger(capitalize(arg3));
	add_score(challenger2, time, capitalize(arg2), won == 2 ? 0 : (won == 1 ? 1 : 2));
	send_to_char("Äàííûå ïîåäèíêà äîáàâëåíû.\n\r", ch);
    }
    else if ((!str_prefix(arg1, "óäàëèòü") || !str_prefix(arg1, "delete"))
	     && get_trust(ch) >= ANGEL )
    {
	if (arg2[0] == '\0')
	{
	    send_to_char("Ñèíòàêñèñ:\n\r"
			 " àðåíà óäàëèòü <ó÷àñòíèê> [ñ÷åò] [<äàòà&âðåìÿ>]\n\r"
			 "\tó÷àñòíèê   - èìÿ ó÷àñòíèêà, êîòîðîãî íóæíî óäàëèòü\n\r"
			 "\t             èëè ó êîòîðîãî íóæíî óäàëèòü ñ÷åò.\n\r"
			 "\tñ÷åò       - óêàçûâàåòñÿ åñëè íåîáõîäèìî óäàëèòü ñ÷åò,\n\r"
			 "\t             à íå ó÷àñòíèêà.\n\r"
			 "\täàòà&âðåìÿ - Äàòà è âðåìÿ óäàëÿåìîãî ñ÷åòà â ôîðìàòå:\n\r"
			 "\t             'ÄÄ.ÌÌ.ÃÃ-÷÷:ìì:ññ'.\n\r", ch);
	}
	if ((challenger = find_challenger(arg2)) == NULL)
	{
	    send_to_char("Òàêîãî ó÷àñòíèêà íåò.\n\r", ch);
	    return;
	}
	else if ( arg3[0] == '\0')
	{
	    delete_challenger(challenger);
	    send_to_char("Ó÷àñòíèê óäàëåí.\n\r", ch);
	    return;
	}
	else if (!str_prefix(arg3, "ñ÷åò") || !str_prefix(arg1, "score"))
	{
	    if (arg4[0] == '\0')
	    {
		send_to_char("Íåîáõîäèìî ââåñòè äàòó óäàëÿåìîãî ñ÷åòà.\n\r", ch);
		return;
	    }
	    else
	    {
		struct tm tp;
		time_t stime;
		int day, month, year, hour, min, sec;

		sscanf(arg4, "%2d.%2d.%2d-%2d:%2d:%2d",
		       &day, &month, &year, &hour, &min, &sec);
		if (day  < 1 || day  > 31  || month < 1 || month > 12 ||
		    year < 0 || year > 100 || hour  < 0 || hour  > 23 ||
		    min  < 0 || min  > 59  || sec   < 0 || sec   > 59)
		{
		    send_to_char("Íåâåðíàÿ èëè íåäîïóñòèìàÿ äàòà.\n\r", ch);
		    return;
		}

		tp.tm_mday = day;
		tp.tm_mon = month - 1;
		tp.tm_year = year + 100;
		tp.tm_hour = hour;
		tp.tm_min = min;
		tp.tm_sec = sec;
		tp.tm_isdst = -1;

		if ((stime = mktime(&tp)) == -1)
		{
		    send_to_char("Íåâåðíàÿ èëè íåäîïóñòèìàÿ äàòà.\n\r", ch);
		    return;
		}

		if ((score = find_score_by_time(challenger, stime)) == NULL)
		{
		    send_to_char("Íåò ñ÷åòà ïî ââåäåíîé äàòå.\n\r", ch);
		    return;
		}
		else
		{
		    delete_score(challenger, score->time);
		    send_to_char("Ðåçóëüòàò ïîåäèíêà óäàëåò.\n\r", ch);
		    if (challenger->score == NULL)
		    {
			send_to_char("Ó ó÷àñòíèêà íå îñòàëîñü ñ÷åòà.\n\rÓäàëåíèå ó÷àñòíèêà...\n\rOk.\n\r", ch);
			delete_challenger(challenger);
		    }
		    return;
		}
	    }
	}
    }
    return;
}
*/

int compare_popularity(const void *v1, const void *v2, int what)
{
    AREA_DATA *a1 = *(AREA_DATA **)v1;
    AREA_DATA *a2 = *(AREA_DATA **)v2;

    if (a1->popularity[what] > a2->popularity[what])
	return -1;
    else if (a1->popularity[what] < a2->popularity[what])
	return 1;
    else
	return 0;
}

int compare_popularity_v(const void *v1, const void *v2)
{
    return compare_popularity(v1, v2, POPUL_VISIT);
}

int compare_popularity_k(const void *v1, const void *v2)
{
    return compare_popularity(v1, v2, POPUL_KILL);
}

int compare_popularity_p(const void *v1, const void *v2)
{
    return compare_popularity(v1, v2, POPUL_PK);
}

int compare_popularity_d(const void *v1, const void *v2)
{
    return compare_popularity(v1, v2, POPUL_DEATH);
}

void save_popularity()
{
    FILE *fp;
    AREA_DATA *area;
    int i;
    int64_t total[MAX_POPUL];

    if ((fp = file_open(POPULARITY_FILE, "w")) == NULL)
	bugf("Couldn't open popularity file for writing...");
    else
    {
	fprintf(fp, "#%d\n", POPUL_VERS);

	for(i = 0; i < MAX_POPUL; i++)
	    total[i] = 0;

	for (area = area_first; area; area = area->next)
	    if (!IS_SET(area->area_flags, AREA_NOPOPULARITY))
		for(i = 0; i < MAX_POPUL; i++)
		    total[i] += area->popularity[i];

	for (area = area_first; area; area = area->next)
	{
	    if (!IS_SET(area->area_flags, AREA_NOPOPULARITY))
	    {
		fprintf(fp, "%s", area->file_name);

		for(i = 0; i < MAX_POPUL; i++)
		{
		    int64_t wr;

		    if (total[i] == 0)
			wr = 0;
		    else if (i == POPUL_VISIT)
			wr = (1000 * area->popularity[i])/total[i];
		    else
			wr = area->popularity[i];
		    fprintf(fp, " %ld", wr);
		}

		fprintf(fp, "\n");
	    }
	}

	file_close(fp);
    }
}

void do_popularity(CHAR_DATA *ch, char *argument)
{
    AREA_DATA *area_table[top_area];
    AREA_DATA *area;
    bool all = TRUE;
    int i, max = 10, total_areas = 0;
    int64_t total = 0;
    BUFFER *out;
    char buf[MAX_INPUT_LENGTH];
    int (*cmpfunc)(const void *, const void *) = compare_popularity_v;
    int what = POPUL_VISIT;

    argument = one_argument(argument, buf);
    if (IS_NULLSTR(buf) || !str_cmp(buf, "visit") || !str_cmp(buf, "âèçèòû"))
    {
	cmpfunc = compare_popularity_v;
	what = POPUL_VISIT;
    }
    else if (!str_cmp(buf, "pk") || !str_cmp(buf, "ïê"))
    {
	cmpfunc = compare_popularity_p;
	what = POPUL_PK;
    }
    else if (!str_cmp(buf, "mob") || !str_cmp(buf, "ìîá"))
    {
	cmpfunc = compare_popularity_k;
	what = POPUL_KILL;
    }
    else if (!str_cmp(buf, "death") || !str_cmp(buf, "ñìåðòè"))
    {
	cmpfunc = compare_popularity_d;
	what = POPUL_DEATH;
    }
    else if (!str_cmp(buf, "reset") || !str_cmp(buf, "ñáðîñ"))
    {
	for (area = area_first; area != NULL; area = area->next)
	    for (i = 0; i < MAX_POPUL; i++)
		area->popularity[i] = 0;

	send_to_char("Ok.\n\r", ch);
	return;
    }
    else
    {
	send_to_char("Ïðèìåíåíèå: ïîïóëÿðíîñòü <òèï> [<êîë-âî ïîêàçûâàåìûõ ýëåìåíòîâ>],\n\r"
		     "ãäå <òèï> ìîæåò èìåòü çíà÷åíèÿ:\n\r"
		     "âèçèòû - ïîïóëÿðíîñòü çîí ïî íàõîæäåíèþ â íåé èãðîêîâ;\n\r"
		     "ïê     - -------\"-------- ïî ÏÊ;\n\r"
		     "ìîá    - -------\"-------- ïî óáèéñòâó ìîáîâ â çîíå.\n\r"
		     "ñìåðòè - -------\"-------- ïî ñìåðòè èãðîêîâ.\n\r"
		     "ñáðîñ  - ñáðîñèòü íàêîïëåííóþ ñòàòèñòèêó.\n\r" , ch);
	return;
    }

    if (!IS_NULLSTR(argument) && is_number(argument))
    {
	max = atoi(argument);
	all = FALSE;
    }

    for (area = area_first, i = 0; area; area = area->next)
    {
	if (!IS_SET(area->area_flags, AREA_NOPOPULARITY))
	{
	    total += area->popularity[what];
	    area_table[i++] = area;
	    total_areas++;
	}
    }

    out = new_buf();

    qsort(area_table, i, sizeof(area_table[0]), cmpfunc);

    sprintf(buf, "[%3s] [%-40s] [%-10s] [%s]\n\r",
	    "Num", "Area Name", "Popularity", "Percent");
    add_buf(out, buf);
    for (i = 0; i < total_areas && (all || i < max); i++)
    {
	sprintf(buf, "%s[%3d] %-42s [%10d] %7.2f%%{x\n\r",
		i == 0 ? "{G" : (i < 3 ? "{g" : (i < 10 ? "{y" : "")),
		area_table[i]->vnum,
		IS_NULLSTR(area_table[i]->credits)
		? area_table[i]->name
		: area_table[i]->credits,
		area_table[i]->popularity[what],
		total != 0
		? (double)100 * area_table[i]->popularity[what] / total
		: 0);
	add_buf(out, buf);
    }

    page_to_char(buf_string(out), ch);
    free_buf(out);
}


void do_save(CHAR_DATA *ch, char *argument)
{
    char arg[MIL];
    CHAR_DATA *victim;

    one_argument(argument, arg);

    if (arg[0] == '\0' || !str_cmp(arg, "all") || !str_cmp(arg, "âñåõ"))
    {
	LIST_FOREACH(victim, &char_list, link)
	    if (!IS_NPC(victim))
	    {
		send_to_char("Ñîõðàíåíèå. Ïîäîæäè íåìíîãî.\n\r", victim);
		save_char_obj(victim, FALSE);
	    }
    }
    else
    {
	if ((victim = get_char_world(ch, arg)) == NULL || IS_NPC(victim))
	{
	    send_to_char("Òàêèõ íåò â ýòîì ìèðå.\n\r", ch);
	    return;
	}

	send_to_char("Ñîõðàíåíèå. Ïîäîæäè íåìíîãî.\n\r", victim);
	save_char_obj(victim, FALSE);
    }

    send_to_char("Ok.\n\r", ch);
    return;
}

void say_spell(CHAR_DATA *ch, int sn, bool check, bool success);

void do_sayspells(CHAR_DATA *ch, char *argument)
{
    int i, count;
    char buf[MIL];

    for (i = 0, count = 0; i < max_skills; i++)
    {
	if (IS_NULLSTR(skill_table[i].name))
	    break;

	if (skill_table[i].spell_fun != spell_null)
	{
	    sprintf(buf, "%3d) %-25s (%3d):  ", count, skill_table[i].name, i);
	    send_to_char(buf, ch);
	    say_spell(ch, i, TRUE, TRUE);
	    count++;
	}
    }
}

#if defined(ONEUSER)
void do_promote(CHAR_DATA *ch, char *argument)
{
    int iLevel;

    for (iLevel = ch->level ; iLevel < MAX_LEVEL; iLevel++)
    {
	ch->level++;
	advance_level(ch, TRUE);
    }

    grant_all(ch);
    ch->pcdata->security = 9;
    send_to_char("Òåïåðü òåáå äîñòóïíû âñå êîìàíäû.\n\r", ch);
}
#endif


void do_house(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH];
    char arg2[MAX_STRING_LENGTH];

    CHAR_DATA *victim;
    int vnum = 0;

    argument = one_argument(argument, arg);

    if (arg[0] == '\0')
    {
	send_to_char("Ñèíòàêñèñ: äîì <char> <vnum>\n\r", ch);
	send_to_char("Ñèíòàêñèñ: äîì <char> NONE\n\r", ch);
	return;
    }

    if ((victim = get_char_world(ch, arg)) == NULL)
    {
	send_to_char("Òàêèõ â äàííûé ìîìåíò â ìèðå íåò.\n\r", ch);
	return;
    }

    if (IS_NPC(victim))
    {
	send_to_char("Òîëüêî íå ìîáà.\n\r", ch);
	return;
    }

    one_argument(argument, arg2);

    if (arg2[0] == '\0')
    {
	send_to_char("À âíóì äîìà? Åñëè õî÷åøü ñíÿòü âíóì, òî óêàæè NONE.\n\r", ch);
	return;
    }

    if ( !str_cmp(arg2, "NONE") || !str_cmp(arg2, "none") )
    {
	vnum = 0;
    }
    else
    {
	vnum = atoi(arg2);
	if ( vnum == 0 )
	{
	    send_to_char("Âíóì äîìà, óêàçàííûé òîáîé, íå ÿâëÿåòñÿ ÷èñëîì èëè ïàðàìåòðîì NONE.\n\r", ch);
	    return;
	}
    }

    if ((vnum < ROOM_VNUM_HOUSE) || (vnum > ROOM_VNUM_HOUSE + 1000))
	if (vnum != 0)
	{
	    send_to_char("Âíóì äîìà, óêàçàííûé òîáîé, íå ïðèíàäëåæèò ê çîíå äîìîâ.\n\r", ch);
	    return;
	}

    if ((vnum != 0) && (victim->pcdata->vnum_house != 0))
    {
	send_to_char("Ó ýòîãî ÷àðà óæå åñòü äîì. Åñëè õî÷åøü ëèøèòü åãî äîìà - ñäåëàé house <char> NONE.\n\r", ch);
	return;
    }

    victim->pcdata->vnum_house = vnum;

    if (vnum == 0)
	send_to_char("Âíóì äîìà ÷àðà ñíÿò.\n\r", ch);
    else
    {
	send_to_char("Âíóì äîìà óñòàíîâëåí.\n\r", ch);
	send_to_char("Âíóì òâîåãî äîìà òåáå óñòàíîâëåí.\n\r", victim);
    }

    return;
}

void do_whoislist(CHAR_DATA *ch, char *argument)
{
    char buf[MSL];
    int i;

    send_to_char("Ðàçðåøåííûå IP àäðåñà:\n\r", ch);
    for (i = 0; i < MAX_WHOIS_ENTRIES && whiteip[i] != NULL; i++)
    {
	sprintf(buf, "%s\n\r", whiteip[i]);
	send_to_char(buf, ch);
    }

    send_to_char("\n\rÐàçðåøåííûå äîìåííûå ñóôôèêñû:\n\r", ch);
    for (i = 0; i < MAX_WHOIS_ENTRIES && whitedomains[i] != NULL; i++)
    {
	sprintf(buf, "%s  ", whitedomains[i]);
	send_to_char(buf, ch);
    }

    send_to_char("\n\r", ch);
}


void do_recipe(CHAR_DATA *ch, char *argument)
{
    RECIPE_DATA *recipe;

    char arg1[MIL];

    argument = one_argument(argument, arg1);

    if (arg1[0] == '\0' || !str_prefix(arg1, "ñòàòèñòèêà")
	|| !str_prefix(arg1, "stat")
	|| !str_prefix(arg1, "èíôî")
	|| !str_prefix(arg1, "info"))
    {
	list_recipe(ch);
    }
    else if (!str_prefix(arg1, "create") || !str_prefix(arg1, "ñîçäàòü")
	|| !str_prefix(arg1, "äîáàâèòü") || !str_prefix(arg1, "add"))
    {
	int sn;

	if (argument[0] == '\0')
	{
	    send_to_char("Îïðåäåëèòå íàçâàíèå.\n\r",ch);
	    return;
	}

	if ((sn = skill_lookup(argument)) != -1
	&& skill_table[sn].spell_fun != spell_null)
	{
	    if ((recipe = find_recipe(skill_table[sn].rname)) != NULL)
		send_to_char("{GÒàêîé ðåöåïò óæå ñóùåñòâóåò! Äîáàâëÿåì íîâûé.\n\r{x",ch);

	    recipe = add_recipe(skill_table[sn].rname);
	}
	else
	{
	    send_to_char("Òàêîãî çàêëèíàíèÿ íå ñóùåñòâóåò, ïîýòîìó òèï èçíà÷àëüíî çàùèòà.\n\r",ch);
	    recipe = add_recipe(argument);
	    recipe->type = RECIPE_TANK;
	}
	show_recipe(ch, recipe);
    }
    else if (!str_prefix(arg1, "delete") || !str_prefix(arg1, "óäàëèòü"))
    {
	if (is_number(argument))
	    recipe = get_recipe(atoi(argument));
	else 
	    recipe = find_recipe(argument);

	if (recipe == NULL)
	{
	    send_to_char("Òàêîãî ðåöåïòà íåò.\n\r", ch);
	    return;
	}
	else
	{
	    delete_recipe(recipe);
	    send_to_char("Ðåöåïò óäàëåí.\n\r", ch);
	}
    }
    else if (!str_prefix(arg1, "edit") || !str_prefix(arg1, "èçìåíèòü"))
    {
	edit_recipe(ch,argument);
    }
    else if (!str_prefix(arg1, "save") || !str_prefix(arg1, "ñîõðàíèòü"))
    {
	save_recipes();
	send_to_char("Ðåöåïòû ñîõðàíåíû.\n\r", ch);
    }
    else if (!str_prefix(arg1, "show") || !str_prefix(arg1, "ïîêàçàòü"))
    {
	if (is_number(argument))
	    recipe = get_recipe(atoi(argument));
	else
	    recipe = find_recipe(argument);

	if (recipe == NULL)
	{
	    send_to_char("Ðåöåïò íå íàéäåí.\n\r",ch);
	    return;
	}
	show_recipe(ch,recipe);
    }
    else if (!str_prefix(arg1, "help") || !str_prefix(arg1, "ïîìîùü"))
    {
	help_recipe(ch);
    }

    return;
}
void do_immnotepad(CHAR_DATA *ch, char *argument)
{
    char arg[MAX_INPUT_LENGTH];
    CHAR_DATA *victim;

    one_argument(argument, arg);

    if (arg[0] == '\0')
    {
	send_to_char("Ñèíòàêñèñ: èììçàïèñè <char>\n\r", ch);
	return;
    }

    if ((victim = get_char_world(ch, arg)) == NULL)
    {
	send_to_char("Òàêèõ â äàííûé ìîìåíò â ìèðå íåò.\n\r", ch);
	return;
    }

    if (IS_NPC(victim))
    {
	send_to_char("Òîëüêî íå ìîáà.\n\r", ch);
	return;
    }

    if (ch->level < victim->level)
    {
	send_to_char("Òâîé óðîâåíü íå ïîçâîëÿåò ñìîòðåòü ýòè çàïèñè.\n\r", ch);
	return;
    }

    printf_to_char("Çàïèñè, êîòîðûå äåëàåò %s:\n\r", ch, capitalize(victim->name));
    send_to_char(!IS_NULLSTR(victim->notepad)
		? victim->notepad
		: "(Çàïèñè îòñóòñòâóþò).\n\r", ch);
    return;
}

void do_absolutely(CHAR_DATA *ch, char *argument)
{
    DESCRIPTOR_DATA *d;
    char buf[MAX_INPUT_LENGTH];
    CHAR_DATA *victim;

	SLIST_FOREACH(d, &descriptor_list, link)
	{
	    if (d->connected != CON_PLAYING)
		continue;
   
		victim   = CH(d);
		
		sprintf(buf, "%s êîë ñïåøêà %s", victim->name, victim->name);
		do_at(ch, buf);
		sprintf(buf, "%s êîë ãèã %s", victim->name, victim->name);
		do_at(ch, buf);
		sprintf(buf, "%s êîë áëàãî %s", victim->name, victim->name);
		do_at(ch, buf);
		sprintf(buf, "%s êîë ÿðîñòü %s", victim->name, victim->name);
		do_at(ch, buf);
		sprintf(buf, "%s êîë ùèò %s", victim->name, victim->name);
		do_at(ch, buf);
		sprintf(buf, "%s êîë áðîí %s", victim->name, victim->name);
		do_at(ch, buf);
		sprintf(buf, "%s êîë ïðèçðà÷ %s", victim->name, victim->name);
		do_at(ch, buf);
		sprintf(buf, "%s êîë 'çàùèòà õðàìà' %s", victim->name, victim->name);
		do_at(ch, buf);
		}

	send_to_char("OK", ch);

    return;
}

void do_choice(CHAR_DATA *ch, char *argument)
{
    QUERY_DATA *query = NULL;
    BUFFER *buffer = NULL;
    int vnum, count = 0;
    char buf[MAX_STRING_LENGTH];
    char arg1[MAX_INPUT_LENGTH];
    char arg2[MAX_INPUT_LENGTH];
    bool found;

    argument = one_argument(argument, arg1);

    found = FALSE;

    if (arg1[0] == '\0')
    {
	send_to_char("Syntax: choice obj <Íîìåð çàïðîñà>\n\r", ch);
	send_to_char("        choice mob <Íîìåð çàïðîñà>\n\r", ch);
	send_to_char("        choice query [list]\n\r", ch);
	send_to_char("        choice query add <Íàçâàíèå çàïðîñà>\n\r", ch);
	send_to_char("        choice query delete <Íîìåð çàïðîñà>\n\r", ch);
	send_to_char("        choice query edit <Íîìåð çàïðîñà>\n\r", ch);
	send_to_char("        choice query show <Íîìåð çàïðîñà>\n\r", ch);
	return;
    } else if (!str_prefix(arg1, "obj"))
    {

//Ïðè èçìåíåíèè íà çàïðîñû sql êîä îòñþäà ïðàâèòü
	OBJ_INDEX_DATA *obj, *array[top_vnum_obj];

	buffer = new_buf();
	
	if (argument != '\0' && !is_number(argument))
	{                 
	    send_to_char("Ââåäèòå íîìåð çàïðîñà.\n\r",ch);
	    return;
	}

	query = get_query(atoi(argument));
	if (query == NULL)
	{
	    send_to_char("Íåïðàâèëüíûé çàïðîñ.\n\r",ch);
	    return;
	}

	for (vnum = 1; vnum <= top_vnum_obj; vnum++)
	{    	
	    if ((obj = get_obj_index(vnum)) != NULL
		&& check_query_obj(query, obj))
	    {
		array[count++] = obj;
		if (count > 300)
		{
		    send_to_char("Íàéäåíî áîëåå 300 îáúåêòîâ. Óòî÷íèòå çàïðîñ.\n\r",ch);
		    return;
		}
		found = TRUE;
	    }
	}

	if (!found)
	{
	    send_to_char("Íå íàéäåíî îáúåêòîâ, óäîâëåòâîðÿþùèõ óñëîâèþ(ÿì).\n\r", ch);
		    free_buf(buffer);
	    return;
	}

	qsort(array, count, sizeof(obj), compare_objects);

	for (vnum = 0; vnum < count; vnum++)
	{
	    sprintf(buf, "%s: level: [%-2d]  vnum: [%-5d]  [%s]\n\r",
		str_color_fmt(array[vnum]->short_descr, 35), array[vnum]->level,
		array[vnum]->vnum, array[vnum]->area->name);
	    add_buf(buffer, buf);
	}

//è äî ñþäà

    } else if (!str_prefix(arg1, "mob"))
    {
	//îòêëþ÷åíà
	send_to_char("Â ðàçðàáîòêå.\n\r",ch);
	return;
// Òóò òîæå ñàìîå, òîëüêî ñ ìîáàìè
	MOB_INDEX_DATA *mob, *array[top_vnum_mob];

	buffer = new_buf();
	
	if (argument != '\0' && !is_number(argument))
	{                 
	    send_to_char("Ââåäèòå íîìåð çàïðîñà.\n\r",ch);
	    return;
	}

	query = get_query(atoi(argument));

	for (vnum = 1; vnum <= top_vnum_mob; vnum++)
	{    
	    if ((mob = get_mob_index(vnum)) != NULL
		&& check_query_mob(query, mob))
	    {
		array[count++] = mob;
		if (count > 300)
		{
		    send_to_char("Íàéäåíî áîëåå 300 ìîáîâ. Óòî÷íèòå çàïðîñ.\n\r",ch);
		    return;
		}

		found = TRUE;
	    }
	}

	if (!found)
	{
	    send_to_char("Íå íàéäåíî ìîáîâ, óäîâëåòâîðÿþùèõ óñëîâèþ(ÿì).\n\r", ch);
		    free_buf(buffer);
	    return;
	}

	qsort(array, count, sizeof(mob), compare_objects);

	for (vnum = 0; vnum < count; vnum++)
	{
	    sprintf(buf, "%s: level: [%-2d]  vnum: [%-5d]  [%s]\n\r",
		str_color_fmt(array[vnum]->short_descr, 35), array[vnum]->level,
		array[vnum]->vnum, array[vnum]->area->name);
	    add_buf(buffer, buf);
	}
//È äî ñþäà

    } else if (!str_prefix(arg1, "query"))
    {
	argument = one_argument(argument, arg2);

	if (!str_prefix(arg2, "list") || arg2 == '\0')
	{
	    list_query(ch);
	    return;
	} else if (!str_prefix(arg2, "add"))
	{
	    if (argument[0] == '\0')
	    {
		send_to_char("Îïðåäåëèòå íàçâàíèå.\n\r",ch);
		return;
	    }
	    query = add_query(argument);
	    show_query(ch, query);
	    string_append(ch, &query->text);
	    return;
	} else if (!str_prefix(arg2, "save"))
	{
	    save_query();
	    send_to_char("Çàïðîñû ñîõðàíåíû.\n\r", ch);
	    return;
	}

	if (argument != '\0' && !is_number(argument))
	{                 
	    send_to_char("Ââåäèòå íîìåð çàïðîñà.\n\r",ch);
	    return;
	}

	if (!str_prefix(arg2, "delete"))
	{
	    if (is_number(argument))
		query = get_query(atoi(argument));
	    if (query == NULL)
	    {
		send_to_char("Òàêîãî çàïðîñà íåò.\n\r", ch);
		return;
	    }
	    delete_query(query);
	    send_to_char("Çàïðîñ óäàëåí.\n\r", ch);

	} else if (!str_prefix(arg2, "edit"))
	{
	    if (is_number(argument))
		query = get_query(atoi(argument));
	    if (query == NULL)
	    {
		send_to_char("Çàïðîñ íå íàéäåí.\n\r",ch);
		return;
	    }
	    string_append(ch, &query->text);
	} else if (!str_prefix(arg2, "show"))
	{
	    if (is_number(argument))
		query = get_query(atoi(argument));

	    if (query == NULL)
	    {
		send_to_char("Çàïðîñ íå íàéäåí.\n\r",ch);
		return;
	    }
	    show_query(ch,query);
	}
    }

    if (!str_prefix(arg1, "obj") || !str_prefix(arg1, "mob"))
    {
	sprintf(buf, "\n\rÍàéäåíî: %d.\n\r", count);
	add_buf(buffer, buf);

	page_to_char(buf_string(buffer), ch);
	free_buf(buffer);
    }
    return;
}

void do_charquest(CHAR_DATA *ch, char *argument)
{
	char arg[2*MAX_STRING_LENGTH];
	int mult = 1, qp = 1, exp = 1, skill = 1, i;

	if (IS_NPC(ch)) 
		return;

	send_to_char("Êâåñòû îò èãðîêîâ âðåìåííî îòêëþ÷åíû â ñâÿçè ñî ââîäîì ïîñòîÿííîãî óòðîåíèÿ.\n\r", ch);
	return;
	

	argument = one_argument(argument, arg);
	if (arg[0] != '\0')
	{
		if (!str_prefix(arg, "óäâîåíèå") || !str_prefix(arg, "óòðîåíèå"))
		{			
			if (!str_prefix(arg,"óäâîåíèå"))
			{
				mult = 2;
			}
			if (!str_prefix(arg,"óòðîåíèå"))
			{
				mult = 3;
			}

			if (immquest.is_quest)
			{
				send_to_char("Ïîäîæäè, ïîêà çàêîí÷èòñÿ òåêóùèé êâåñò.\n\r", ch);
				return;
			}
			
			if (mult>1)
			{				
				for (i = 1; i < 4; i++)
				{
					one_argument(argument, arg);
 
					if (is_number(arg)) break;
					
					argument = one_argument(argument, arg);

					if (!str_prefix(arg, "qp"))
					{
						qp = mult;
					}
					if (!str_prefix(arg, "exp"))
					{
						exp = mult;
					}
					if (!str_prefix(arg, "skill"))
					{
						skill = mult;
					}
				}
				if (qp == 1 && exp == 1 && skill == 1)
				{
					send_to_char("Äîñòóïíûå ïàðàìåòðû: QP, EXP, SKILL.\n\r",ch);
					return;
				}		
				
				argument=one_argument(argument, arg);
				
				if (!is_number(arg) || (immquest.timer = atoi(arg)) <= 30)
				{
					send_to_char("Òàéìåð äîëæåí áûòü ÷èñëîâûì è áûòü áîëüøå 30.\n\r", ch);
					return;
				}

				if (atoi(arg) <= 30)
				{
					send_to_char("Òàéìåð äîëæåí áûòü íå áîëüøå 300.\n\r", ch);
					return;
				}

				if (atoi(arg) >= 300)
				{
					send_to_char("Òàéìåð äîëæåí áûòü íå áîëüøå 300.\n\r", ch);
					return;
				}

				if (ch->pcdata->bronze < (qp + skill + exp - 3) * immquest.timer) 
				{
					send_to_char("Áðîíçû íåäîñòàòî÷íî äëÿ òàêîãî âðåìåíè.\n\r", ch);
					return;		
				}
				
				ch->pcdata->bronze -= ((qp + skill + exp - 3) * immquest.timer);

				immquest.who_claim = ch;
				sprintf(immquest.desc, "%s ïîëó÷àåìûõ î÷êîâ %s%s%s%s%s",
				mult == 2 ? "Óäâîåíèå" : "Óòðîåíèå",
				(exp>1) ? "îïûòà" : "",
				(exp>1 && qp>1) ? ", " : "",
				(qp>1) ? "óäà÷è" : "",
				(exp>1 && qp>1 && skill>1) ? " è " : ((exp>1 || qp>1) && skill>1) ? " , " : "",
				(skill>1) ? "ñêîðîñòè îáó÷åíèÿ" : ""
				);
				
				sprintf(arg, "{G%s îïëà÷èâàåò: %s.{x{*\n\r", immquest.who_claim->name, immquest.desc);
				
				
				immquest.is_quest     = TRUE;
				immquest.double_qp    = qp;
				immquest.double_exp   = exp;
				immquest.double_skill = skill;

				do_function(ch, &do_echo, arg);
				
				return;
			}
		}		
	}	
	send_to_char("Íåò òàêîé êîìàíäû. Ñïèñîê äîñòóïíûõ:\n\r"
	"ñêâåñò óäâîåíèå|óòðîåíèå [qp] [skill] [exp] <timer>\n\r"
	"íàïðèìåð: ñêâåñò óòðîåíèå exp 60 -- çàïóñêàåò óòðîåíèå ýêñïû íà 60 ÷àñîâ\n\r"
	"Ðàñõîä áðîíçû ïðè óäâîåíèè 1/÷àñ, ïðè óòðîåíèè 2/÷àñ\n\r"
	"çà êàæäûé ïàðàìåòð öåíà óìíîæàåòñÿ íà êîë-âî ïàðàìåòðîâ: óäâîåíèå îïûòà ñòîèò 1/÷àñ, óòðîåíèå âñåãî - 6/÷àñ\n\r", ch);
	return;
}


/*ÏÎÑÌÎÒÐÅÒÜ ÝÔÔÅÊÒÛ ÍÀ ×ÀÐÅ*/
void do_immaffects(CHAR_DATA *ch, char *argument){
    CHAR_DATA *victim;
    AFFECT_DATA *paf;
    char arg[MAX_INPUT_LENGTH];
    char buf[MAX_STRING_LENGTH];
    
    argument = one_argument(argument, arg);

    if ((victim = get_char_world(ch, arg)) == NULL)
    {
	send_to_char("Òàêèõ â Ìèðå íåò.\n\r", ch);
	return;
    }


    //  show_skills(ch, victim, argument);


    for (paf = victim->affected; paf != NULL; paf = paf->next){
        if (paf->type < 1 || paf->type>= max_skills)
	        continue;
    
        /* Affc 'camouflage'  0  51  48   0   0 0 1*/
        sprintf(buf, "Affc '%s' %3d %3d %3d %3d %3d %lu %d \n",
                skill_table[paf->type].name,
                paf->where,
                paf->level,
                paf->duration,
                paf->modifier,
                paf->location,
                paf->bitvector,
                paf->caster_id
        );

        send_to_char(buf, ch);
    }
}

void do_get_mob_param(CHAR_DATA *ch, char *argument){

}

/* charset=cp1251 */
